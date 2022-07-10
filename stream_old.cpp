/*
 * stream.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include <userver/stream.h>
#include "async_provider.h"

namespace userver {

std::string_view SocketStream::read() {
	std::string_view out;
	if (curbuff.empty()) {
		if (!eof) {
			if (rdbuff.empty()) rdbuff.resize(1000);
			int sz = sock->read(rdbuff.data(), rdbuff.size());
			if (sz == 0) {
				eof = true;
			} else {
				if (sz == static_cast<int>(rdbuff.size())) rdbuff.resize(sz*3/2);
				out = rdbuff;
				out = out.substr(0, sz);
			}
		}
	} else {
		std::swap(out, curbuff);
	}
	return out;
}

std::size_t SocketStream::maxWrBufferSize = 65536;

void SocketStream::putBack(const std::string_view &pb) {
	curbuff = pb;
}


void SocketStream::write(const std::string_view &data) {
	wrbuff.append(data);
	if (wrbuff.size() >= wrbufflimit) {
		flush_lk();
	}
}


bool SocketStream::timeouted() const {
	return sock->timeouted();
}

void SocketStream::closeOutput() {
	flush_lk();
	sock->closeOutput();

}

void SocketStream::closeInput() {
	sock->closeInput();
}

void SocketStream::flush() {
	flush_lk();
}

ISocket& SocketStream::getSocket() const {
	return *sock;
}

bool SocketStream::writeNB(const std::string_view &data) {
	wrbuff.append(data);
	return (wrbuff.size() >= wrbufflimit);
}

void SocketStream::flushAsync(const std::string_view &data, bool firstCall, CallbackT<void(bool)> &&fn) {
	if (data.empty()) {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn)] {
			fn(true);
		});
	} else {
		sock->write(data.data(), data.size(), [this, data, firstCall, fn = std::move(fn)](int r) mutable {
			if (r <= 0) {
				wrbuff.clear();
				fn(false);
			} else if (static_cast<std::size_t>(r) == data.size()) {
				wrbufflimit = std::min(wrbufflimit * 3 / 2, maxWrBufferSize);
				wrbuff.clear();
				fn(true);
			} else {
				if (!firstCall) wrbufflimit = (r * 2 + 2) / 3 ;
				flushAsync(data.substr(r), false, std::move(fn));
			}
		});
	}
}

void SocketStream::flushAsync(CallbackT<void(bool)> &&fn) {
	if (wrbuff.empty()) {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn)] {
			fn(true);
		});
	}
	else {
		std::string_view s(wrbuff);
		flushAsync(s, true, std::move(fn));

	}
}


void SocketStream::flush_lk() {
	std::string_view s(wrbuff);
	if (!s.empty())  {
		unsigned int wx = sock->write(s.data(),s.length());
		bool rep = wx < s.length();
		while (rep) {
			s = s.substr(wx);
			wx = sock->write(s.data(),s.length());
			rep = wx < s.length();
			if (rep && wx < wrbufflimit) {
				wrbufflimit = (wx * 2+2) / 3;
			}
		}
		wrbufflimit = std::min(wrbufflimit *3/2, maxWrBufferSize);
		wrbuff.clear();
	}
}

void SocketStream::readAsync(CallbackT<void(const std::string_view &data)> &&fn) {
	std::string_view out;
	if (curbuff.empty() && !eof) {
		if (rdbuff.empty()) rdbuff.resize(1000);
		sock->read(rdbuff.data(), rdbuff.size(), [this, fn = std::move(fn)](int sz){
			std::string_view out;
			if (sz == 0) {
				eof = true;
			} else {
				if (sz == static_cast<int>(rdbuff.size())) rdbuff.resize(sz*3/2);
				out = rdbuff;
				out = out.substr(0, sz);
			}
			fn(out);
		});
	} else {
		std::swap(out,curbuff);
		getCurrentAsyncProvider().runAsync([fn = std::move(fn), out](){
			fn(out);
		});
	}
}

std::size_t SocketStream::getOutputBufferSize() const {
	return wrbufflimit;
}

void SocketStream::clearTimeout() {
	sock->clearTimeout();
	eof = false;
}

}
