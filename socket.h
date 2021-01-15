/*
 * socket.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SOCKET_H_
#define SRC_MAIN_SOCKET_H_
#include "isocket.h"
class NetAddr;

class Socket: public ISocket {
public:
	Socket();
	explicit Socket(int s);
	virtual ~Socket();
	Socket(Socket &&other);
	Socket &operator=(Socket &&other);

	int read(void *buffer, unsigned int size) override;
	int write(const void *buffer, unsigned int size) override;
	void readAsync(void *buffer, unsigned int size, CallbackT<void(int)> &&fn) override;
	void writeAsync(const void *buffer, unsigned int size, CallbackT<void(int)> &&fn) override;


	void closeOutput() override;
	void closeInput() override;

	void setRdTimeout(int tm) override;
	void setWrTimeout(int tm) override;
	void setIOTimeout(int tm) override;

	int getRdTimeout() const override;
	int getWrTimeout() const override;

	Socket connect(const NetAddr &addr);
	bool timeouted() const override;

protected:
	int s = -1;
	int readtm=-1;
	int writetm=-1;
	bool tm = false;


};

#endif /* SRC_MAIN_SOCKET_H_ */
