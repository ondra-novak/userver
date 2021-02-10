/*
 * dispatcher.cpp
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */

#include "platform.h"
#include "dispatcher.h"
#include "win_category.h"

#include <fcntl.h>


namespace userver {

	Dispatcher::Dispatcher()
		:stopped(false)
	{
		intr_r = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		waiting.push_back({ intr_r,POLLIN,0 });
		next_timeout = std::chrono::system_clock::time_point::max();
		regs.push_back(Reg(nullptr, next_timeout));

		sockaddr_in sin;
		int sinlen = sizeof(sin);
		getsockname(intr_r, reinterpret_cast<sockaddr*>(&sin), &sinlen);
		thisAddr = NetAddr::fromSockAddr(*reinterpret_cast<sockaddr*>(&sin));
	}

	Dispatcher::~Dispatcher() {
		closesocket(intr_r);
	}

	void Dispatcher::waitRead(SocketHandle socket, Callback&& cb, std::chrono::system_clock::time_point timeout) {
		waitEvent(POLLIN, socket, std::move(cb), timeout);
	}

	void Dispatcher::waitWrite(SocketHandle socket, Callback&& cb, std::chrono::system_clock::time_point timeout) {
		waitEvent(POLLOUT, socket, std::move(cb), timeout);
	}

	void Dispatcher::execAsync(Callback&& cb) {
		waitEvent(0, intr_r, std::move(cb), std::chrono::system_clock::time_point::min());
	}

	void Dispatcher::notify() {
		char b = 1;
		int r = sendto(intr_r, &b, 1, 0, thisAddr->getAddr(), thisAddr->getAddrLen());
		if (r < 0) {
			int e = WSAGetLastError();
			throw std::system_error(e, win32_error_category());
		}		
	}

	void Dispatcher::waitEvent(int event, SocketHandle socket, Callback&& cb, std::chrono::system_clock::time_point timeout) {
		std::unique_lock _(lk);
		if (stopped) return;
		new_waiting.push_back({ socket, static_cast<short>(event), 0 });
		new_regs.push_back(Reg(std::move(cb), timeout));
		notify();
	}

	void Dispatcher::stop() {
		std::unique_lock _(lk);
		stopped = true;
		notify();
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
		while (true) {
			auto now = std::chrono::system_clock::now();
			if (lastIdx >= waiting.size()) {
				int wait_tm;
				if (now > next_timeout) wait_tm = 0;
				else if (next_timeout == std::chrono::system_clock::time_point::max()) wait_tm = -1;
				else wait_tm = std::chrono::duration_cast<std::chrono::milliseconds>(next_timeout - now).count();
				int r = WSAPoll(waiting.data(), waiting.size(), wait_tm);
				if (r < 0) {
					int e = WSAGetLastError();
					if (e == WSAEINTR) continue;
					throw std::system_error(e, win32_error_category());
				}
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
						sockaddr_in sin;
						int slen = sizeof(sin);
						if (::recvfrom(waiting[idx].fd, buff, 100,0, reinterpret_cast<sockaddr *>(&sin),&slen) < 0) {
							throw std::system_error(WSAGetLastError(), win32_error_category());
						}
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
					}
					else {
						Task ret(std::move(regs[idx].cb), false);
						removeItem(idx);
						return ret;
					}
				}
				else if (regs[idx].timeout < now || waiting[idx].events == 0) {
					Task ret(std::move(regs[idx].cb), true);
					removeItem(idx);
					return ret;
				}
				else {
					next_timeout = std::min(next_timeout, regs[idx].timeout);
					++lastIdx;
				}
			}
		}
	}

	Dispatcher::Reg::Reg(Callback&& cb, std::chrono::system_clock::time_point timeout)
		:cb(std::move(cb)), timeout(timeout)
	{

	}

}
