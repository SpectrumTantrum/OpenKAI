/*
 * _WebSocketServer.cpp
 *
 *  Created on: August 8, 2016
 *      Author: yankai
 */

#include "_WebSocketServer.h"

#include <limits>

namespace kai
{
	struct wsServerCallbackContext
	{
		pthread_mutex_t m_mutex;
		pthread_cond_t m_idle;
		_WebSocketServer *m_pServer;
		size_t m_nActive;
		bool m_bStopping;
	};

	namespace
	{
		_WebSocketServer *acquireServer(ws_cli_conn_t client,
										 wsServerCallbackContext **ppContext)
		{
			wsServerCallbackContext *pContext =
				static_cast<wsServerCallbackContext *>(ws_get_server_context(client));
			if (!pContext)
				return nullptr;

			pthread_mutex_lock(&pContext->m_mutex);
			if (pContext->m_bStopping || !pContext->m_pServer)
			{
				pthread_mutex_unlock(&pContext->m_mutex);
				return nullptr;
			}

			++pContext->m_nActive;
			_WebSocketServer *pServer = pContext->m_pServer;
			pthread_mutex_unlock(&pContext->m_mutex);
			*ppContext = pContext;
			return pServer;
		}

		void releaseServer(wsServerCallbackContext *pContext)
		{
			if (!pContext)
				return;

			pthread_mutex_lock(&pContext->m_mutex);
			if (--pContext->m_nActive == 0)
				pthread_cond_broadcast(&pContext->m_idle);
			pthread_mutex_unlock(&pContext->m_mutex);
		}
	}

	_WebSocketServer::_WebSocketServer()
	{
		m_pTr = nullptr;
		pthread_mutex_init(&m_clientMutex, NULL);
		m_nClientMax = MAX_CLIENTS;
		m_nPacket = 256;
		m_nMessageMax = 1024 * 1024;
		m_nQueueBytesMax = 4 * 1024 * 1024;
		m_wsMode = wsSocket_txt_bcast;

		m_host = "localhost";
		m_port = 8080;
		m_tOutMs = 1000;

		m_pCallbackContext = new wsServerCallbackContext();
		pthread_mutex_init(&m_pCallbackContext->m_mutex, NULL);
		pthread_cond_init(&m_pCallbackContext->m_idle, NULL);
		m_pCallbackContext->m_pServer = this;
		m_pCallbackContext->m_nActive = 0;
		m_pCallbackContext->m_bStopping = false;
	}

	_WebSocketServer::~_WebSocketServer()
	{
		m_ioStatus = io_closed;
		if (m_pT)
		{
			m_pT->stop();
			m_pT->join();
		}

		// wsServer has no listener-stop API. Retire this process-lifetime
		// callback context and wait for callbacks already using this object.
		// The listener thread and token are intentionally retained until exit.
		pthread_mutex_lock(&m_pCallbackContext->m_mutex);
		m_pCallbackContext->m_bStopping = true;
		m_pCallbackContext->m_pServer = nullptr;
		while (m_pCallbackContext->m_nActive > 0)
			pthread_cond_wait(&m_pCallbackContext->m_idle,
							  &m_pCallbackContext->m_mutex);
		pthread_mutex_unlock(&m_pCallbackContext->m_mutex);

		const vector<shared_ptr<wsClient>> vClient = getClientSnapshot();
		pthread_mutex_lock(&m_clientMutex);
		m_vClient.clear();
		pthread_mutex_unlock(&m_clientMutex);

		for (const shared_ptr<wsClient> &pClient : vClient)
		{
			shared_ptr<_WebSocket> pWS = pClient->getWS();
			if (pWS)
				pWS->setIOstatus(io_closed);
			ws_close_client(pClient->m_wsConn);
		}

		if (m_pTr)
		{
			m_pTr->stop();
			if (m_pTr->bStop())
			{
				m_pTr->join();
				DEL(m_pTr);
			}
			else
			{
				// Its ws_socket() call is blocked forever in the dependency's
				// accept loop. The callback token above makes retaining it safe.
				m_pTr = nullptr;
			}
		}

		pthread_mutex_destroy(&m_clientMutex);
	}

	bool _WebSocketServer::init(const json &j)
	{
		IF_F(!this->_IObase::init(j));

		jKv(j, "wsMode", m_wsMode);
		jKv(j, "host", m_host);
		jKv(j, "port", m_port);
		jKv(j, "tOutMs", m_tOutMs);
		jKv(j, "nClientMax", m_nClientMax);
		jKv(j, "nPacket", m_nPacket);
		jKv(j, "nMessageMax", m_nMessageMax);
		jKv(j, "nQueueBytesMax", m_nQueueBytesMax);

		IF_F(m_wsMode < wsSocket_bin || m_wsMode > wsSocket_txt_bcast);
		IF_F(m_nClientMax <= 0);
		IF_F(m_nPacket <= 0);
		IF_F(m_nMessageMax <= 0 || m_nMessageMax > MAX_FRAME_LENGTH);
		IF_F(m_nQueueBytesMax <= 0);
		m_nClientMax = small<int>(m_nClientMax, MAX_CLIENTS);
		IF_F(!m_packetW.init(m_nMessageMax, m_nPacket, ioPacket_message,
							 static_cast<size_t>(m_nQueueBytesMax)));

		DEL(m_pTr);
		m_pTr = createThread(jK(j, "threadR"), "threadR");
		NULL_F(m_pTr);

		m_ioStatus = io_opened;

		return true;
	}

