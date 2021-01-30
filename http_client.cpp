/*
 * http_client.cpp
 *
 *  Created on: 28. 1. 2021
 *      Author: ondra
 */

#include "http_client.h"

namespace userver {

HttpClientRequest::HttpClientRequest(Stream &&s)
		:s(std::move(s)) {}

void HttpClientRequest::open(const std::string_view &method, const std::string &host, const std::string_view &path) {
	send_ctx_len.reset();
	userStream.reset();
	has_te = false;
	has_te_chunked = false;
	header_sent = false;
	s.writeNB(method);
	s.writeNB(" ");
	s.writeNB(path);
	s.writeNB(" HTTP/1.1\r\n");
	addHeader("Host",host);
	head_method = HeaderValue::iequal(method, "HEAD");
}

void HttpClientRequest::addHeaderInternal(const std::string_view &key,
		const std::string_view &value) {
	s.writeNB(key);
	s.writeNB(": ");
	s.writeNB(value);
	s.writeNB("\r\n");
}

void HttpClientRequest::addHeader(const std::string_view &key,
		const std::string_view &value) {
	if (HeaderValue::iequal(key,"Content-Length")) {
		send_ctx_len = HeaderValue(value).getUInt();
	}
	if (HeaderValue::iequal(key,"Transfer-Encoding")) {
		has_te = true;
		has_te_chunked = HeaderValue::iequal(value,"chunked");
	}
	addHeaderInternal(key, value);
}

void HttpClientRequest::setBodyLength(std::size_t sz) {
	addHeader("Content-Length", sz);
}

void HttpClientRequest::addHeader(const std::string_view &key, unsigned int value) {
	if (key == "Content-Length") {
		send_ctx_len = value;
	}

	char buff[100];char *c = buff+99;
	*c=0;
	if (value == 0) {
		--c;
		*c = '0';
	}
	else {
		while (value) {
			c--;
			*c = '0'+(value % 10);
			value/=10;
		}
	}
	addHeaderInternal(key, std::string_view(c));
}

void HttpClientRequest::setContentType(const std::string_view &ctx) {
	addHeader("Content-Type", ctx);
}

Stream& HttpClientRequest::beginBody() {
	if (!header_sent) {
		finish_headers();
	}
	if (has_te) {
		if (has_te_chunked) {
			userStream.emplace(std::make_unique<ChunkedStream<Stream &> >(s,true,false));
			return *userStream;
		} else {
			return s;
		}
	} else if (send_ctx_len.has_value()) {
		userStream.emplace(std::make_unique<LimitedStream<Stream &> >(s,0,*send_ctx_len));
		return *userStream;
	} else {
		return s;
	}
}

int HttpClientRequest::send() {
	if (!header_sent) finish_headers();
	userStream.reset();
	s.flush();
	if (s.getLine(responseBuffer, "\r\n\r\n") && parseResponse()) {
		prepareUserStream();
	} else {
		status = -1;
	}
	return status;
}

void HttpClientRequest::finish_headers() {
	if (!has_te && !send_ctx_len.has_value()) {
		addHeader("Transfer-Encoding","chunked");
	}
	s.writeNB("\r\n");
	header_sent = true;
}

void HttpClientRequest::prepareUserStream() {
	if (status == 100 || status == 204 || status == 304 || head_method) {
		userStream.reset();
	} else {
		HeaderValue ctl = get("Content-Length");
		if (ctl.defined) {
			std::size_t len = ctl.getUInt();
			userStream.emplace(std::make_unique<LimitedStream<Stream &> >(s, len, 0));
		} else {
			HeaderValue te = get("Tranfer-Encoding");
			if (HeaderValue::iequal(te, "chunked")) {
				userStream.emplace(std::make_unique<ChunkedStream<Stream &> >(s,false, true));
			} else {
				userStream.emplace(s.makeReference());
			}
		}
	}
}

void HttpClientRequest::sendAsync(CallbackT<void(int)> &&cb) {
	if (!header_sent) finish_headers();
	userStream.reset();
	s.flushAsync([this, cb = std::move(cb)](bool error) mutable {
		if (error) {
			status = -1;
			cb(status);
		} else {
			s.readAsync([this, cb = std::move(cb)](const std::string_view &data) mutable {
				if (data.empty()) {
					status = -1;
				} else {
					s.putBack(data);
					if (s.getLine(responseBuffer, "\r\n\r\n") && parseResponse()) {
						prepareUserStream();
					} else {
						status = -1;
					}
				}
				cb(status);
			});
		}
	});
}

Stream& HttpClientRequest::getRespone() {
	return *userStream;
}

HeaderValue HttpClientRequest::get(const std::string_view &key) const {
	auto iter = std::lower_bound(responseHeaders.begin(), responseHeaders.end(), std::pair(key,std::string_view()), HeaderValue::lessHeader);
	if (iter == responseHeaders.end() || !HeaderValue::iequal(iter->first, key)) return HeaderValue();
	else return HeaderValue(iter->second);
}

HttpClientRequest::iterator HttpClientRequest::begin() const {
	return responseHeaders.begin();
}
HttpClientRequest::iterator HttpClientRequest::end() const {
	return responseHeaders.end();
}

std::pair<HttpClientRequest::iterator, HttpClientRequest::iterator> HttpClientRequest::find(
		const std::string_view &key) const {
	return std::equal_range(responseHeaders.begin(), responseHeaders.end(), std::pair(key,std::string_view()));
}

std::string_view HttpClientRequest::getStatusMessage() {
	return st_message;
}

std::string_view HttpClientRequest::getProtocol() {
	return protocol;
}

void HttpClientRequest::finish_headers_excpect_100() {
	addHeader("Expect", "100-continue");
	finish_headers();
}

int HttpClientRequest::requestContinue() {
	if (!header_sent) {
		finish_headers_excpect_100();
		return send();
	} else {
		return 100;
	}
}

void HttpClientRequest::requestContinueAsync(CallbackT<void(int)> &&cb) {
	if (!header_sent) {
		finish_headers_excpect_100();
		sendAsync(std::move(cb));
	} else {
		cb(100);
	}
}

int HttpClientRequest::getStatus() const {
	return status;
}

bool HttpClientRequest::parseResponse() {
	std::string_view resp(responseBuffer);
	std::string_view ln = splitAt("\r\n", resp);
	responseHeaders.clear();
	if (!ln.empty()) {
		protocol = splitAt(" ", ln);
		std::string_view status_str = splitAt(" ", ln);
		st_message = ln;
		status = static_cast<int>(strtol(status_str.data(), nullptr, 10));
		ln = splitAt("\r\n", resp);
		while (!ln.empty()) {
			std::string_view key = splitAt(":", ln);
			std::string_view value = ln;
			trim(key);
			trim(value);
			responseHeaders.push_back({key,value});
			ln = splitAt("\r\n", resp);
		}
		std::sort(responseHeaders.begin(), responseHeaders.end(), HeaderValue::lessHeader);
		if (HeaderValue::iequal(protocol,"HTTP/1.0") || HeaderValue::iequal(protocol,"HTTP/1.1")) return true;
		return true;
	} else {
		return false;
	}
}


}

