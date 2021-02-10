#pragma once

#ifdef _WIN32 

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