	bool _WebSocketServer::link(const json &j, ModuleMgr *pM)
	{
		IF_F(!this->_IObase::link(j, pM));

		return true;
	}

	bool _WebSocketServer::start(void)
	{
		NULL_F(m_pT);
		NULL_F(m_pTr);
		IF_F(!m_pT->startThread(getUpdateW, this));
		return m_pTr->startThread(getUpdateR, this);
	}

	void _WebSocketServer::updateW(void)
	{
		// signal(SIGPIPE, SIG_IGN);

		while (m_pT->bAlive())
		{
			m_pT->autoFPS();

			const vector<shared_ptr<wsClient>> vClient = getClientSnapshot();
			if (vClient.empty())
				continue;

			const bool bBroadcast = (m_wsMode == wsSocket_bin_bcast ||
									 m_wsMode == wsSocket_txt_bcast);
			const int frameType = (m_wsMode == wsSocket_bin ||
								 m_wsMode == wsSocket_bin_bcast)
								? WS_FR_OP_BIN
								: WS_FR_OP_TXT;

			vector<uint8_t> msg;
			while (m_packetW.getPacket(&msg))
			{
				const char *pMsg = reinterpret_cast<const char *>(msg.data());
				const uint64_t nB = static_cast<uint64_t>(msg.size());
				if (bBroadcast)
					ws_sendframe_bcast(m_port, pMsg, nB, frameType);
				else
					ws_sendframe(vClient.front()->m_wsConn, pMsg, nB, frameType);
			}

			for (const shared_ptr<wsClient> &pClient : vClient)
			{
				shared_ptr<_WebSocket> pWS = pClient->getWS();
				IF_CONT(!pWS);

				IO_PACKET_FIFO *pPw = pWS->getPacketFIFOw();
				while (pPw->getPacket(&msg))
				{
					const char *pMsg = reinterpret_cast<const char *>(msg.data());
					const uint64_t nB = static_cast<uint64_t>(msg.size());
					ws_sendframe(pClient->m_wsConn, pMsg, nB, frameType);
				}
			}
		}
	}

	void _WebSocketServer::updateR(void)
	{
		ws_server ws;
		ws.host = m_host.c_str();
		ws.port = m_port;
		ws.thread_loop = 0;
		ws.timeout_ms = m_tOutMs;
		ws.context = m_pCallbackContext;
		ws.evs.onopen = &sCbOpen;
		ws.evs.onclose = &sCbClose;
		ws.evs.onmessage = &sCbMessage;

		ws_socket(&ws);
	}

	int _WebSocketServer::nClient(void)
	{
		pthread_mutex_lock(&m_clientMutex);
		const int n = static_cast<int>(m_vClient.size());
		pthread_mutex_unlock(&m_clientMutex);
		return n;
	}

	shared_ptr<_WebSocket> _WebSocketServer::getClientShared(int i)
	{
		shared_ptr<wsClient> pC = getWSclient(i);
		NULL_N(pC);
		return pC->getWS();
	}

	bool _WebSocketServer::write(uint8_t *pBuf, int nB)
	{
		return this->_IObase::write(pBuf, nB);
	}

	int _WebSocketServer::read(uint8_t *pBuf, int nB)
	{
		shared_ptr<_WebSocket> pWS = getClientShared(0);
		NULL__(pWS, 0);

		return pWS->read(pBuf, nB);
	}

	void _WebSocketServer::sCbOpen(ws_cli_conn_t client)
	{
		wsServerCallbackContext *pContext = nullptr;
		_WebSocketServer *pServer = acquireServer(client, &pContext);
		NULL_(pServer);
		pServer->cbOpen(client);
		releaseServer(pContext);
	}

	void _WebSocketServer::sCbClose(ws_cli_conn_t client)
	{
		wsServerCallbackContext *pContext = nullptr;
		_WebSocketServer *pServer = acquireServer(client, &pContext);
		NULL_(pServer);
		pServer->cbClose(client);
		releaseServer(pContext);
	}

	void _WebSocketServer::sCbMessage(ws_cli_conn_t client,
									  const unsigned char *msg, uint64_t size, int type)
	{
		wsServerCallbackContext *pContext = nullptr;
		_WebSocketServer *pServer = acquireServer(client, &pContext);
		NULL_(pServer);
		pServer->cbMessage(client, msg, size, type);
		releaseServer(pContext);
	}

