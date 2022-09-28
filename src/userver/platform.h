#pragma once
#ifndef USERVER_PLATFORM_PCH
#define USERVER_PLATFORM_PCH

#include "platform_def.h"

#ifdef _WIN32 

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include "win_category.h"

namespace userver {


	inline auto &error_category() {return win32_error_category();}

}

#define MSG_DONTWAIT 0
#undef min
#undef max
#undef DELETE

#else

#include <cerrno>
#include <csignal>
#include <system_error>

#include <unistd.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/tcp.h>




namespace userver {

	using SocketHandle = int;
	static constexpr SocketHandle INVALID_SOCKET_HANDLE = -1;

	inline const auto &error_category() {return std::system_category();}
	inline void closesocket(SocketHandle s) {::close(s);}

}

#endif
#endif

