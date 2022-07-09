/*
 * dispatcher.h
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */
#ifndef SRC_USERVER_DISPATCHER_H_
#define SRC_USERVER_DISPATCHER_H_


#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <functional>
#include <optional>
#include "platform_def.h"

#include "idispatcher.h"
#include "netaddr.h"

namespace userver {


class Dispatcher: public IDispatcher {
public:


	Dispatcher();
	~Dispatcher();

	virtual bool waitAsync(IAsyncResource &&resource,  Callback &&cb, std::chrono::system_clock::time_point timeout) override;
	virtual void waitRead(SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) ;
	virtual void waitWrite(SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) ;
	virtual void interrupt() override;
	virtual Task getTask() override;
	virtual void stop() override;
	virtual Callback stopWait(IAsyncResource &&resource) override;

protected:

	struct Reg {
		Callback cb;
		std::chrono::system_clock::time_point timeout;

		Reg(Callback &&cb, std::chrono::system_clock::time_point timeout);
	};

	void waitEvent(int event, SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout);
	Callback disarmEvent(int event, SocketHandle socket);
	void removeItem(std::size_t idx);
	void notify();
	std::mutex lk;
	std::vector<pollfd> waiting, new_waiting;
	std::vector<Reg> regs, new_regs;
	SocketHandle intr_r, intr_w;
	std::size_t lastIdx = 0;
	std::chrono::system_clock::time_point next_timeout;
	std::atomic_bool stopped;
	std::atomic_bool intr;
	std::optional<NetAddr> thisAddr;  //used in windows
};


}


#endif
