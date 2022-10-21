/*
 * ssl_socket.h
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_SSL_SOCKET_H_
#define SRC_USERVER_SSL_SOCKET_H_

#include <openssl/ssl.h>
#include "socket.h"
#include "isocket.h"
#include <mutex>

namespace userver {

struct SSL_CTX_Free { void operator()(SSL_CTX *ptr) const;};
using PSSL_CTX = std::shared_ptr<SSL_CTX>;

struct SSL_Free { void operator()(SSL *ptr) const;};
using PSSL = std::unique_ptr<SSL, SSL_Free>;

class SSLSocket: public ISocket {
public:
	enum class Mode {
		accept,
		connect
	};

	SSLSocket(Socket &&s, const PSSL_CTX &ctx, Mode mode);
	~SSLSocket();
	SSLSocket(SSLSocket &&other);

	Socket &getSocket();
	const Socket &getSocket() const;
	SSL *getSSLSocket() const;
	PSSL_CTX getSSLContext() const;
	void setSSLContext(const PSSL_CTX &ctx);
	virtual void waitConnect(int tm, userver::CallbackT<void(bool)> &&cb) override;
	virtual bool waitConnect(int tm)  override;
	virtual int read(void *buffer, std::size_t size)  override;
	virtual void read(void *buffer, std::size_t size, userver::CallbackT<void(int)> &&fn)  override;
	virtual int write(const void *buffer, std::size_t size)  override;
	virtual void setIOTimeout(int tm)  override;
	virtual void write(const void *buffer, std::size_t size, userver::CallbackT<void(int)> &&fn)  override;
	virtual bool timeouted() const  override;
	virtual void closeOutput()  override;
	virtual void closeInput()  override;
	virtual void setWrTimeout(int tm)  override;
	virtual int getWrTimeout() const  override;
	virtual void setRdTimeout(int tm)  override;
	virtual int getRdTimeout() const  override;
	virtual void clearTimeout() override;
    virtual bool cancelAsyncRead(bool set_timeouted = true) override;
    virtual bool cancelAsyncWrite(bool set_timeouted = true) override;

protected:

	enum class State {
		retry,
		eof,
		timeout,
		error
	};

	enum class ConnState {
		not_connected,
		connected,
		closed
	};


	Socket s;
	PSSL_CTX ctx;
	PSSL ssl;
	Mode mode;
	std::recursive_mutex ssl_lock;
	bool tm = false;
	ConnState connState = ConnState::not_connected;


	State handleState(int r, int tm);
	template<typename Fn>
	void handleStateAsync(int r, int tm, Fn &&fn);

	void shutdownAsync();
	bool afterConnect();
};

}



#endif /* SRC_USERVER_SSL_SOCKET_H_ */
