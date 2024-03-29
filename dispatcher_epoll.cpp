/*
 * dispatcher_epoll.cpp
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */
#include "socketresource.h"

#ifndef _WIN32

#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <sys/epoll.h>
#include "socketresource.h"
#include "dispatcher_epoll.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <iostream>
namespace userver {



Dispatcher_EPoll::Dispatcher_EPoll() {
	stopped.store(false);
	epoll_fd = epoll_create1(EPOLL_CLOEXEC);
	if (epoll_fd < 0) {
		int e = errno;
		throw std::system_error(e,std::generic_category(), "epoll_create1");
	}

	int fd[2];
	int r = pipe2(fd, O_CLOEXEC);
	if (r <0)  {
		int e = errno;
		::close(epoll_fd);
		throw std::system_error(e,std::generic_category(), "socket/notify");
	}
	event_fd=fd[0];
	::close(fd[1]);
}

Dispatcher_EPoll::~Dispatcher_EPoll() {
	::close(event_fd);
	::close(epoll_fd);
}

bool Dispatcher_EPoll::waitAsync(IAsyncResource &&resource,  Callback &&cb, std::chrono::system_clock::time_point timeout) {
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

void Dispatcher_EPoll::waitRead(int socket, Callback &&cb,
		std::chrono::system_clock::time_point timeout) {
	regWait(socket,Op::read , std::move(cb), timeout);
}

void Dispatcher_EPoll::waitWrite(int socket, Callback &&cb,
		std::chrono::system_clock::time_point timeout) {
	regWait(socket,Op::write, std::move(cb), timeout);
}


void Dispatcher_EPoll::interrupt() {
    if (!intr.exchange(true)) {
        notify();
    }
}


void Dispatcher_EPoll::stop() {
	if (!stopped.exchange(true)) {
		notify();
	}
	std::lock_guard _(lock);
	for (auto &c:fd_map) {
		for (auto &x:c.second) {
			x.cb.reset();
		}
	}


}

void Dispatcher_EPoll::regImmCall(Callback &&cb) {
	std::lock_guard _(lock);
	imm_calls.push(std::move(cb));
	notify();
}

void Dispatcher_EPoll::regWait(int socket, Op op, Callback &&cb, std::chrono::system_clock::time_point timeout) {
	std::lock_guard _(lock);
	RegList &lst = fd_map[socket];
	bool first = lst.empty();
	lst.push_back(Reg{
		timeout, op, std::move(cb)
	});
	rearm_fd(first, socket, lst);
	notify();
}

void Dispatcher_EPoll::notify() {
	epoll_event ev ={};
	ev.events = EPOLLIN|EPOLLONESHOT;
	ev.data.fd = event_fd;
	int r = epoll_ctl(epoll_fd, EPOLL_CTL_MOD, event_fd, &ev);
	if (r < 0) {
		int e = errno;
		if (e == ENOENT) {
			r = epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev);
			if (r < 0) {
				int e = errno;
				throw std::system_error(e, std::generic_category(), "notify()");
			}
		} else {
			throw std::system_error(e, std::generic_category(), "notify_mod()");
		}
	}
}

Dispatcher_EPoll::Task Dispatcher_EPoll::getTask() {
	if (!stopped.load()) {
		int r;
		std::unique_lock mx(lock, std::defer_lock);
		epoll_event ev;

		do {
			mx.lock();
			if (!imm_calls.empty()) {
				Task t(std::move(imm_calls.front()), false);
				imm_calls.pop();
				return t;
			}
			auto tm = getWaitTime();
			mx.unlock();
			r = epoll_wait(epoll_fd, &ev, 1, tm);;
			if (r < 0) {
				int e = errno;
				if (e != EINTR) {
					throw std::system_error(e, std::generic_category(), "epoll_wait");
				}
			}
		} while (r < 0);

		mx.lock();

		if (r == 0) {
			auto now = std::chrono::system_clock::now();
			//check for timeouts;
			int tmfd = getTmFd();
			RegList &regs = fd_map[tmfd];
			auto itr = std::find_if(regs.begin(), regs.end(), [&](const Reg &rg) {
				return rg.timeout <= now;
			});
			if (itr != regs.end()) {
				Task tsk(std::move(itr->cb), false);
				regs.erase(itr);
				rearm_fd(false, tmfd, regs);
				return tsk;
			}
		} else {
			int fd = ev.data.fd;
			if (fd != event_fd) {
				RegList &regs = fd_map[fd];

				auto iter = regs.begin();

				if (ev.events & (EPOLLIN|EPOLLHUP)) {
					iter = std::find_if(regs.begin(), regs.end(), [&](const Reg &rg){
							return rg.op == Op::read;
					});
				} else if (ev.events & (EPOLLOUT|EPOLLERR)) {
					iter = std::find_if(regs.begin(), regs.end(), [&](const Reg &rg){
							return rg.op == Op::write;
					});
				}
				if (iter == regs.end() && (ev.events & EPOLLERR)) {
					iter = regs.end();
				}


				if (iter != regs.end()) {
					Task tsk(std::move(iter->cb), true);
					regs.erase(iter);
					rearm_fd(false, fd, regs);
					return tsk;
				}
			}
		}
	}
	return Task();
}

void Dispatcher_EPoll::rearm_fd(bool first_call, int socket, RegList &lst) {
	epoll_event ev ={};
	ev.events = 0;
	ev.data.fd = socket;
	const std::chrono::system_clock::time_point maxtm = std::chrono::system_clock::time_point::max();
	tm_map.erase(TimeoutKey(lst.timeout, socket));
	lst.timeout = std::chrono::system_clock::time_point::max();

	if (!lst.empty()) {
		for (const auto &x: lst) {
			lst.timeout = std::min(lst.timeout, x.timeout);
			switch (x.op) {
				case Op::read: ev.events |= EPOLLIN|EPOLLRDHUP; break;
				case Op::write: ev.events |= EPOLLOUT; break;
			}
		}
		ev.events |= (EPOLLONESHOT|EPOLLERR);
		int r =epoll_ctl(epoll_fd, first_call?EPOLL_CTL_ADD:EPOLL_CTL_MOD, socket, &ev);
		if (r < 0) {
			int e = errno;
			throw std::system_error(e, std::generic_category(), "epoll_ctl");
		}

		if (lst.timeout != maxtm) {
			tm_map.insert({
				lst.timeout, socket
			});
		}
	} else {
		epoll_ctl(epoll_fd, EPOLL_CTL_DEL, socket, &ev);
		fd_map.erase(socket);
	}
}

int Dispatcher_EPoll::getWaitTime() const {
	auto now = std::chrono::system_clock::now();
	auto iter = tm_map.begin();
	if (iter == tm_map.end()) return -1;
	auto tm = iter->first;
	auto dist = std::chrono::duration_cast<std::chrono::milliseconds>(tm - now).count();
	if (dist <0) dist = 0;
	return dist;

}

int Dispatcher_EPoll::getTmFd() const {
	auto iter = tm_map.begin();
	if (iter == tm_map.end()) return event_fd;
	return iter->second;
}

Dispatcher_EPoll::Callback Dispatcher_EPoll::stopWait(IAsyncResource &&resource) {
    if (typeid(resource) == typeid(SocketResource)) {
          const SocketResource &res = static_cast<const SocketResource &>(resource);
          switch (res.op) {
              case SocketResource::read: return disarm(Op::read, res.socket);break;
              case SocketResource::write: disarm(Op::write, res.socket);break;
          }
    }
    return Callback();
}

Dispatcher_EPoll::Callback Dispatcher_EPoll::disarm(Op op, int socket) {
    std::lock_guard _(lock);
    auto iter = fd_map.find(socket);
    if (iter != fd_map.end()) {
        auto iter2 = std::find_if(iter->second.begin(), iter->second.end(), [&](const Reg &r) {
           return r.op == op;
        });
        if (iter2 != iter->second.end()) {
            iter2->timeout = std::chrono::system_clock::now();
            Callback cb ( std::move(iter2->cb));
            notify();
            return cb;

        }
    }
    return Callback();
}

}

#endif

