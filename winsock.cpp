#include "platform.h"

#include "winsock.h"
#include <stdexcept>


#pragma comment(lib, "Ws2_32.lib")

namespace userver {

	class WinSockInit {
	public:
		WinSockInit() {
			int err = WSAStartup(MAKEWORD(2, 2), &wsaData);
			if (err != 0) {
				throw std::runtime_error("Failed to initialize WinSock");
			}
		}
		~WinSockInit() {
			WSACleanup();
		}

	protected:
		WSADATA wsaData;


	};


	void initWinsock() {
		static WinSockInit ws;
	}
}