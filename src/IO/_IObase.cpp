/*
 * _IObase.cpp
 *
 *  Created on: June 16, 2016
 *      Author: yankai
 */

#include "_IObase.h"

namespace kai
{
	IO_PACKET_FIFO::IO_PACKET_FIFO()
	{
		pthread_mutex_init(&m_mutex, NULL);
	}

	IO_PACKET_FIFO::~IO_PACKET_FIFO()
	{
		release();
		pthread_mutex_destroy(&m_mutex);
	}

	bool IO_PACKET_FIFO::init(int nB, int nP,
							 IO_PACKET_FIFO_MODE mode,
							 size_t nBqueuedMax)
	{
		IF_F(nB <= 0);
		IF_F(nP <= 0);

		const size_t nBcapacity = static_cast<size_t>(nB) * static_cast<size_t>(nP);
		if (nBqueuedMax == 0)
			nBqueuedMax = nBcapacity;
		IF_F(nBqueuedMax == 0);

		pthread_mutex_lock(&m_mutex);
		m_qP.clear();
		m_nB = nB;
		m_nP = nP;
		m_iPset = 0;
		m_iPget = 0;
		m_nBqueued = 0;
		m_nBqueuedMax = nBqueuedMax;
		m_nDropped = 0;
		m_mode = mode;
		m_bInit = true;
		pthread_mutex_unlock(&m_mutex);

		return true;
	}

	void IO_PACKET_FIFO::release(void)
	{
		pthread_mutex_lock(&m_mutex);
		m_qP.clear();
		m_nB = 0;
		m_nP = 0;
		m_iPset = 0;
		m_iPget = 0;
		m_nBqueued = 0;
		m_nBqueuedMax = 0;
		m_nDropped = 0;
		m_bInit = false;
		pthread_mutex_unlock(&m_mutex);
	}

	void IO_PACKET_FIFO::clear(void)
	{
		pthread_mutex_lock(&m_mutex);
		m_qP.clear();
		m_iPset = 0;
		m_iPget = 0;
		m_nBqueued = 0;
		m_nDropped = 0;
		pthread_mutex_unlock(&m_mutex);
	}

	bool IO_PACKET_FIFO::setPacket(const uint8_t *pB, int nB)
	{
		IF_F(!pB);
		IF_F(nB <= 0);

		pthread_mutex_lock(&m_mutex);
		if (!m_bInit)
		{
			pthread_mutex_unlock(&m_mutex);
			return false;
		}

		const size_t nPacket = (m_mode == ioPacket_message)
							   ? 1
							   : (static_cast<size_t>(nB) + m_nB - 1) / m_nB;
		if ((m_mode == ioPacket_message && nB > m_nB) ||
			nPacket > static_cast<size_t>(m_nP) ||
			m_qP.size() + nPacket > static_cast<size_t>(m_nP) ||
			m_nBqueued + static_cast<size_t>(nB) > m_nBqueuedMax)
		{
			m_nDropped++;
			pthread_mutex_unlock(&m_mutex);
			return false;
		}

		int iB = 0;
		while (iB < nB)
		{
			const int nCopy = (m_mode == ioPacket_message)
							  ? nB
							  : small<int>(m_nB, nB - iB);
			m_qP.emplace_back(&pB[iB], &pB[iB + nCopy]);
			iB += nCopy;
			m_iPset = (m_iPset + 1) % m_nP;
		}
		m_nBqueued += static_cast<size_t>(nB);
		pthread_mutex_unlock(&m_mutex);

		return true;
	}

	int IO_PACKET_FIFO::getPacket(uint8_t *pB, int nB)
	{
		IF__(!pB, -1);
		IF__(nB <= 0, -1);

		pthread_mutex_lock(&m_mutex);
		if (m_qP.empty())
		{
			pthread_mutex_unlock(&m_mutex);
			return 0;
		}

		vector<uint8_t> &packet = m_qP.front();
		const size_t nRead = small<size_t>(packet.size(), static_cast<size_t>(nB));
		memcpy(pB, packet.data(), nRead);
		m_nBqueued -= nRead;

		if (nRead == packet.size())
		{
			m_qP.pop_front();
			m_iPget = (m_iPget + 1) % m_nP;
		}
		else
		{
			// Byte-stream consumers such as _JSONbase read into a 512-byte
			// buffer. Preserve the unread tail of a larger WebSocket message
			// instead of dropping the entire command.
			packet.erase(packet.begin(), packet.begin() + nRead);
		}
		pthread_mutex_unlock(&m_mutex);

		return static_cast<int>(nRead);
	}

