/*
 * init.cpp
 *
 *  Created on: 12. 2. 2021
 *      Author: ondra
 */




#include "platform.h"
#include <stdexcept>

#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

namespace userver {
#ifdef _WIN32

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


	void initNetwork() {
		static WinSockInit ws;
	}
}
#else

class OneTimeInit {
public:
	OneTimeInit() {
		signal(SIGPIPE, SIG_IGN);
	}
};

void initNetwork() {
	static OneTimeInit oti;
}

}


#endif
