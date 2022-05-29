/*
 * idispatcher.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_IDISPATCHER_H_
#define SRC_USERVER_IDISPATCHER_H_

#include "platform_def.h"
#include "helpers.h"


namespace userver {

class IDispatcher {
public:

	//NOTE! the argument is now success, not timeout
	using Callback = CallbackT<void(bool)>;
	virtual void waitRead(SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) = 0;
	virtual void waitWrite(SocketHandle socket, Callback &&cb, std::chrono::system_clock::time_point timeout) = 0;
	virtual void execAsync(Callback &&cb) = 0;

	struct Task {
		Callback cb;
		bool success;

		Task(Callback &&cb, bool success):cb(std::move(cb)), success(success) {}
		Task():success(false) {}

		bool valid() const {return cb != nullptr;}
	};

	virtual Task getTask() = 0;
	virtual void stop() = 0;

	virtual ~IDispatcher() {}


};


}



#endif /* SRC_USERVER_IDISPATCHER_H_ */