	bool IO_PACKET_FIFO::getPacket(vector<uint8_t> *pB)
	{
		NULL_F(pB);

		pthread_mutex_lock(&m_mutex);
		if (m_qP.empty())
		{
			pthread_mutex_unlock(&m_mutex);
			return false;
		}

		*pB = std::move(m_qP.front());
		m_qP.pop_front();
		m_nBqueued -= pB->size();
		m_iPget = (m_iPget + 1) % m_nP;
		pthread_mutex_unlock(&m_mutex);

		return true;
	}

	size_t IO_PACKET_FIFO::nQueued(void)
	{
		pthread_mutex_lock(&m_mutex);
		const size_t n = m_qP.size();
		pthread_mutex_unlock(&m_mutex);
		return n;
	}

	size_t IO_PACKET_FIFO::nBytesQueued(void)
	{
		pthread_mutex_lock(&m_mutex);
		const size_t n = m_nBqueued;
		pthread_mutex_unlock(&m_mutex);
		return n;
	}

	uint64_t IO_PACKET_FIFO::nDropped(void)
	{
		pthread_mutex_lock(&m_mutex);
		const uint64_t n = m_nDropped;
		pthread_mutex_unlock(&m_mutex);
		return n;
	}

	_IObase::_IObase()
	{
		m_ioType = io_none;
		m_ioStatus = io_unknown;
	}

	_IObase::~_IObase()
	{
		m_packetW.release();
	}

	bool _IObase::init(const json &j)
	{
		IF_F(!this->_ModuleBase::init(j));

		int nPacket = 256;
		int nPbuffer = 2000;
		jKv(j, "nPacket", nPacket);
		jKv(j, "nPbuffer", nPbuffer);
		IF_F(!m_packetW.init(nPbuffer, nPacket));

		return true;
	}

	bool _IObase::link(const json &j, ModuleMgr *pM)
	{
		IF_F(!this->_ModuleBase::link(j, pM));

		return true;
	}

	bool _IObase::open(void)
	{
		return false;
	}

	bool _IObase::bOpen(void)
	{
		return (m_ioStatus == io_opened);
	}

	IO_TYPE _IObase::ioType(void)
	{
		return m_ioType;
	}

	void _IObase::close(void)
	{
		m_packetW.clear();

		m_ioStatus = io_closed;
	}

	bool _IObase::write(uint8_t *pBuf, int nB)
	{
		IF_F(m_ioStatus != io_opened);
		IF_F(!pBuf);
		IF_F(nB <= 0);

		IF_F(!m_packetW.setPacket(pBuf, nB));

		NULL__(m_pT, true);
		m_pT->run();
		return true;
	}

	int _IObase::read(uint8_t *pBuf, int nB)
	{
		if (m_ioStatus != io_opened)
			return -1;

		return 0;
	}

	IO_STATUS _IObase::getIOstatus(void)
	{
		return m_ioStatus.load();
	}

	void _IObase::setIOstatus(IO_STATUS s)
	{
		m_ioStatus.store(s);
	}

	IO_PACKET_FIFO *_IObase::getPacketFIFOw(void)
	{
		return &m_packetW;
	}

	void _IObase::console(void *pConsole)
	{
		NULL_(pConsole);
		this->_ModuleBase::console(pConsole);
		_Console *pC = (_Console *)pConsole;

		pC->addMsg("packetW_nQueued=" + li2str(m_packetW.nQueued()));
		pC->addMsg("packetW_nDropped=" + li2str(m_packetW.nDropped()));
	}

}
