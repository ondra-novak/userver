/*
 * isocket.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_ISOCKET_H_
#define SRC_MAIN_ISOCKET_H_

#include "helpers.h"

namespace userver {

class ISocket {
public:

	virtual int read(void *buffer, unsigned int size) = 0;
	virtual int write(const void *buffer, unsigned int size) = 0;
	virtual void read(void *buffer, unsigned int size, CallbackT<void(int)> &&fn) = 0;
	virtual void write(const void *buffer, unsigned int size, CallbackT<void(int)> &&fn) = 0;

	virtual void closeOutput() = 0;
	virtual void closeInput() = 0;

	virtual void setRdTimeout(int tm) = 0;
	virtual void setWrTimeout(int tm) = 0;
	virtual void setIOTimeout(int tm) = 0;

	virtual int getRdTimeout() const = 0;
	virtual int getWrTimeout() const = 0;

	///Wait for connect
	/** Sockets are create in pending connection state. This function waits for connect socket.
	 * If the socket is already connected, function returns immediately
	 * @param tm timeout
	 * @retval true connected
	 * @retval timeout or error
	 */
	virtual bool waitConnect(int tm) = 0;

	///Wait for connect
	/** Sockets are create in pending connection state. This function waits for connect socket.
	 * If the socket is already connected, function returns immediately
	 * @param tm timeout
	 * @param cb callback, which returns true=connected, false=timeout
	 */
	virtual void waitConnect(int tm, CallbackT<void(bool)> &&cb)  = 0;

	virtual ~ISocket() {}

	virtual bool timeouted() const = 0;
};


}

#endif /* SRC_MAIN_ISOCKET_H_ */
