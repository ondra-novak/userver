/*
 * dispatcher.cpp
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */

#include "platform.h"
#include "dispatcher.h"
#include "socketresource.h"
#include <fcntl.h>

namespace userver {

Dispatcher::Dispatcher()
:stopped(false),intr(false)
{
#ifdef _WIN32
	intr_r = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	waiting.push_back({ intr_r,POLLIN,0 });
	next_timeout = std::chrono::system_clock::time_point::max();
	regs.push_back(Reg(nullptr, next_timeout));

	sockaddr_in cursin;
	cursin.sin_family = AF_INET;
	cursin.sin_addr.S_un.S_un_b = { 127,0,0,1 };
	cursin.sin_port = 0;
	if (bind(intr_r, reinterpret_cast<const sockaddr*>(&cursin), sizeof(cursin))) {
		throw std::system_error(WSAGetLastError(), win32_error_category(), "bind on intr socket");
	}

	sockaddr_storage sin;
	int sinlen = sizeof(sin);
	int e = getsockname(intr_r, reinterpret_cast<sockaddr*>(&sin), &sinlen);
	if (e) {
		throw std::system_error(WSAGetLastError(), error_category(), "getsockname on intr socket");
	}
	thisAddr = NetAddr::fromSockAddr(*reinterpret_cast<sockaddr*>(&sin));

#else
	int fds[2];
	if (::pipe2(fds, O_CLOEXEC|O_NONBLOCK)<0) {
		throw std::system_error(errno, error_category());
	}
	intr_r = fds[0];
	intr_w = fds[1];
	waiting.push_back({intr_r,POLLIN,0});
	next_timeout = std::chrono::system_clock::time_point::max();
	regs.push_back(Reg(Callback(), next_timeout));
#endif
}

Dispatcher::~Dispatcher() {
#ifdef _WIN32
	closesocket(intr_r);
#else
	close(intr_r);
	close(intr_w);
#endif
}

bool Dispatcher::waitAsync(IAsyncResource &&resource,  Callback &&cb, std::chrono::system_clock::time_point timeout) {
    if (typeid(resource) == typeid(SocketResource)) {
       const SocketResource &res = static_cast<const SocketResource &>(resource);
       switch (res.op) {
           default:
           case SocketResource::read: waitRead(res.socket, std::move(cb), timeout);break;
           case SocketResource::write: waitWrite(res.socket, std::move(cb), timeout);break;
       }
       return true;
    } else {
       return false;
    }

}

void Dispatcher::waitRead(SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) {
	waitEvent(POLLIN, socket, std::move(cb), timeout);
}

void Dispatcher::waitWrite(SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) {
	waitEvent(POLLOUT, socket, std::move(cb), timeout);
}

void Dispatcher::interrupt() {
    if (!intr.exchange(true))  {
        notify();
    }
}

void Dispatcher::notify() {
	char b = 1;
#ifdef _WIN32
	int r = sendto(intr_r, &b, 1, 0, thisAddr->getAddr(), thisAddr->getAddrLen());
	if (r < 0) {
		int e = WSAGetLastError();
		throw std::system_error(e, error_category());
#else
	if (!::write(intr_w, &b, 1)) {
		int e = errno;
		if (e != EWOULDBLOCK) {
			throw std::system_error(e, error_category());
		}
#endif
		
	}
}

void Dispatcher::waitEvent(int event, SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) {
	std::unique_lock _(lk);
	if (stopped) return;
	new_waiting.push_back({socket, static_cast<short >(event), 0});
	new_regs.push_back(Reg(std::move(cb), timeout));
	notify();
}

void Dispatcher::stop() {
	std::unique_lock _(lk);
	stopped = true;
	notify();
	for (auto &c: regs) {
		c.cb.reset();
	}
	for (auto &c: new_regs) {
		c.cb.reset();
	}

}

void Dispatcher::removeItem(std::size_t idx) {
	auto wsz = waiting.size() - 1;
	if (idx != wsz) {
		std::swap(regs[idx], regs[wsz]);
		std::swap(waiting[idx], waiting[wsz]);
	}
	waiting.pop_back();
	regs.pop_back();
}

Dispatcher::Task Dispatcher::getTask() {
    if (stopped) return Task();
	while (!intr.exchange(false)) {
		auto now = std::chrono::system_clock::now();
		if (lastIdx >= waiting.size()) {
			int wait_tm;
			if (now > next_timeout) wait_tm = 0;
			else if (next_timeout == std::chrono::system_clock::time_point::max()) wait_tm = -1;
			else {
					auto z = std::chrono::duration_cast<std::chrono::milliseconds>(next_timeout - now).count();
					if (z > std::numeric_limits<int>::max()) wait_tm = std::numeric_limits<int>::max();
					else wait_tm = static_cast<int>(z);
				}
#ifdef _WIN32
			int r = WSAPoll(waiting.data(), static_cast<ULONG>(waiting.size()), wait_tm);
			if (r < 0) {
				int e = WSAGetLastError();
				if (e == WSAEINTR) continue;
				throw std::system_error(e, win32_error_category());
			}
#else
			int r = poll(waiting.data(),waiting.size(), wait_tm);
			if (r < 0) {
				int e = errno;
				if (e == EINTR) continue;
				throw std::system_error(e,std::generic_category());
			}
#endif
			lastIdx = 0;
			now = std::chrono::system_clock::now();
		}
		next_timeout = std::chrono::system_clock::time_point::max();
		while (lastIdx < waiting.size()) {
			auto idx = lastIdx;
			if (waiting[idx].revents) {
				if (idx == 0) {
					waiting[idx].revents = 0;
					waiting[idx].events = POLLIN;
					std::unique_lock _(lk);
					char buff[100];
#ifdef _WIN32
					if (::recv(waiting[idx].fd, buff, 100,0) < 0) {
						throw std::system_error(WSAGetLastError(), error_category());
					}
#else
					if (::read(waiting[idx].fd, buff, 100)<0) {
						throw std::system_error(errno, error_category());
					}
#endif
					++lastIdx;
					if (stopped) {
						waiting.clear();
						regs.clear();
						new_waiting.clear();
						new_regs.clear();
						return Task();
					}

					while (!new_waiting.empty()) {
						waiting.push_back(std::move(new_waiting.back()));
						regs.push_back(std::move(new_regs.back()));
						new_waiting.pop_back();
						new_regs.pop_back();
					}
				} else {
					Task ret (std::move(regs[idx].cb), true);
					removeItem(idx);
					return ret;
				}
			} else if (regs[idx].timeout < now || waiting[idx].events == 0) {
				Task ret (std::move(regs[idx].cb), false);
				removeItem(idx);
				return ret;
			} else {
				next_timeout = std::min(next_timeout, regs[idx].timeout);
				++lastIdx;
			}
		}
	}
	return Task();
}

Dispatcher::Reg::Reg(Callback &&cb, std::chrono::system_clock::time_point timeout)
	:cb(std::move(cb)),timeout(timeout)
{

}

Dispatcher::Callback Dispatcher::stopWait(IAsyncResource &&resource) {
    std::unique_lock _(lk);

    if (typeid(resource) == typeid(SocketResource)) {
       const SocketResource &res = static_cast<const SocketResource &>(resource);
       switch (res.op) {
           case SocketResource::read: return disarmEvent(POLLIN, res.socket);break;
           case SocketResource::write: return disarmEvent(POLLOUT, res.socket);break;
       }
    }
    return Callback();


}

/* Disarm strategy
 *
 * Acquire the lock and try to find the socket and the event. We search in the list of
 *  current waiting sockets and then in the lost of newly added sockets not yet processed.
 *  It is easier to disarm newly added socket, because it only needs to remove it from the list.
 *  However for currently waiting socket, we cannot delete the registration. We can move the
 *  callback out and then set the timeout to 'now' and after that notify() is called
 *  to restart waiting. The socket registration will be processed as timeouted, removed,
 *  but without the callback, nothing will be executed.
 *
 */
Dispatcher::Callback Dispatcher::disarmEvent(int event, SocketHandle socket) {
    Callback cb_to_call;

    std::unique_lock _(lk);
    if (stopped) return Callback();
    auto itr = std::find_if(waiting.begin(), waiting.end(), [&](const pollfd &fd) {
        return fd.fd == socket && (fd.events & event) != 0;
    });
    if (itr == waiting.end()) {
        itr = std::find_if(new_waiting.begin(), new_waiting.end(), [&](const pollfd &fd) {
            return fd.fd == socket && (fd.events & event) != 0;
        });
        if (itr == waiting.end()) return Callback();
        auto idx = std::distance(new_waiting.begin(), itr);
        cb_to_call = std::move(new_regs[idx].cb);
        new_regs.erase(new_regs.begin()+idx);
        new_waiting.erase(itr);
    } else {
        auto idx = std::distance(waiting.begin(), itr);
        cb_to_call = std::move(regs[idx].cb);
        regs[idx].timeout = std::chrono::system_clock::now();
        notify();
    }

    _.unlock();

    return cb_to_call;
}

}


