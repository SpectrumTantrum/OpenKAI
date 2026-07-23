/*
 * _IObase.h
 *
 *  Created on: June 16, 2016
 *      Author: yankai
 */

#ifndef OpenKAI_src_IO_IObase_H_
#define OpenKAI_src_IO_IObase_H_

#include "../Base/_ModuleBase.h"
#include "../UI/_Console.h"

#include <atomic>

#define IO_BUF_N 2000

namespace kai
{

	enum IO_TYPE
	{
		io_none,
		io_serialPort,
		io_file,
		io_tcp,
		io_udp,
		io_webSocket
	};

	enum IO_STATUS
	{
		io_unknown,
		io_closed,
		io_opened
	};

	enum IO_PACKET_FIFO_MODE
	{
		ioPacket_chunk,
		ioPacket_message
	};

	struct IO_PACKET
	{
		uint8_t *m_pB = NULL;
		int m_nB;
		int m_nBw;

		bool init(int nB)
		{
			m_pB = new uint8_t[nB];
			NULL_F(m_pB);
			m_nB = nB;
			m_nBw = 0;

			return true;
		}

		void release(void)
		{
			DEL(m_pB);
		}

		int set(uint8_t *pB, int nB)
		{
			m_nBw = nB;
			if (m_nBw > m_nB)
				m_nBw = m_nB;

			memcpy(m_pB, pB, m_nBw);

			return m_nBw;
		}
	};

	struct IO_PACKET_FIFO
	{
		IO_PACKET_FIFO();
		~IO_PACKET_FIFO();

		IO_PACKET_FIFO(const IO_PACKET_FIFO &) = delete;
		IO_PACKET_FIFO &operator=(const IO_PACKET_FIFO &) = delete;

		bool init(int nB, int nP,
				  IO_PACKET_FIFO_MODE mode = ioPacket_chunk,
				  size_t nBqueuedMax = 0);
		void release(void);
		void clear(void);

		bool setPacket(const uint8_t *pB, int nB);
		int getPacket(uint8_t *pB, int nB);
		bool getPacket(vector<uint8_t> *pB);

		size_t nQueued(void);
		size_t nBytesQueued(void);
		uint64_t nDropped(void);

		// Retained for console/source compatibility. Access through the
		// synchronized methods above when a consistent value is required.
		int m_nP = 0;
		int m_iPset = 0;
		int m_iPget = 0;

	private:
		deque<vector<uint8_t>> m_qP;
		int m_nB = 0;
		size_t m_nBqueued = 0;
		size_t m_nBqueuedMax = 0;
		uint64_t m_nDropped = 0;
		IO_PACKET_FIFO_MODE m_mode = ioPacket_chunk;
		bool m_bInit = false;
		pthread_mutex_t m_mutex;
	};

	class _IObase : public _ModuleBase
	{
	public:
		_IObase();
		virtual ~_IObase();

		virtual bool init(const json& j);
		virtual bool link(const json& j, ModuleMgr* pM);
		virtual void console(void *pConsole);

		virtual IO_TYPE ioType(void);
		virtual bool open(void);
		virtual bool bOpen(void);
		virtual void close(void);

		virtual bool write(uint8_t *pBuf, int nB);
		virtual int read(uint8_t *pBuf, int nB);

		virtual IO_STATUS getIOstatus(void);
		virtual void setIOstatus(IO_STATUS s);

		virtual IO_PACKET_FIFO* getPacketFIFOw(void);

	protected:
		IO_TYPE m_ioType;
		atomic<IO_STATUS> m_ioStatus;

		IO_PACKET_FIFO m_packetW;
	};

}
#endif
