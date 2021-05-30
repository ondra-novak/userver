/*
 * ssl_socket.cpp
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#include "platform.h"
#include <userver/async_provider.h>
#include <userver/async_resource.h>
#include "ssl_socket.h"
#include "ssl_exception.h"

namespace userver {

SSLSocket::SSLSocket(Socket &&s, const PSSL_CTX &ctx, Mode mode)
		:s(std::move(s)),ctx(ctx),mode(mode)
{
		ssl = PSSL(SSL_new(ctx.get()));
		if (ssl == nullptr) {
			throw SSLError();
		}
		if (!SSL_set_fd(ssl.get(), s.getHandle())) {
			throw SSLError();
		}

		switch (mode) {
		case Mode::connect: SSL_set_connect_state(ssl.get());break;
		case Mode::accept: SSL_set_accept_state(ssl.get());break;
		}
}

SSLSocket::~SSLSocket() {
	if (connState == ConnState::connected) {
		try {
			auto prov = getCurrentAsyncProvider();

			prov.runAsync([sock = SSLSocket(std::move(*this))]() mutable {
				sock.shutdownAsync();
			});

		} catch (...) {
			closeOutput();
		}
	}
}

SSLSocket::SSLSocket(SSLSocket &&other)
	:s(std::move(other.s))
	,ctx(std::move(other.ctx))
	,ssl(std::move(other.ssl))
	,mode(other.mode)
	,tm(other.tm)
	,connState(other.connState) {
	other.connState = ConnState::closed;
}


void SSL_CTX_Free::operator ()(SSL_CTX *ptr) const {SSL_CTX_free(ptr);}
void SSL_Free::operator ()(SSL *ptr) const {SSL_free(ptr);}


Socket& SSLSocket::getSocket() {return s;}
const Socket& SSLSocket::getSocket() const {return s;}
SSL* SSLSocket::getSSLSocket() const {return ssl.get();}
PSSL_CTX SSLSocket::getSSLContext() const {return ctx;}

void SSLSocket::setIOTimeout(int tm) {
	s.setIOTimeout(tm);
}

bool SSLSocket::timeouted() const {
	return tm;
}

void SSLSocket::closeOutput() {
	if (connState == ConnState::connected) {
		int r;
		{
			std::lock_guard _(ssl_lock);
			r = SSL_shutdown(ssl.get());
			connState = ConnState::closed;
		}
		while (r < 0) {
			switch (handleState(r, s.getWrTimeout())) {
			case State::retry:  {
				std::lock_guard _(ssl_lock);
				r = SSL_shutdown(ssl.get());
				break;
			}
			case State::timeout: tm = true; r = 0;break;
			default:
			case State::eof: r = 0; break;
			}
		}
	}
}

void SSLSocket::closeInput() {
	closeOutput();
	s.closeInput();
}

void SSLSocket::setWrTimeout(int tm) {
	s.setWrTimeout(tm);
}

int SSLSocket::getWrTimeout() const {
	return s.getWrTimeout();
}

void SSLSocket::setRdTimeout(int tm) {
	s.setRdTimeout(tm);
}

int SSLSocket::getRdTimeout() const {
	return s.getRdTimeout();
}

SSLSocket::State SSLSocket::handleState(int r, int tm) {
	int st;
	{
		std::lock_guard _(ssl_lock);
		st = SSL_get_error(ssl.get(), r);
		if (connState == ConnState::not_connected) connState  = ConnState::connected;
	}
	switch (st) {
		case SSL_ERROR_NONE: return State::retry;
		case SSL_ERROR_ZERO_RETURN:	{
			std::lock_guard _(ssl_lock);
			connState = ConnState::closed;
			return State::eof;
		}
		case SSL_ERROR_WANT_WRITE: return s.waitForWrite(tm)?State::retry:State::timeout;
		case SSL_ERROR_WANT_READ: return s.waitForRead(tm)?State::retry:State::timeout;
		case SSL_ERROR_WANT_CONNECT:
		case SSL_ERROR_WANT_ACCEPT: return waitConnect(tm)?State::retry:State::timeout;
		case SSL_ERROR_SYSCALL: {
			std::lock_guard _(ssl_lock);
			connState = ConnState::closed;
			int e = errno;
			throw std::system_error(e, error_category(), "SSL_ERROR_SYSCALL");
		}
		case SSL_ERROR_SSL: {
			std::lock_guard _(ssl_lock);
			connState = ConnState::closed;
			throw SSLError();
		}
		default:
			return State::eof;
	}
}
template<typename Fn>
void SSLSocket::handleStateAsync(int r, int tm, Fn &&fn) {
	int st;
	{
		std::lock_guard _(ssl_lock);
		st = SSL_get_error(ssl.get(), r);
		if (connState == ConnState::not_connected) connState  = ConnState::connected;
	}
	switch (st) {
		case SSL_ERROR_NONE: fn(State::retry);break;
		case SSL_ERROR_ZERO_RETURN:	{
			std::lock_guard _(ssl_lock);
			connState = ConnState::closed;
			fn(State::eof);break;
		}
		case SSL_ERROR_WANT_CONNECT:
		case SSL_ERROR_WANT_ACCEPT:
			waitConnect(tm, [fn = std::forward<Fn>(fn)](bool ok)mutable {fn(ok?State::retry: State::timeout);});
			break;
		case SSL_ERROR_WANT_WRITE:
			getCurrentAsyncProvider().runAsync(
					AsyncResource(AsyncResource::write, s.getHandle()), [fn = std::forward<Fn>(fn)] (bool succ) mutable {
							fn(succ?State::retry:State::timeout);
					},
					std::chrono::system_clock::now() + std::chrono::milliseconds(tm)
			);break;
		case SSL_ERROR_WANT_READ:
			getCurrentAsyncProvider().runAsync(
					AsyncResource(AsyncResource::read, s.getHandle()), [fn = std::forward<Fn>(fn)] (bool succ) mutable {
						fn(succ?State::retry:State::timeout);},
					std::chrono::system_clock::now() + std::chrono::milliseconds(tm)
			);break;
		case SSL_ERROR_SYSCALL: {
			std::lock_guard _(ssl_lock);
			connState = ConnState::closed;
			int e = errno;
			throw std::system_error(e, error_category(), "SSL_ERROR_SYSCALL");
		}
		case SSL_ERROR_SSL: {
			std::lock_guard _(ssl_lock);
			connState = ConnState::closed;
			throw SSLError();
		}
		default:
			fn(State::eof);
			break;
	}
}


void SSLSocket::waitConnect(int tm, userver::CallbackT<void(bool)> &&cb) {
	int r;
	{
		std::lock_guard _(ssl_lock);
		if (connState != ConnState::not_connected) {
			cb(connState == ConnState::connected);
		}
		switch (mode) {
			case Mode::connect: r = SSL_connect(ssl.get());break;
			case Mode::accept: r = SSL_accept(ssl.get());break;
			default: cb(false);break;
		};
	}

	if (r<0) {
		handleStateAsync(r, tm, [cb = std::move(cb), tm, this](State st) mutable {
			if (st == State::retry) waitConnect(tm, std::move(cb)); else cb(false);
		});
	} else {
		cb(true);
	}
}

bool SSLSocket::waitConnect(int tm) {
	int r;
	if (connState != ConnState::not_connected) return connState == ConnState::connected;
	switch (mode) {
		case Mode::connect:
			{
				std::lock_guard _(ssl_lock);
				r = SSL_connect(ssl.get());
			}
			while (r<1) {
				if (handleState(r, tm) != State::retry) return false;
				std::lock_guard _(ssl_lock);
				r = SSL_connect(ssl.get());
			}
			return true;
		case Mode::accept: {
			{
				std::lock_guard _(ssl_lock);
				r = SSL_accept(ssl.get());
			}
			while (r<1) {
				if (handleState(r, tm) != State::retry) return false;
				std::lock_guard _(ssl_lock);
				r = SSL_accept(ssl.get());
			}
			return true;
		}
	}
	return false;

}

int SSLSocket::read(void *buffer, std::size_t size) {
	int r;
	{
		std::lock_guard _(ssl_lock);
		if (connState == ConnState::closed) return 0;
		r = SSL_read(ssl.get(), buffer, static_cast<int>(size));
	}
	while (r < 1) {
		switch (handleState(r, s.getRdTimeout())) {
			case State::retry: {
				std::lock_guard _(ssl_lock);
				r = SSL_read(ssl.get(), buffer, static_cast<int>(size)); break;
			}
			case State::timeout: tm = true; return 0;
			default:
			case State::eof: return 0;
		}
	}
	return r;
}

int SSLSocket::write(const void *buffer, std::size_t size) {
	int r;
	{
		std::lock_guard _(ssl_lock);
		if (connState == ConnState::closed) return 0;
		r = SSL_write(ssl.get(), buffer, static_cast<int>(size));
	}
	while (r < 1) {
		switch (handleState(r, s.getWrTimeout())) {
			case State::retry: {
				std::lock_guard _(ssl_lock);
				r = SSL_write(ssl.get(), buffer, static_cast<int>(size)); break;
			}
			case State::timeout: tm = true; return 0;
			default:
			case State::eof: return 0;
		}
	}
	return r;
}

void SSLSocket::read(void *buffer, std::size_t size, userver::CallbackT<void(int)> &&fn) {
	int r;
	{
		std::lock_guard _(ssl_lock);
		if (connState == ConnState::closed)  {
			fn(0);return;
		}
		r = SSL_read(ssl.get(), buffer, static_cast<int>(size));
	}
	if (r < 1) {
		handleStateAsync(r, s.getRdTimeout(), [fn = std::move(fn), buffer, size, this](State st) mutable {
			switch (st) {
			case State::retry: read(buffer, size, std::move(fn)); break;
			case State::timeout: tm = true; fn(0); break;
			default:
			case State::eof: fn(0); break;
			}
		});
	} else {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn),r]{fn(r);});
	}
}

void SSLSocket::write(const void *buffer, std::size_t size, userver::CallbackT<void(int)> &&fn) {
	int r;
	{
		std::lock_guard _(ssl_lock);
		r = SSL_write(ssl.get(), buffer, static_cast<int>(size));
		if (connState == ConnState::closed)  {
			fn(0);return;
		}
	}
	if (r < 1) {
		handleStateAsync(r, s.getWrTimeout(), [fn = std::move(fn), buffer, size, this](State st) mutable {
			switch (st) {
			case State::retry: write(buffer, size, std::move(fn)); break;
			case State::timeout: tm = true; fn(0); break;
			default:
			case State::eof: fn(0); break;
			}
		});
	} else {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn),r]{fn(r);});
	}

}

void SSLSocket::shutdownAsync() {
	int r = SSL_shutdown(ssl.get());
	if (r < 0) {
		AsyncResource::Op op;
		SocketHandle h = s.getHandle();
		int st = SSL_get_error(ssl.get(), r);
		switch (st) {
			case SSL_ERROR_WANT_WRITE: op = AsyncResource::write;break;
			case SSL_ERROR_WANT_READ: op = AsyncResource::read;break;
			default: return;
		}
		getCurrentAsyncProvider().runAsync(AsyncResource(op, h),[sock = SSLSocket(std::move(*this))](bool succ) mutable {
			if (succ) sock.shutdownAsync();
		}, std::chrono::system_clock::now()+std::chrono::seconds(30));
	}
}


void SSLSocket::setSSLContext(const PSSL_CTX &ctx) {
	std::lock_guard _(ssl_lock);
	if (SSL_set_SSL_CTX(ssl.get(), ctx.get()) == nullptr) throw SSLError();
	this->ctx = ctx;
}

void SSLSocket::clearTimeout() {
	s.clearTimeout();
}

}
