/*
 * ThreadBase.cpp
 *
 *  Created on: Feb 3, 2021
 *      Author: yankai
 */

#include "_Thread.h"
#include "../UI/_Console.h"

namespace kai
{
	_Thread::_Thread()
	{
		m_class = "_Thread";
		m_threadID = 0;
		m_bJoining = false;
		m_wakeupGeneration = 0;
		m_dT = 1.0;
		m_FPS = 0;
		m_targetFPS = DEFAULT_FPS;
		m_targetTframe = SEC_2_USEC / m_targetFPS;
		m_tFrom = 0;
		m_tTo = 0;

		m_state.store(thread_stop);
		m_setState.store(thread_stop);
		m_bPaused.store(false);
		m_bSkipSleep.store(false);

		pthread_mutex_init(&m_lifecycleMutex, NULL);
		pthread_mutex_init(&m_wakeupMutex, NULL);
		pthread_cond_init(&m_wakeupSignal, NULL);
	}

	_Thread::~_Thread()
	{
		stop();
		join();

		pthread_cond_destroy(&m_wakeupSignal);
		pthread_mutex_destroy(&m_wakeupMutex);
		pthread_mutex_destroy(&m_lifecycleMutex);
	}

	bool _Thread::init(const json &j)
	{
		IF_F(!this->BASE::init(j));

		float FPS = DEFAULT_FPS;
		jKv(j, "FPS", FPS);
		setTargetFPS(FPS);

		return true;
	}

	bool _Thread::link(const json &j, ModuleMgr *pM)
	{
		IF_F(!this->BASE::link(j, pM));

		vector<string> vRunT;
		jKv(j, "vRunThread", vRunT);
		m_vRunThread.clear();
		for (string s : vRunT)
		{
			_Thread *pT = (_Thread *)(pM->findModule(s));
			if (!pT)
			{
				LOG_I("Instance not found: " + s);
				continue;
			}

			m_vRunThread.push_back(pT);
		}

		return true;
	}

	bool _Thread::startThread(void *(*__start_routine)(void *),
						void *__arg)
	{
		NULL_F(__start_routine);

		pthread_mutex_lock(&m_lifecycleMutex);
		if (m_threadID != 0)
		{
			pthread_mutex_unlock(&m_lifecycleMutex);
			return false;
		}

		THREAD_START_CONTEXT *pContext = new THREAD_START_CONTEXT();
		if (!pContext)
		{
			pthread_mutex_unlock(&m_lifecycleMutex);
			return false;
		}

		pContext->m_pThread = this;
		pContext->m_pRoutine = __start_routine;
		pContext->m_pArg = __arg;

		m_setState.store(thread_run);
		m_state.store(thread_run);
		m_tFrom = getApproxTbootUs();

		int r = pthread_create(&m_threadID, 0, threadEntry, pContext);
		if (r != 0)
		{
			delete pContext;
			m_threadID = 0;
			m_setState.store(thread_stop);
			m_state.store(thread_stop);
			pthread_mutex_unlock(&m_lifecycleMutex);
			return false;
		}

		pthread_mutex_unlock(&m_lifecycleMutex);
		return true;
	}

	void *_Thread::threadEntry(void *pContext)
	{
		unique_ptr<THREAD_START_CONTEXT> pStart((THREAD_START_CONTEXT *)pContext);
		void *pResult = pStart->m_pRoutine(pStart->m_pArg);
		pStart->m_pThread->finishThread();

		return pResult;
	}

	void _Thread::finishThread(void)
	{
		m_setState.store(thread_stop);
		m_state.store(thread_stop);
		wake();
	}

	bool _Thread::join(void)
	{
		pthread_mutex_lock(&m_lifecycleMutex);
		if (m_threadID == 0)
		{
			pthread_mutex_unlock(&m_lifecycleMutex);
			return true;
		}

		if (pthread_equal(pthread_self(), m_threadID) || m_bJoining)
		{
			pthread_mutex_unlock(&m_lifecycleMutex);
			return false;
		}

		m_bJoining = true;
		pthread_t threadID = m_threadID;
		pthread_mutex_unlock(&m_lifecycleMutex);

		int r = pthread_join(threadID, NULL);

		pthread_mutex_lock(&m_lifecycleMutex);
		if (r == 0 && pthread_equal(threadID, m_threadID))
			m_threadID = 0;
		m_bJoining = false;
		pthread_mutex_unlock(&m_lifecycleMutex);

		return (r == 0);
	}

