/*
 * _WebSocketServer.h
 *
 *  Created on: Nov. 8, 2024
 *      Author: yankai
 */

#ifndef OpenKAI_src_IO__WebSocketServer_H_
#define OpenKAI_src_IO__WebSocketServer_H_

#include "_WebSocket.h"

namespace kai
{
	struct wsServerCallbackContext;

	struct wsClient
	{
		shared_ptr<_WebSocket> m_pWS;
		ws_cli_conn_t m_wsConn;
		uint64_t m_tStamp;

		bool init(const shared_ptr<_WebSocket> &pWS)
		{
			NULL_F(pWS);

			m_pWS = pWS;
			m_tStamp = getApproxTbootUs();
			return true;
		}

		void setWS(const shared_ptr<_WebSocket> &pWS)
		{
			m_pWS = pWS;
		}

		shared_ptr<_WebSocket> getWS(void)
		{
			return m_pWS;
		}
	};

	enum WSSOCKET_MODE
	{
		wsSocket_bin = 0,
		wsSocket_bin_bcast = 1,
		wsSocket_txt = 2,
		wsSocket_txt_bcast = 3,
	};

	class _WebSocketServer : public _IObase
	{
	public:
		_WebSocketServer();
		virtual ~_WebSocketServer();

		virtual bool init(const json& j);
		virtual bool link(const json& j, ModuleMgr* pM);
		virtual bool start(void);
		virtual void console(void *pConsole);

		// default to client 0
		bool write(uint8_t *pBuf, int nB);
		int read(uint8_t *pBuf, int nB);

		int nClient(void);
		shared_ptr<_WebSocket> getClientShared(int i);

		static void sCbOpen(ws_cli_conn_t client);
		static void sCbClose(ws_cli_conn_t client);
		static void sCbMessage(ws_cli_conn_t client, const unsigned char *msg, uint64_t size, int type);

	private:
		void cbOpen(ws_cli_conn_t client);
		void cbClose(ws_cli_conn_t client);
		void cbMessage(ws_cli_conn_t client, const unsigned char *msg, uint64_t size, int type);

		shared_ptr<wsClient> findWSclient(ws_cli_conn_t wsCli);
		shared_ptr<wsClient> getWSclient(int i);
		shared_ptr<wsClient> removeWSclient(ws_cli_conn_t wsCli);
		vector<shared_ptr<wsClient>> getClientSnapshot(void);
//		wsClient *findClient(const string& addr, const string& port);

		void updateW(void);
		static void *getUpdateW(void *This)
		{
			((_WebSocketServer *)This)->updateW();
			return NULL;
		}

		void updateR(void);
		static void *getUpdateR(void *This)
		{
			((_WebSocketServer *)This)->updateR();
			return NULL;
		}

	protected:
		vector<shared_ptr<wsClient>> m_vClient;
		pthread_mutex_t m_clientMutex;
		int m_nClientMax;
		int m_nPacket;
		int m_nMessageMax;
		int m_nQueueBytesMax;
		WSSOCKET_MODE m_wsMode;

		string m_host;
		uint16_t m_port;
		uint32_t m_tOutMs;

		wsServerCallbackContext *m_pCallbackContext;
		_Thread *m_pTr;
	};

}
#endif
