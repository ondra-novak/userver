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
#include "win_category.h"

#include "socket.h"

namespace userver {

	Socket::Socket() {}

	Socket::Socket(Socket&& other)
		:s(other.s), readtm(other.readtm), writetm(other.writetm), tm(other.tm)
	{
		other.s = -1;
	}

	Socket& Socket::operator =(Socket&& other) {
		if (s != -1) closesocket(s);
		s = other.s;
		other.s = -1;
		return *this;
	}

	static void error(int errnr, const char* desc) {
		std::ostringstream bld;
		bld << "Network error: " << desc;
		throw std::system_error(errnr, win32_error_category(), bld.str());
	}


	int Socket::read(void* buffer, std::size_t size) {
		int r = recv(s, reinterpret_cast<char *>(buffer), static_cast<unsigned int>(size), 0);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				pollfd pfd = { s, POLLIN, 0 };
				int r = WSAPoll(&pfd, 1, readtm);
				if (r <= 0) {
					tm = true;
					return 0;
				}
				r = recv(s, reinterpret_cast<char *>(buffer), static_cast<unsigned int>(size), 0);
				if (r < 0) error(WSAGetLastError(), "socket read()");
			}
			else {
				error(err, "socket read()");
			}
		}
		return r;
	}

	int Socket::write(const void* buffer, std::size_t size) {
		int r = send(s, reinterpret_cast<const char *>(buffer), static_cast<unsigned int>(size), 0);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				pollfd pfd = { s, POLLOUT, 0 };
				int r = WSAPoll(&pfd, 1, writetm);
				if (r <= 0) {
					tm = true;
					return 0;
				}
				r = send(s, reinterpret_cast<const char*>(buffer), static_cast<unsigned int>(size), 0);
				if (r < 0) error(WSAGetLastError(), "socket write()");
			}
			else {
				error(err, "socket write()");
			}
		}
		return r;
	}

	void Socket::closeOutput() {
		shutdown(s, SD_SEND);
	}

	void Socket::closeInput() {
		shutdown(s, SD_RECEIVE);
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

	Socket Socket::connect(const NetAddr& addr) {
		return Socket(addr.connect());
	}

	Socket::~Socket() {
		if (s != -1) closesocket(s);
	}


	bool Socket::timeouted() const {
		return tm;
	}

	Socket::Socket(SocketHandle s) :s(s) {
	}

	void Socket::read(void* buffer, std::size_t size, CallbackT<void(int)>&& fn) {
		int r = recv(s, reinterpret_cast<char*>(buffer), static_cast<unsigned int>(size), 0);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				getCurrentAsyncProvider().runAsync(AsyncResource(AsyncResource::read, s), [this, buffer, size, fn = std::move(fn)](bool succ) mutable {
					if (!succ) {
						this->tm = true;							
						fn(0);
					}
					else {
						fn(read(buffer, size));
					}
				}, readtm < 0 ? std::chrono::system_clock::time_point::max()
					: std::chrono::system_clock::now() + std::chrono::milliseconds(readtm));
			}
			else {
				error(err, "socket read()");
			}
		}
		else {
			getCurrentAsyncProvider().runAsync([fn = std::move(fn), r]{
				fn(r);
				});
		}
	}

	void Socket::write(const void* buffer, std::size_t size, CallbackT<void(int)>&& fn) {
		int r = send(s, reinterpret_cast<const char*>(buffer), static_cast<unsigned int>(size), 0);
		if (r < 0) {
			int err = WSAGetLastError();
			if (err == WSAEWOULDBLOCK) {
				getCurrentAsyncProvider().runAsync(AsyncResource(AsyncResource::write, s), [this, buffer, size, fn = std::move(fn)](bool succ){
					if (!succ) {
						this->tm = true;
						fn(0);
					}
					else {
						fn(write(buffer, size));
					}
				}, std::chrono::system_clock::now() + std::chrono::milliseconds(writetm));
			}
			else {
				try {
					error(err, "socket write()");
				}
				catch (...) {
					fn(-1);
				}
			}
		}
		else {
			getCurrentAsyncProvider().runAsync([fn = std::move(fn), r]{
				fn(r);
				});
		}
	}

	bool Socket::checkSocketState() const {
		int e = 0;
		socklen_t len = sizeof(e);
		int r = getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&e), &len);
		if (r < 0) {
			int e = WSAGetLastError();
			throw std::system_error(e, win32_error_category(), "waitConnect,getsockopt");
		}
		else {
			if (e) {
				WSASetLastError(e);
				return false;
			}
			else {
				return true;
			}
		}
	}

	bool Socket::waitConnect(int tm) {
		fd_set wrset, errset;
		FD_ZERO(&wrset);
		FD_ZERO(&errset);
		FD_SET(s, &wrset);
		FD_SET(s, &errset);

		timeval tmout = { tm / 1000,(tm % 1000) * 1000 };
		int r = select(1, nullptr, &wrset, &errset, &tmout);

		if (r < 0) {
			int e = WSAGetLastError();
			throw std::system_error(e, win32_error_category(), "waitConnect,select");
		}
		else if (r == 0) {
			return false;
		}
		else {
			if (FD_ISSET(s, &errset)) return false;
			return true;
		}
	}

	void Socket::waitConnect(int tm, CallbackT<void(bool)>&& cb) {
		auto now = std::chrono::system_clock::now();
		auto checkTime = tm < 0 || tm > 1000 ? now + std::chrono::seconds(1) : now + std::chrono::milliseconds(tm);
		getCurrentAsyncProvider()->runAsync(AsyncResource(AsyncResource::write, s),
			[this, cb = std::move(cb), tm](bool succ) mutable {


			if (succ) {
				cb(checkSocketState());
			}
			else {
				fd_set errset;
				FD_ZERO(&errset);
				FD_SET(s, &errset);
				timeval tmout = { 0,0 };
				int r = select(1, nullptr, nullptr, &errset, &tmout);
				if (FD_ISSET(s, &errset)) {
					cb(false);
				}
				else if (tm < 1000) {
					cb(false);
				}
				else {
					waitConnect(tm - 1000, std::move(cb));
				}
			}
		}, checkTime);
	}

}