	bool _Thread::bAlive(void)
	{
		return (m_setState.load() != thread_stop);
	}

	bool _Thread::bRun(void)
	{
		return (m_state.load() == thread_run);
	}

	bool _Thread::bStop(void)
	{
		return (m_state.load() == thread_stop);
	}

	void _Thread::run(void)
	{
		m_setState.store(thread_run);
		wake();
	}

	void _Thread::pause(void)
	{
		m_setState.store(thread_pause);
		wake();
	}

	void _Thread::stop(void)
	{
		m_setState.store(thread_stop);
		wake();
	}

	void _Thread::wake(void)
	{
		pthread_mutex_lock(&m_wakeupMutex);
		++m_wakeupGeneration;
		pthread_cond_broadcast(&m_wakeupSignal);
		pthread_mutex_unlock(&m_wakeupMutex);
	}

	bool _Thread::bOnPause(void)
	{
		IF_F(m_setState.load() != thread_pause);

		bool expected = false;
		return m_bPaused.compare_exchange_strong(expected, true);
	}

	bool _Thread::bOnResume(void)
	{
		IF_F(m_setState.load() == thread_pause);

		bool expected = true;
		return m_bPaused.compare_exchange_strong(expected, false);
	}

	void _Thread::runAll(void)
	{
		for (_Thread *pT : m_vRunThread)
			pT->run();
	}

	void _Thread::sleepT(int64_t usec)
	{
		m_state.store(thread_sleep);
		pthread_mutex_lock(&m_wakeupMutex);
		uint64_t wakeupGeneration = m_wakeupGeneration;

		if (bAlive() && usec > 0)
		{
			struct timespec tTimeout;
			clock_gettime(CLOCK_REALTIME, &tTimeout);
			int64_t nsec = tTimeout.tv_nsec + usec * 1000;
			tTimeout.tv_sec += nsec / NSEC_1SEC;
			tTimeout.tv_nsec = nsec % NSEC_1SEC;

			while (bAlive() && wakeupGeneration == m_wakeupGeneration)
			{
				int r = pthread_cond_timedwait(&m_wakeupSignal, &m_wakeupMutex, &tTimeout);
				if (r == ETIMEDOUT)
					break;
			}
		}
		else if (bAlive())
		{
			while (bAlive() && wakeupGeneration == m_wakeupGeneration)
				pthread_cond_wait(&m_wakeupSignal, &m_wakeupMutex);
		}

		pthread_mutex_unlock(&m_wakeupMutex);
		m_state.store(bAlive() ? thread_run : thread_stop);
	}

	void _Thread::skipSleep(void)
	{
		m_bSkipSleep.store(true);
		wake();
	}

	void _Thread::autoFPS(void)
	{
		m_tTo = getApproxTbootUs();

		if (!m_bSkipSleep.exchange(false))
		{
			int uSleep = (int)(m_targetTframe - (m_tTo - m_tFrom));
			if (uSleep > 1000)
			{
				sleepT(uSleep);
			}
		}

		if (m_setState.load() == thread_pause)
		{
			m_FPS = 0;
			sleepT(0);
			m_tFrom = getApproxTbootUs();
		}

		uint64_t tNow = getApproxTbootUs();
		m_dT = (float)(tNow - m_tFrom + 1);
		m_tFrom = tNow;
		m_FPS = SEC_2_USEC / m_dT;
	}

	float _Thread::getFPS(void)
	{
		return m_FPS;
	}

	void _Thread::setTargetFPS(float fps)
	{
		IF_(fps <= 0);

		m_targetFPS = fps;
		m_targetTframe = SEC_2_USEC / m_targetFPS;
	}

	float _Thread::getTargetFPS(void)
	{
		return m_targetFPS;
	}

	uint64_t _Thread::getTfrom(void)
	{
		return m_tFrom;
	}

	uint64_t _Thread::getTto(void)
	{
		return m_tTo;
	}

	float _Thread::getDt(void)
	{
		return m_dT;
	}

	void _Thread::console(void *pConsole)
	{
		NULL_(pConsole);

		string msg = "FPS: " + f2str(m_FPS, 2);
		string t = " " + this->getName();

		_Console *pC = (_Console *)pConsole;
		pC->addMsg(t, COLOR_PAIR(_Console_COL_NAME) | A_BOLD, _Console_X_NAME, 1);
		pC->addMsg(msg, COLOR_PAIR(_Console_COL_FPS) | A_BOLD, _Console_X_FPS);
	}

}