	void _WebSocketServer::cbOpen(ws_cli_conn_t client)
	{
		char *cli, *port;
		cli = ws_getaddress(client);
		port = ws_getport(client);

		json j = json::object();
		j["name"] = this->getName() + ".WS" + li2str(client);
		j["class"] = "_WebSocket";
		j["nPacket"] = m_nPacket;
		j["nMessageMax"] = m_nMessageMax;
		j["nQueueBytesMax"] = m_nQueueBytesMax;
		json jT = json::object();
		jT["FPS"] = 1;
		j["thread"] = jT;

		shared_ptr<_WebSocket> pWS = make_shared<_WebSocket>();
		IF_(!pWS->init(j));
		pWS->setIOstatus(io_opened);

		shared_ptr<wsClient> pClient = make_shared<wsClient>();
		IF_(!pClient->init(pWS));
		pClient->m_wsConn = client;

		pthread_mutex_lock(&m_clientMutex);
		if (m_vClient.size() >= static_cast<size_t>(m_nClientMax))
		{
			pthread_mutex_unlock(&m_clientMutex);
			pWS->setIOstatus(io_closed);
			ws_close_client(client);
			return;
		}
		m_vClient.push_back(pClient);
		pthread_mutex_unlock(&m_clientMutex);

		LOG_I("Connection opened, addr: " + string(cli) + ", port: " + string(port));
	}

	void _WebSocketServer::cbClose(ws_cli_conn_t client)
	{
		char *cli;
		cli = ws_getaddress(client);

		shared_ptr<wsClient> pClient = removeWSclient(client);
		NULL_(pClient);

		shared_ptr<_WebSocket> pWS = pClient->getWS();
		NULL_(pWS);

		pWS->setIOstatus(io_closed);

		LOG_I("Connection closed, addr: " + string(cli));
	}

	void _WebSocketServer::cbMessage(ws_cli_conn_t client,
									 const unsigned char *msg, uint64_t size, int type)
	{
		char *cli;
		cli = ws_getaddress(client);

		shared_ptr<wsClient> pC = findWSclient(client);
		NULL_(pC);

		shared_ptr<_WebSocket> pWS = pC->getWS();
		NULL_(pWS);

		IO_PACKET_FIFO *pFifo = pWS->getPacketFIFOr();
		NULL_(pFifo);

		IF_(size > static_cast<uint64_t>(numeric_limits<int>::max()));
		IF_(!pFifo->setPacket(msg, static_cast<int>(size)));

		LOG_I("Received message, size: " + li2str(size) + ", type: " + i2str(type) + ", from: " + string(cli));
	}

	shared_ptr<wsClient> _WebSocketServer::findWSclient(ws_cli_conn_t wsCli)
	{
		pthread_mutex_lock(&m_clientMutex);
		for (const shared_ptr<wsClient> &pClient : m_vClient)
		{
			if (pClient->m_wsConn == wsCli)
			{
				pthread_mutex_unlock(&m_clientMutex);
				return pClient;
			}
		}
		pthread_mutex_unlock(&m_clientMutex);
		return nullptr;
	}

	shared_ptr<wsClient> _WebSocketServer::getWSclient(int i)
	{
		IF_N(i < 0);
		pthread_mutex_lock(&m_clientMutex);
		if (static_cast<size_t>(i) >= m_vClient.size())
		{
			pthread_mutex_unlock(&m_clientMutex);
			return nullptr;
		}
		shared_ptr<wsClient> pClient = m_vClient[i];
		pthread_mutex_unlock(&m_clientMutex);
		return pClient;
	}

	shared_ptr<wsClient> _WebSocketServer::removeWSclient(ws_cli_conn_t wsCli)
	{
		pthread_mutex_lock(&m_clientMutex);
		for (auto it = m_vClient.begin(); it != m_vClient.end(); ++it)
		{
			if ((*it)->m_wsConn != wsCli)
				continue;

			shared_ptr<wsClient> pClient = *it;
			m_vClient.erase(it);
			pthread_mutex_unlock(&m_clientMutex);
			return pClient;
		}
		pthread_mutex_unlock(&m_clientMutex);
		return nullptr;
	}

	vector<shared_ptr<wsClient>> _WebSocketServer::getClientSnapshot(void)
	{
		pthread_mutex_lock(&m_clientMutex);
		const vector<shared_ptr<wsClient>> vClient = m_vClient;
		pthread_mutex_unlock(&m_clientMutex);
		return vClient;
	}

	// wsClient *_WebSocketServer::findClient(const string &addr, const string &port)
	// {
	// }

	void _WebSocketServer::console(void *pConsole)
	{
		NULL_(pConsole);
		this->_IObase::console(pConsole);

		((_Console *)pConsole)->addMsg("nClients: " + i2str(nClient()), 1);
	}

}
