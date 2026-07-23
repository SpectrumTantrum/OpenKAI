#include "../../src/IO/_WebSocketServer.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace
{
	std::string makeJsonMessage(size_t nBytes)
	{
		const std::string prefix = "{\"payload\":\"";
		const std::string suffix = "\"}";

		if (nBytes < prefix.size() + suffix.size())
			return "";

		return prefix + std::string(nBytes - prefix.size() - suffix.size(), 'x') + suffix;
	}
}

int main(int argc, char **argv)
{
	if (argc != 2)
	{
		std::cerr << "usage: websocket_message_server_test <port>" << std::endl;
		return 2;
	}

	const int port = std::atoi(argv[1]);
	if (port <= 0 || port > 65535)
		return 2;

	json j = json::object();
	j["name"] = "WebSocketMessageTest";
	j["class"] = "_WebSocketServer";
	j["host"] = "127.0.0.1";
	j["port"] = port;
	j["wsMode"] = kai::wsSocket_txt_bcast;
	j["nClientMax"] = 2;
	j["nPacket"] = 16;
	j["nMessageMax"] = 8192;
	j["nQueueBytesMax"] = 32768;
	j["thread"] = {{"FPS", 100}};
	j["threadR"] = {{"FPS", 100}};

	kai::_WebSocketServer server;
	if (!server.init(j) || !server.start())
		return 3;

	const uint64_t tDeadline = kai::getApproxTbootUs() + 5 * SEC_2_USEC;
	while (server.nClient() < 2 && kai::getApproxTbootUs() < tDeadline)
		usleep(1000);

	if (server.nClient() != 2)
		return 4;

	const size_t sizes[] = {511, 512, 513, 4096};
	for (size_t nBytes : sizes)
	{
		std::string msg = makeJsonMessage(nBytes);
		if (msg.size() != nBytes)
			return 5;

		if (!server.write(reinterpret_cast<uint8_t *>(&msg[0]),
						  static_cast<int>(msg.size())))
			return 6;
	}

	for (size_t nBytes : sizes)
	{
		const std::string expected = makeJsonMessage(nBytes) + "EOJ";
		std::string received;
		const uint64_t tReadDeadline = kai::getApproxTbootUs() + 5 * SEC_2_USEC;
		while (received.size() < expected.size() &&
			   kai::getApproxTbootUs() < tReadDeadline)
		{
			uint8_t buffer[512];
			const int nRead = server.read(buffer, sizeof(buffer));
			if (nRead < 0)
				return 7;
			if (nRead == 0)
			{
				usleep(1000);
				continue;
			}
			received.append(reinterpret_cast<const char *>(buffer), nRead);
		}

		if (received != expected)
			return 8;
	}

	return 0;
}
