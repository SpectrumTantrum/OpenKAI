/*
 * _WebSocket.cpp
 *
 *  Created on: August 8, 2016
 *      Author: yankai
 */

#include "_WebSocket.h"

namespace kai
{

	_WebSocket::_WebSocket()
	{
		m_ioType = io_webSocket;
		m_ioStatus = io_unknown;
	}

	_WebSocket::~_WebSocket()
	{
		close();
		m_packetR.release();
	}

	bool _WebSocket::init(const json &j)
	{
		IF_F(!this->_IObase::init(j));

		int nPacket = 1024;
		int nMessageMax = 1024 * 1024;
		int nQueueBytesMax = 4 * 1024 * 1024;
		jKv(j, "nPacket", nPacket);
		jKv(j, "nMessageMax", nMessageMax);
		jKv(j, "nQueueBytesMax", nQueueBytesMax);

		IF_F(nMessageMax <= 0);
		IF_F(nQueueBytesMax <= 0);

		IF_F(!m_packetW.init(nMessageMax, nPacket, ioPacket_message,
							 static_cast<size_t>(nQueueBytesMax)));
		IF_F(!m_packetR.init(nMessageMax, nPacket, ioPacket_message,
							 static_cast<size_t>(nQueueBytesMax)));

		return true;
	}

	int _WebSocket::read(uint8_t *pBuf, int nB)
	{
		NULL__(pBuf, 0);

		return m_packetR.getPacket(pBuf, nB);
	}

	IO_PACKET_FIFO *_WebSocket::getPacketFIFOr(void)
	{
		return &m_packetR;
	}

	void _WebSocket::console(void *pConsole)
	{
		NULL_(pConsole);
		this->_IObase::console(pConsole);
	}

}
