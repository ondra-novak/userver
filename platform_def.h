#pragma once

#ifndef USERVER_PLATFORM_DEF
#define USERVER_PLATFORM_DEF

#ifdef _WIN32 


namespace userver {

#ifdef _WIN64
	using SocketHandle = unsigned __int64;
#else
	using SocketHandle = unsigned int;
#endif

	static constexpr SocketHandle INVALID_SOCKET_HANDLE = ~(SocketHandle)0;
}

struct sockaddr;
struct sockaddr_storage;

using socklen_t = int;
#else


#endif

#endif
