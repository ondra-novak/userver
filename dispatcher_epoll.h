/*
 * dispatcher_epoll.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_DISPATCHER_EPOLL_H_
#define SRC_USERVER_DISPATCHER_EPOLL_H_

#include <atomic>
#include <chrono>
#include <mutex>
#include <queue>
#include <unordered_map>
#include <set>
#include <utility>
#include "idispatcher.h"


namespace userver {

class Dispatcher_EPoll: public IDispatcher {
public:

	Dispatcher_EPoll();
	virtual ~Dispatcher_EPoll() override;

	virtual void waitRead(int socket, Callback &&cb, std::chrono::system_clock::time_point timeout) override;
	virtual void waitWrite(int socket, Callback &&cb, std::chrono::system_clock::time_point timeout) override;
	virtual void execAsync(Callback &&cb) override;
	virtual Task getTask() override;
	virtual void stop() override;


protected:

	enum class Op {
		read, write
	};

	struct Reg {
		std::chrono::system_clock::time_point timeout;
		Op op;
		Callback cb;
	};

	class RegList: public SmallVector<Reg,4> {
	public:
		std::chrono::system_clock::time_point timeout;

	};

	using FDMap = std::unordered_map<int, RegList>;
	struct TimeoutKey: public std::pair<std::chrono::system_clock::time_point, int> {
		using Super = std::pair<std::chrono::system_clock::time_point, int>;
		TimeoutKey(std::chrono::system_clock::time_point tm, int s)
				:Super(tm,s) {}


		auto getTimeout() const {return first;}
		auto getFD() const {return second;}
	};
	using TMMap = std::set<TimeoutKey>;


	int epoll_fd;
	int pipe_wr;
	int pipe_rd;
	std::mutex lock;
	std::queue<Callback> imm_calls;

	FDMap fd_map;
	TMMap tm_map;

	std::atomic_bool stopped;


	void regWait(int socket, Op, Callback &&cb, std::chrono::system_clock::time_point timeout);
	void regImmCall(Callback &&cb);
	void notify();
	void rearm_fd(bool first_call, int socket, RegList &lst);
	int getWaitTime() const;
	int getTmFd() const;
};

}



#endif /* SRC_USERVER_DISPATCHER_EPOLL_H_ */
