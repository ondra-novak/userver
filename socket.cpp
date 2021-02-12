/*
 * socket.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include "platform.h"
#include <cstring>
#include <sstream>

#include "async_provider.h"
#include "async_resource.h"
#include "netaddr.h"

#include "socket.h"

namespace userver {

Socket::Socket() {}

Socket::Socket(Socket &&other)
:s(other.s), readtm(other.readtm), writetm(other.writetm), tm(other.tm)
{
	other.s = -1;
	signal(SIGPIPE, SIG_IGN);
}

Socket& Socket::operator =(Socket &&other) {
	if (s != -1) close(s);
	s = other.s;
	other.s = -1;
	return *this;
}

static void error(int errnr, const char *desc) {
	std::ostringstream bld;
	bld<<"Network error: " << strerror(errnr) << " - " << desc;
	throw std::system_error(errnr, std::generic_category(), bld.str());
}


int Socket::read(void *buffer, std::size_t size) {
	int r = recv(s, buffer,size,0);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			pollfd pfd = {s, POLLIN, 0};
			int r = poll(&pfd, 1, readtm);
			if (r <= 0) {
				tm = true;
				return 0;
			}
			r = recv(s, buffer,size,0);
			if (r < 0) error(errno,"socket read()");
		} else {
			error(err,"socket read()");
		}
	}
	return r;
}

int Socket::write(const void *buffer, std::size_t size) {
	int r = send(s, buffer, size,0);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			pollfd pfd = {s, POLLOUT, 0};
			int r = poll(&pfd, 1, writetm);
			if (r <= 0) {
				tm = true;
				return 0;
			}
			r = send(s, buffer,size, 0);
			if (r < 0) error(errno,"socket write()");
		} else {
			error(err,"socket write()");
		}
	}
	return r;
}

void Socket::closeOutput() {
	shutdown(s, SHUT_WR);
}

void Socket::closeInput() {
	shutdown(s, SHUT_RD);
}

void Socket::setRdTimeout(int tm) {
	readtm = tm;
}

void Socket::setWrTimeout(int tm) {
	writetm = tm;
}

void Socket::setIOTimeout(int tm) {
	readtm = writetm = tm;
}

int Socket::getRdTimeout() const {
	return readtm;
}

int Socket::getWrTimeout() const {
	return writetm;
}

Socket Socket::connect(const NetAddr &addr) {
	return Socket(addr.connect());
}

Socket::~Socket() {
	if (s != -1) close(s);
}


bool Socket::timeouted() const {
	return tm;
}

Socket::Socket(SocketHandle s):s(s) {
}

void Socket::read(void *buffer, std::size_t size, CallbackT<void(int)> &&fn) {
	int r = recv(s, buffer,size,0);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			getCurrentAsyncProvider().runAsync(AsyncResource(AsyncResource::read, s), [this, buffer, size, fn = std::move(fn)](bool b){
				if (b) {
					tm = true;
					fn(0);
				} else {
					fn(read(buffer, size));
				}
			}, readtm<0?std::chrono::system_clock::time_point::max()
					 :std::chrono::system_clock::now()+std::chrono::milliseconds(readtm));
		} else {
			error(err,"socket read()");
		}
	} else {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn),r]{
			fn(r);
		});
	}
}

void Socket::write(const void *buffer, std::size_t size, CallbackT<void(int)> &&fn) {
	int r = send(s, buffer, size,0);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			getCurrentAsyncProvider().runAsync(AsyncResource(AsyncResource::write, s), [this, buffer, size, fn = std::move(fn)](bool b){
				if (b) {
					tm = true;
					fn(0);
				} else {
					fn(write(buffer, size));
				}
			}, std::chrono::system_clock::now()+std::chrono::milliseconds(writetm));
		} else {
			try {
				error(err,"socket write()");
			} catch (...) {
				fn(-1);
			}
		}
	} else {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn),r]{
			fn(r);
		});
	}
}

bool Socket::checkSocketState() const {
	int e = 0;
	socklen_t len = sizeof(e);
	int r = getsockopt(s, SOL_SOCKET, SO_ERROR, &e, &len);
	if (r < 0) {
		int e = errno;
		throw std::system_error(e, std::generic_category(), "waitConnect,getsockopt");
	} else {
		if (e) {
			errno = e;
			return false;
		} else {
			return true;
		}
	}
}

bool Socket::waitConnect(int tm)  {
	pollfd pfd = {s,POLLOUT,0};
	int r = poll(&pfd,1,tm);
	if (r < 0) {
		int e = errno;
		throw std::system_error(e, std::generic_category(), "waitConnect,poll");
	} else if (r == 0) {
		return false;
	} else  {
		return checkSocketState();
	}
}

void Socket::waitConnect(int tm, CallbackT<void(bool)> &&cb)  {
	getCurrentAsyncProvider()->runAsync(AsyncResource(AsyncResource::write, s),
			[this, cb = std::move(cb)](bool timeouted) {
				cb(!timeouted && checkSocketState());
			}, tm<0?std::chrono::system_clock::time_point::max()
					:std::chrono::system_clock::now()+std::chrono::milliseconds(tm));
}

}
