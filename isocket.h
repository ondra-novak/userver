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

	///Read to a buffer synchronously
	/**
	 * @param buffer pointer to a buffer
	 * @param size size of the buffer
	 * @return returns count of read bytes, returns 0 if there is end of stream or timeout.
	 *
	 * @note read is blocking, you need to use async version to perform nonblocking read
	 */
	virtual int read(void *buffer, std::size_t size) = 0;
	///Write the buffer
	/**
	 *
	 * @param buffer pointer to a buffer
	 * @param size size of the buffer
	 * @return returns count of written bytes. Returns 0 if write timeouted
	 */
	virtual int write(const void *buffer, std::size_t size) = 0;

	///Read asynchronously
	/**
	 * @param buffer pointer to buffer
	 * @param size size of the buffer
	 * @param fn function called when read is done. Function receives count of bytes
	 * actualy read to the buffer. The value has the same meaning as return value of
	 * synchronized read.
	 *
	 * @note You must ensure, that buffer is valid during waiting for completion.
	 */
	virtual void read(void *buffer, std::size_t size, CallbackT<void(int)> &&fn) = 0;
	///Write asynchronously
	virtual void write(const void *buffer, std::size_t size, CallbackT<void(int)> &&fn) = 0;

	///Close the output
	/** when output is closed, futher writes are rejected. The other side receives
	 * EOF, however, reading is still possible
	 */
	virtual void closeOutput() = 0;
	///Close the input
	/** when input is closed, any futher reads will return EOF. Other side will be unable
	 * to send data and connection will look closed. However writing is still possible
	 */
	virtual void closeInput() = 0;

	///Set read timeout (in ms)
	virtual void setRdTimeout(int tm) = 0;
	///Set write timeout (in ms)
	virtual void setWrTimeout(int tm) = 0;
	///Set both read and write timeout (in ms)
	virtual void setIOTimeout(int tm) = 0;

	///Get current read timeout
	virtual int getRdTimeout() const = 0;
	///Get current write timeout
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

	///returns true, when any blocking or async operation timeouted
	virtual bool timeouted() const = 0;

	virtual void clearTimeout() = 0;
};


}

#endif /* SRC_MAIN_ISOCKET_H_ */
