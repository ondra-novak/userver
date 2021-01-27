/*
 * idispatcher.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_IDISPATCHER_H_
#define SRC_USERVER_IDISPATCHER_H_

#include "helpers.h"


namespace userver {

class IDispatcher {
public:

	using Callback = CallbackT<void(bool)>;
	virtual void waitRead(int socket, Callback &&cb, std::chrono::system_clock::time_point timeout) = 0;
	virtual void waitWrite(int socket, Callback &&cb, std::chrono::system_clock::time_point timeout) = 0;
	virtual void execAsync(Callback &&cb) = 0;

	struct Task {
		Callback cb;
		bool timeouted;

		Task(Callback &&cb, bool timeouted):cb(std::move(cb)), timeouted(timeouted) {}
		Task():cb(nullptr),timeouted(false) {}

		bool valid() const {return cb != nullptr;}
	};

	virtual Task getTask() = 0;
	virtual void stop() = 0;

	virtual ~IDispatcher() {}


};


}



#endif /* SRC_USERVER_IDISPATCHER_H_ */
