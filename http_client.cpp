/*
 * http_client.cpp
 *
 *  Created on: 28. 1. 2021
 *      Author: ondra
 */

#include "http_client.h"

#include "async_provider.h"
#include "socket.h"
namespace userver {

HttpClientRequest::HttpClientRequest(Stream &&s)
		:s(std::move(s)) {}

void HttpClientRequest::open(const std::string_view &method, const std::string_view &host, const std::string_view &path) {
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
	this->host = host;
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
	if (HeaderValue::iequal(key,"Upgrade")) {
		has_te = true;
		has_te_chunked = false;
	}
	addHeaderInternal(key, value);
}

void HttpClientRequest::setBodyLength(std::size_t sz) {
	addHeader("Content-Length", sz);
}

void HttpClientRequest::addHeader(const std::string_view &key, std::size_t value) {
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
		finish_headers(true);
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
	if (!header_sent) finish_headers(false);
	userStream.reset();
	s.flush();
	if (s.getLine(responseBuffer, "\r\n\r\n") && parseResponse()) {
		prepareUserStream();
	} else {
		status = -1;
	}
	return status;
}

void HttpClientRequest::finish_headers(bool message) {
	if (message && !has_te && !send_ctx_len.has_value()) {
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
	if (!header_sent) finish_headers(true);
	userStream.reset();
	s.flushAsync([this, cb = std::move(cb)](bool ok) mutable {
		if (!ok) {
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

Stream& HttpClientRequest::getResponse() {
	return *userStream;
}
Stream& HttpClientRequest::getStream() {
	return s;
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
	finish_headers(true);
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

NetAddrList HttpClient::resolve(const CrackedURL &cu) {
	NetAddr addr(NetAddr::PNetAddr(nullptr));
	if (cfg.resolve != nullptr)  {
		return cfg.resolve(cu.domain);
	} else {
		auto lst = NetAddr::fromString(cu.host, std::to_string(cu.port));
		return lst;
	}
}

std::unique_ptr<HttpClientRequest> HttpClient::GET(const URL &url, HeaderList headers) {
	return sendRequest("GET", url, headers);
}

std::unique_ptr<HttpClientRequest> HttpClient::POST(const URL &url,
		HeaderList headers, const Data &data) {
	return sendRequest("POST", url, headers, data);

}

std::unique_ptr<HttpClientRequest> HttpClient::PUT(const URL &url,
		HeaderList &headers, const Data &data) {
	return sendRequest("PUT", url, headers, data);
}

std::unique_ptr<HttpClientRequest> HttpClient::DELETE(const URL &url,
		HeaderList headers, const Data &data) {
	return sendRequest("DELETE", url, headers, data);

}

std::unique_ptr<HttpClientRequest> HttpClient::DELETE(const URL &url,
		HeaderList headers) {
	return sendRequest("DELETE", url, headers);
}

std::unique_ptr<ISocket> HttpClient::connect(const NetAddr &addr, const CrackedURL &cu) {
	std::unique_ptr<ISocket> s;
	if (cu.ssl) {
		if (cfg.sslConnect == nullptr) throw std::runtime_error("SSL is not available");
		s = cfg.sslConnect(addr, cu.host);
		return s;
	} else {
		if (cfg.connect != nullptr) {
			s = cfg.connect(addr, cu.host);
		}
		else {
			s =std::make_unique<Socket>(Socket::connect(addr));
		}
	}
	if (s != nullptr) {
		s->setIOTimeout(cfg.iotimeout);
	}
	return s;
}
std::unique_ptr<HttpClientRequest> HttpClient::open(const Method &method, const URL &url) {

	auto cu = crackUrl(url);
	if (!cu.valid) return nullptr;

	NetAddrList addr = resolve(cu);
	std::unique_ptr<ISocket> socket;
	unsigned int idx = 0;
	while (idx < addr.size() && socket == nullptr) {
		socket = connect(addr[idx],cu);
		bool resp = socket->waitConnect(cfg.connectTimeout);
		if (!resp) socket = nullptr;
		idx++;
	}
	if (socket == nullptr) return nullptr;

	Stream stream(new SocketStream(std::move(socket)));
	auto req = std::make_unique<HttpClientRequest>(std::move(stream));
	req->open(method, cu.host, cu.path);
	req->addHeader("UserAgent", cfg.userAgent);
	return req;
}

struct Clousure {
	std::string method;
	std::string url;
	CallbackT<void(std::unique_ptr<HttpClientRequest> &&)> cb;
};


template<typename Fn>
void HttpClient::connectAsync(NetAddrList &&list, CrackedURL &&cu, Fn &&fn, unsigned int idx) {
	if (idx >= list.size()) fn(nullptr);
	auto sock = connect(list[idx],cu);
	if (sock == nullptr) fn(nullptr);
	ISocket *s = sock.get();
	sock->setIOTimeout(cfg.iotimeout);
	s->waitConnect(cfg.connectTimeout, [this, sock = std::move(sock), list = std::move(list), cu = std::move(cu), fn = std::forward<Fn>(fn), idx](bool ok) mutable  {
		if (ok) {
			fn(std::move(sock));
		} else {
			connectAsync(std::move(list), std::move(cu), std::forward<Fn>(fn), idx+1);
		}
	});
}

void HttpClient::open(const Method &method, const URL &url,Callback  &&callback) {


	getCurrentAsyncProvider().runAsync([this,
										clousure = std::make_unique<Clousure>(Clousure{
												std::string(method),
												std::string(url),
												std::move(callback)
										})] () mutable {
		try {
			auto cu = crackUrl(clousure->url);
			if (!cu.valid) clousure->cb(nullptr);
			NetAddrList nlist = resolve(cu);
			connectAsync(std::move(nlist),std::move(cu), [this, cu, clousure = std::move(clousure)](std::unique_ptr<ISocket> &&socket){
				if (socket==nullptr) {
					clousure->cb(nullptr);
				} else {
					Stream stream(new SocketStream(std::move(socket)));
					auto req = std::make_unique<HttpClientRequest>(std::move(stream));
					req->open(clousure->method, cu.host, cu.path);
					req->addHeader("UserAgent", cfg.userAgent);
					clousure->cb(std::move(req));
				}
			},0);
		} catch (...) {
			if (clousure !=nullptr) {
				clousure->cb(nullptr);
			}
		}
	});

}

HttpClient::CrackedURL HttpClient::crackUrl(const std::string_view &url) {

	std::string_view rest;
	CrackedURL cu;
	if (HeaderValue::iequal(url.substr(0,7), "http://")) {
		rest = url.substr(7);
	} else if (HeaderValue::iequal(url.substr(0,8), "https://")) {
		cu.ssl = true;
		rest = url.substr(8);
	} else {
		return cu;
	}

	auto first_slash = rest.find('/');
	if (first_slash == rest.npos) {
		cu.path = "/";
		cu.host = rest;
	} else {
		cu.path = rest.substr(first_slash);
		cu.host = rest.substr(0,first_slash);
	}
	auto amp = cu.host.rfind('@');
	if (amp != cu.host.npos) {
		cu.auth = cu.host.substr(0,amp);
		cu.host = cu.host.substr(amp+1);
	}
	auto ddot = cu.host.find(':');
	if (ddot != cu.host.npos) {
		cu.port = 0;
		auto port_part = cu.host.substr(ddot+1);
		cu.domain = cu.host.substr(0,ddot);
		for (char c: port_part) {
			if (isdigit(c)) cu.port = cu.port * 10 + (c - '0');
			else return cu;
		}
	} else {
		cu.port = cu.ssl?443:80;
		cu.domain = cu.host;
	}
	cu.valid = true;
	return cu;
}

std::unique_ptr<HttpClientRequest> HttpClient::sendRequest(const Method &method, const URL &url, HeaderList headers) {
	auto req = open(method, url);
	if (req == nullptr) return req;
	for (const HeaderPair &hp: headers) req->addHeader(hp.first, hp.second);
	if (req->send() < 0) return nullptr;
	return req;
}

std::unique_ptr<HttpClientRequest> HttpClient::sendRequest(const Method &method, const URL &url, HeaderList headers, const Data &data) {
	auto req = open(method, url);
	if (req == nullptr) return req;
	for (const HeaderPair &hp: headers) req->addHeader(hp.first, hp.second);
	req->setBodyLength(data.size());
	Stream &s = req->beginBody();
	s.write(data);
	if (req->send() < 0) return nullptr;
	return req;
}

void HttpClient::sendRequest(const Method &method, const URL &url, HeaderList headers, Callback &&cb) {
	std::vector<HeaderPair> hrds(headers.begin(), headers.end());
	open(method, url, [cb = std::move(cb), hdrs = std::move(hrds)](
			std::unique_ptr<HttpClientRequest> &&req
	)mutable{
		if (req == nullptr) cb(nullptr);
		else {
			for (const auto &hp: hdrs) req->addHeader(hp.first, hp.second);
			auto reqptr = req.get();
			reqptr->sendAsync([req = std::move(req), cb = std::move(cb)](int status) mutable {
				if (status < 0) cb(nullptr);
				else cb(std::move(req));
			});
		}
	});
}

void HttpClient::sendRequest(const Method &method, const URL &url, HeaderList headers, DataStream &&data, Callback &&cb) {
	std::vector<HeaderPair> hrds(headers.begin(), headers.end());
	open(method, url, [cb = std::move(cb), data= std::move(data), hdrs = std::move(hrds)](
			std::unique_ptr<HttpClientRequest> &&req
	)mutable{
		if (req == nullptr) cb(nullptr);
		else {
			for (const auto &hp: hdrs) req->addHeader(hp.first, hp.second);
			auto reqptr = req.get();
			reqptr->sendAsync(std::move(data), [req = std::move(req), cb = std::move(cb)](int status) mutable {
				if (status < 0) cb(nullptr);
				else cb(std::move(req));
			});
		}
	});
}



HttpClient::HeaderList::HeaderList(const std::initializer_list<HeaderPair> &lst)
:ptr(0),count(lst.size())
{
ptr = lst.begin();
}

HttpClient::HeaderList::HeaderList(const std::vector<HeaderPair> &lst)
:ptr(lst.data()),count(lst.size())
{

}

HttpClient::HeaderList::HeaderList(const HeaderPair *lst, std::size_t count)
:ptr(lst),count(count){}

HttpClient::HttpClient(HttpClientCfg &&cfg):cfg(std::move(cfg)) {
}

class StreamWrap: public AbstractStream {
public:
	StreamWrap(Stream &s, std::unique_ptr<HttpClientRequest> &&req):req(std::move(req)),s(s) {}

	virtual std::string_view read() override {return s.read();}
	virtual void readAsync(CallbackT<void(const std::string_view &data)> &&fn) override {return s.readAsync(std::move(fn));}
	virtual void putBack(const std::string_view &pb) override {return s.putBack(pb);}
	virtual void write(const std::string_view &) override {}
	virtual bool writeNB(const std::string_view &) override {return false;}
	virtual void closeOutput() override  {}
	virtual void closeInput() override  {s.closeInput();}
	virtual void flush() override  {}
	virtual void flushAsync(CallbackT<void(bool)> &&fn) override  {fn(false);}
	virtual bool timeouted() const override  {return s.timeouted();}
	virtual void clearTimeout() override  {s.clearTimeout();}
	virtual std::size_t getOutputBufferSize() const override  {return s.getOutputBufferSize();}


protected:
	std::unique_ptr<HttpClientRequest> req;
	Stream &s;


};

Stream HttpClientRequest::getResponseBody(std::unique_ptr<HttpClientRequest> &&req) {
	Stream &s = req->getResponse();
	return Stream(new StreamWrap(s,std::move(req)));
}

const HttpClientRequest::HeaderMap& HttpClientRequest::getHeaders() {
	return responseHeaders;
}


void HttpClient::GET(const URL &url, HeaderList headers, Callback &&cb) {
	sendRequest("GET", url, headers, std::move(cb));
}

void HttpClient::POST(const URL &url, HeaderList headers, const Data &data, Callback &&cb) {
	sendRequest("POST", url, headers, data, std::move(cb));
}

void HttpClient::PUT(const URL &url, HeaderList &headers, const Data &data, Callback &&cb) {
	sendRequest("PUT", url, headers, data, std::move(cb));
}

void HttpClient::DELETE(const URL &url, HeaderList headers, const Data &data,Callback &&cb) {
	sendRequest("DELETE", url, headers, data, std::move(cb));
}

void HttpClient::DELETE(const URL &url, HeaderList headers, Callback &&cb) {
	sendRequest("DELETE", url, headers, std::move(cb));
}


class StringSource {
public:
	StringSource (const std::string_view s):s(s),sent(false) {}
	std::string_view operator()() const {
		if (sent) return std::string_view();
		else {
			sent = true;
			return s;
		}
	}
	auto size() const {return s.size();}

protected:
	std::string s;
	mutable bool sent;
};

void HttpClient::sendRequest(const Method &method, const URL &url,
		HeaderList headers, const Data &data, Callback &&cb) {
	std::vector<HeaderPair> hrds(headers.begin(), headers.end());
	StringSource sdata(data);
	open(method, url, [cb = std::move(cb), sdata= std::move(sdata), hdrs = std::move(hrds)](
			std::unique_ptr<HttpClientRequest> &&req
	)mutable{
		if (req == nullptr) cb(nullptr);
		else {
			for (const auto &hp: hdrs) req->addHeader(hp.first, hp.second);
			req->setBodyLength(sdata.size());
			auto reqptr = req.get();
			reqptr->sendAsync(sdata, [req = std::move(req), cb = std::move(cb)](int status) mutable {
				if (status < 0) cb(nullptr);
				else cb(std::move(req));
			});
		}
	});
}

}
