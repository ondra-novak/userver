/*
 * async_provider_impl.h
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MINISERVER_ASYNC_RESOURCE_H_
#define SRC_MINISERVER_ASYNC_RESOURCE_H_

#include "platform.h"

namespace userver {



class AsyncResource {
public:

	enum Op {
		read, write
	};
	Op op;
	SocketHandle socket;

	AsyncResource(Op op, SocketHandle socket):op(op),socket(socket) {}


};

}

#endif /* SRC_MINISERVER_ASYNC_RESOURCE_H_ */
