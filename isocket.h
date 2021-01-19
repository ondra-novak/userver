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
	virtual void readAsync(void *buffer, unsigned int size, CallbackT<void(int)> &&fn) = 0;
	virtual void writeAsync(const void *buffer, unsigned int size, CallbackT<void(int)> &&fn) = 0;

	virtual void closeOutput() = 0;
	virtual void closeInput() = 0;

	virtual void setRdTimeout(int tm) = 0;
	virtual void setWrTimeout(int tm) = 0;
	virtual void setIOTimeout(int tm) = 0;

	virtual int getRdTimeout() const = 0;
	virtual int getWrTimeout() const = 0;

	virtual ~ISocket() {}

	virtual bool timeouted() const = 0;
};


}

#endif /* SRC_MAIN_ISOCKET_H_ */
