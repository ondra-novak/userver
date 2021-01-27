/*
 * dispatcher.h
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */

#include <poll.h>
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>
#include <functional>
#include "idispatcher.h"

namespace userver {

class Dispatcher: public IDispatcher {
public:


	Dispatcher();
	~Dispatcher();

	virtual void waitRead(int socket, Callback &&cb, std::chrono::system_clock::time_point timeout) override;
	virtual void waitWrite(int socket, Callback &&cb, std::chrono::system_clock::time_point timeout) override;
	virtual void execAsync(Callback &&cb) override;
	virtual Task getTask() override;
	virtual void stop() override;

protected:

	struct Reg {
		Callback cb;
		std::chrono::system_clock::time_point timeout;

		Reg(Callback &&cb, std::chrono::system_clock::time_point timeout);
	};

	void waitEvent(int event, int socket, Callback &&cb, std::chrono::system_clock::time_point timeout);
	void removeItem(std::size_t idx);
	void notify();
	std::mutex lk, disp_lk;
	std::vector<pollfd> waiting, new_waiting;
	std::vector<Reg> regs, new_regs;
	int intr_r, intr_w;
	std::size_t lastIdx = 0;
	std::chrono::system_clock::time_point next_timeout;
	std::atomic_bool stopped;
};


}
