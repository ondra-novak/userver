#pragma once
#ifndef USERVER_PLATFORM_PCH
#define USERVER_PLATFORM_PCH

#ifdef _WIN32 

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>

namespace userver {

	using SocketHandle = SOCKET;

}

#undef min
#undef max
using socklen_t = int;
#define MSG_DONTWAIT 0
#undef DELETE


#else

#include <errno.h>
#include <poll.h>

#include <sys/socket.h>
#include <unistd.h>
#include <csignal>

namespace userver {

	using SocketHandle = int;

}

#endif
#endif

