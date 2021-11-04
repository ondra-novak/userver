/*
 * socket_server.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include "platform.h"

#include "socket_server.h"

#include <cstdlib>
#include <chrono>
#include <mutex>
#include <iterator>

#include "async_provider.h"
#include "async_resource.h"

namespace userver {

SocketServer::SocketServer(const NetAddrList &addrLst) {
	fds.reserve(addrLst.size());
	std::exception_ptr e;
	for (const auto &a : addrLst) {
		try {
			fds.push_back(a.listen());
		} catch (...) {
			if (e == nullptr) e = std::current_exception();
		}
	}
	if (fds.empty()) {
		if (e == nullptr) throw std::runtime_error("No addresses to listen");
		else std::rethrow_exception(e);
	}
}

SocketServer::~SocketServer() {
	for (auto i: fds) closesocket(i);
}

void SocketServer::stop() {
	exit = true;
#ifdef _WIN32
	for (auto i : fds) shutdown(i, SD_RECEIVE);
#else
	for (auto i: fds) shutdown(i, SHUT_RD);
#endif
}

std::optional<Socket> SocketServer::waitAccept() {
	sockaddr_storage stor;
	auto z = waitForSocket(stor);
	if (z < 0) return std::optional<Socket>();
	else return Socket(z);
}

std::optional<SocketServer::AcceptInfo> SocketServer::waitAcceptGetPeer() {
	sockaddr_storage stor;
	auto z = waitForSocket(stor);
	if (z < 0) return std::optional<SocketServer::AcceptInfo>();
	else return SocketServer::AcceptInfo{
		Socket(z), NetAddr::fromSockAddr(reinterpret_cast<sockaddr &>(stor))
	};
}

static SocketHandle acceptConn(SocketHandle src, sockaddr *sin, socklen_t *slen) {
#ifdef _WIN32
	SocketHandle s = accept(src, sin, slen);
	if (s == SOCKET_ERROR) {
		throw std::system_error(WSAGetLastError(), error_category(),"Accept");
	}
	u_long one = 1;
	ioctlsocket(s, FIONBIO, &one);
	return s;
#else
	SocketHandle s= accept4(src, sin,slen, SOCK_NONBLOCK|SOCK_CLOEXEC);
	if (s < 0) {
		int e = errno;
		throw std::system_error(e, error_category(),"Accept");
	}
	return s;
#endif
}

SocketHandle SocketServer::waitForSocket(sockaddr_storage &sin) {
	std::basic_string<pollfd> pfds;
	std::transform(fds.begin(), fds.end(), std::back_insert_iterator(pfds), [](SocketHandle i) {
		return pollfd{ i,POLLIN,0 };
	});
#ifdef _WIN32
	int r = WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), -1);
	if (r < 0) {
		if (exit) return INVALID_SOCKET_HANDLE;
		int e = WSAGetLastError();
#else
	int r = poll(pfds.data(), pfds.size(), -1);
	if (r<0) {
		if (exit) return INVALID_SOCKET_HANDLE;
		int e =errno;
#endif
		throw std::system_error(e, error_category(), "Failed to wait on listening socket");
	}
	for (auto &fd: pfds) {
		if (fd.revents & POLLIN) {
			socklen_t slen = sizeof(sin);
			try {
				return acceptConn(fd.fd, reinterpret_cast<sockaddr *>(&sin),&slen);
			} catch (...) {
				if (exit) return INVALID_SOCKET_HANDLE;
			}
		}
	}
	return INVALID_SOCKET_HANDLE;
}

class SocketServer::AsyncAcceptor{
public:

	bool asyncAccept(std::shared_ptr<AsyncAcceptor> me, AsyncCallback &&callback, const std::vector<SocketHandle> &fds);


protected:
	std::mutex lk;
	AsyncCallback curCallback;
	std::vector<SocketHandle> charged; //already charged descriptors, to avoid changing repeatedly
	std::vector<AcceptInfo> ready;    //ready connections arrived when no callback was defined - will be returned immediatelly

	bool isCharged(SocketHandle i) const;
	void uncharge(SocketHandle i);
	void charge(SocketHandle i);
};

bool SocketServer::AsyncAcceptor::asyncAccept(std::shared_ptr<AsyncAcceptor> me, AsyncCallback &&callback, const std::vector<SocketHandle> &fds) {
	std::unique_lock _(lk);
	if (!ready.empty()) {
		std::optional<SocketServer::AcceptInfo> ainfo(std::move(ready.back()));
		ready.pop_back();
		_.unlock();
		callback(ainfo);
		return true;
	}
	auto ap = getCurrentAsyncProvider();
	if (curCallback != nullptr) return false;
	curCallback = std::move(callback);
	for (SocketHandle i: fds) {
		if (!isCharged(i)) {
			ap->runAsync(AsyncResource(AsyncResource::read, i), [me,i](bool){
				std::unique_lock _(me->lk);
				me->uncharge(i);

				sockaddr_storage sin;
				socklen_t slen = sizeof(sin);
				try {
					SocketHandle s = acceptConn(i, reinterpret_cast<sockaddr *>(&sin), &slen);
					AsyncCallback cb (std::move(me->curCallback));
					if (cb != nullptr) {
						std::optional<SocketServer::AcceptInfo> ainfo({
							Socket(s), NetAddr::fromSockAddr(reinterpret_cast<sockaddr &>(sin))
						});
						_.unlock();
						cb(ainfo);
					} else {
						me->ready.push_back({
							Socket(s), NetAddr::fromSockAddr(reinterpret_cast<sockaddr &>(sin))
						});
					}
				} catch (...) {
					_.unlock();
					AsyncCallback cb (std::move(me->curCallback));
					if (cb != nullptr) {
						std::optional<AcceptInfo> opt;
						cb(opt);

					}
				}

			}, std::chrono::system_clock::time_point::max());
			charge(i);
		}
	}

	return true;

}

bool SocketServer::waitAcceptAsync(AsyncCallback &&callback) {
	if (exit) return false;

	if (asyncState == nullptr) asyncState = std::make_shared<AsyncAcceptor>();
	return asyncState->asyncAccept(asyncState, std::move(callback), fds);
	return true;
}

inline bool SocketServer::AsyncAcceptor::isCharged(SocketHandle i) const {
	return std::find(charged.begin(), charged.end(), i) != charged.end();
}

inline void SocketServer::AsyncAcceptor::uncharge(SocketHandle i) {
	auto x = std::remove(charged.begin(), charged.end(), i);
	charged.erase(x, charged.end());
}

inline void SocketServer::AsyncAcceptor::charge(SocketHandle i) {
	charged.push_back(i);
}

}
