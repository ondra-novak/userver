/*
 * socket.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SOCKET_H_
#define SRC_MAIN_SOCKET_H_
#include "isocket.h"
#include "platform_def.h"

namespace userver {

class NetAddr;

class Socket: public ISocket {
public:
	Socket();
	explicit Socket(SocketHandle s);
	virtual ~Socket();
	Socket(Socket &&other);
	Socket &operator=(Socket &&other);

	int read(void *buffer, std::size_t size) override;
	int write(const void *buffer, std::size_t size) override;
	void read(void *buffer, std::size_t size, CallbackT<void(int)> &&fn) override;
	void write(const void *buffer, std::size_t size, CallbackT<void(int)> &&fn) override;

    virtual bool cancelAsyncRead(bool set_timeouted = true) override;
    virtual bool cancelAsyncWrite(bool set_timeouted = true) override;

	void closeOutput() override;
	void closeInput() override;

	void setRdTimeout(int tm) override;
	void setWrTimeout(int tm) override;
	void setIOTimeout(int tm) override;

	int getRdTimeout() const override;
	int getWrTimeout() const override;

	bool timeouted() const override;

	virtual bool waitConnect(int tm) override;
	virtual void waitConnect(int tm, CallbackT<void(bool)> &&cb) override;;

	bool waitForRead(int tm) const;
	bool waitForWrite(int tm) const;

	///Connect the socket
	/**
	 * @param addr address to connect
	 * @return socket in connection state. You need to call waitConnect()
	 */
	static Socket connect(const NetAddr &addr);

	SocketHandle getHandle() const {return s;}

	virtual void clearTimeout() override;

protected:
	SocketHandle s = -1;
	int readtm=-1;
	int writetm=-1;
	bool tm = false;

	bool checkSocketState() const;
};

}


#endif /* SRC_MAIN_SOCKET_H_ */
