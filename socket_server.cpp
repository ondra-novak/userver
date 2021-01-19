/*
 * socket_server.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include "socket_server.h"

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cstdlib>
#include <chrono>
#include <mutex>

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
			if (e != nullptr) e = std::current_exception();
		}
	}
	if (fds.empty()) {
		if (e == nullptr) throw std::runtime_error("No addresses to listen");
		else std::rethrow_exception(e);
	}
}

SocketServer::~SocketServer() {
	for (int i: fds) close(i);
}

void SocketServer::stop() {
	exit = true;
	for (int i: fds) shutdown(i, SHUT_RD);
}

std::optional<Socket> SocketServer::waitAccept() {
	sockaddr_storage stor;
	int z = waitForSocket(stor);
	if (z < 0) return std::optional<Socket>();
	else return Socket(z);
}

std::optional<SocketServer::AcceptInfo> SocketServer::waitAcceptGetPeer() {
	sockaddr_storage stor;
	int z = waitForSocket(stor);
	if (z < 0) return std::optional<SocketServer::AcceptInfo>();
	else return SocketServer::AcceptInfo{
		Socket(z), NetAddr::fromSockAddr(reinterpret_cast<sockaddr &>(stor))
	};
}

int SocketServer::waitForSocket(sockaddr_storage &sin) {
	pollfd *pfds = reinterpret_cast<pollfd *>(alloca(sizeof(pollfd)*fds.size()));
	{
		unsigned int idx = 0;
		for (int i: fds) {
			pfds[idx].events = POLLIN;
			pfds[idx].fd = i;
			pfds[idx].revents = 0;
			++idx;
		}
	}
	int r = poll(pfds, fds.size(), -1);
	if (r<0) {
		if (exit) return -1;
		int e =errno;
		throw std::system_error(e, std::generic_category(), "Failed to wait on listening socket");
	}
	for (unsigned int i = 0, cnt = fds.size(); i < cnt; i++) {
		if (pfds[i].revents & POLLIN) {
			socklen_t slen = sizeof(sin);
			int s = accept4(pfds[i].fd, reinterpret_cast<sockaddr *>(&sin),&slen, SOCK_NONBLOCK|SOCK_CLOEXEC);
			return s;
		}
	}
	return -1;
}

class SocketServer::AsyncAcceptor{
public:

	bool asyncAccept(std::shared_ptr<AsyncAcceptor> me, AsyncCallback &&callback, const std::vector<int> &fds);


protected:
	std::mutex lk;
	AsyncCallback curCallback;
	std::vector<int> charged;

	bool isCharged(int i) const;
	void uncharge(int i);
	void charge(int i);
};

bool SocketServer::AsyncAcceptor::asyncAccept(std::shared_ptr<AsyncAcceptor> me, AsyncCallback &&callback, const std::vector<int> &fds) {
	std::unique_lock _(lk);
	auto ap = getCurrentAsyncProvider();
	if (curCallback != nullptr) return false;
	curCallback = std::move(callback);
	for (int i: fds) {
		if (!isCharged(i)) {
			ap->runAsync(AsyncResource(AsyncResource::read, i), [me,i](bool){
				std::unique_lock _(me->lk);
				me->uncharge(i);

				sockaddr_storage sin;
				socklen_t slen = sizeof(sin);
				int s = accept4(i, reinterpret_cast<sockaddr *>(&sin), &slen, SOCK_NONBLOCK|SOCK_CLOEXEC);
				if (s != -1) {
					AsyncCallback cb (std::move(me->curCallback));
					if (cb != nullptr) {
						std::optional<SocketServer::AcceptInfo> ainfo({
								Socket(s), NetAddr::fromSockAddr(reinterpret_cast<sockaddr &>(sin))
						});
						_.unlock();
						cb(ainfo);
					}

				} else {
					int err = errno;
					try {
						throw std::system_error(err, std::generic_category());
					} catch (...) {
						_.unlock();
						AsyncCallback cb (std::move(me->curCallback));
						if (cb != nullptr) {
							std::optional<AcceptInfo> opt;
							cb(opt);
						}
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

inline bool SocketServer::AsyncAcceptor::isCharged(int i) const {
	return std::find(charged.begin(), charged.end(), i) != charged.end();
}

inline void SocketServer::AsyncAcceptor::uncharge(int i) {
	auto x = std::remove(charged.begin(), charged.end(), i);
	charged.erase(x, charged.end());
}

inline void SocketServer::AsyncAcceptor::charge(int i) {
	charged.push_back(i);
}

}
