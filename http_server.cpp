/*
 * http_server_request.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include <iostream>
#include "http_server.h"

#include <algorithm>
#include <atomic>
#include <sstream>
#include <filesystem>
#include <fstream>

#include "helpers.h"
#include "socket_server.h"

namespace userver {

static std::string_view statusMessages[] = {
		"100 Continue",
		"101 Switching Protocols",
		"200 OK",
		"201 Created",
		"202 Accepted",
		"203 Non-Authoritative Information",
		"204 No Content",
		"205 Reset Content",
		"206 Partial Content",
		"300 Multiple Choices",
		"301 Moved Permanently",
		"302 Found",
		"303 See Other",
		"304 Not Modified",
		"305 Use Proxy",
		"307 Temporary Redirect",
		"308 Permanent Redirect",
		"400 Bad Request",
		"401 Unauthorized",
		"402 Payment Required",
		"403 Forbidden",
		"404 Not Found",
		"405 Method Not Allowed",
		"406 Not Acceptable",
		"407 Proxy Authentication Required",
		"408 Request Timeout",
		"409 Conflict",
		"410 Gone",
		"411 Length Required",
		"412 Precondition Failed",
		"413 Request Entity Too Large",
		"414 Request-URI Too Long",
		"415 Unsupported Media Type",
		"416 Requested Range Not Satisfiable",
		"417 Expectation Failed",
		"426 Upgrade Required",
		"500 Internal Server Error",
		"501 Not Implemented",
		"502 Bad Gateway",
		"503 Service Unavailable",
		"504 Gateway Timeout",
		"505 HTTP Version Not Supported"
};


#define CONTENT_TYPE "Content-Type"
#define CONTENT_LENGTH "Content-Length"
#define TRANSFER_ENCODING "Transfer-Encoding"
#define TE_CHUNKED "chunked"
#define CONNECTION  "Connection"
#define CONN_CLOSE  "close"
#define SET_COOKIE  "Set-Cookie"
#define CRLF "\r\n"
#define DATE "Date"

std::atomic<std::size_t> HttpServerRequest::identCounter(0);

static std::string_view getStatusCodeMsg(int code) {
	char num[100];
	snprintf(num,100,"%d",code);
	std::string_view codestr(num);
	if (codestr.length() == 3) {

		auto f = std::lower_bound(
				std::begin(statusMessages),
				std::end(statusMessages),
				codestr, [](const std::string_view &a, const std::string_view &b){
			return a.substr(0,3) < b.substr(0,3);
		});
		if (f != std::end(statusMessages)) {
			if (f->substr(0,3) == codestr)
				return f->substr(4);
		}

	}
	return "Unexpected status";
}


std::size_t HttpServerRequest::maxChunkSize = 16384;

std::size_t HeaderValue::getUInt() const {
	std::size_t n = 0;
	for (char c: *this) {
		if (isdigit(c)) n = n * 10 + (c - '0'); else return 0;
	}
	return n;
}

bool HeaderValue::lessHeader(const std::pair<std::string_view, std::string_view> &a,
				const std::pair<std::string_view, std::string_view> &b) {
	auto ln = std::min(a.first.length(), b.first.length());
	for (std::size_t i = 0; i < ln; i++) {
		int c = std::tolower(a.first[i]) - std::tolower(b.first[i]);
		if (c) return c<0;
	}
	return (static_cast<int>(a.first.length()) - static_cast<int>(b.first.length())) < 0;
}

bool HeaderValue::iequal(const std::string_view &a, const std::string_view &b) {
	if (a.length() != b.length()) return false;
	auto ln = a.length();
	for (std::size_t i = 0; i < ln; i++) {
		int c = std::tolower(a[i]) - std::tolower(b[i]);
		if (c) return false;
	}
	return true;
}

bool HttpServerRequest::readHeader(std::string_view &buff, int &m) {
	int pos = 0;
	for (char c: buff) {
		switch (m) {
		case 0: if (c == '\r') {
					++m;
				} else {
					inHeaderData.push_back(c);
				} break;
		case 1: if (c == '\n') {
					++m;
				} else {
					inHeaderData.push_back('\r');
					if (c == '\r') {
						m = 1;
					} else {
						m = 0;
						inHeaderData.push_back(c);
					}
				} break;
		case 2: if (c == '\r') {
					++m;
				} else  {
					inHeaderData.push_back('\r');
					inHeaderData.push_back('\n');
					inHeaderData.push_back(c);
					m = 0;
				} break;
		case 3: if (c == '\n') {
					pos++;
					buff = buff.substr(pos);
					return true;
				} else {
					inHeaderData.push_back('\r');
					inHeaderData.push_back('\n');
					inHeaderData.push_back('\r');
					if (c == '\r') {
						m = 1;
					} else {
						m = 0;
						inHeaderData.push_back(c);
					}
				} break;
		}
		pos++;
	}
	return false;
}

void HttpServerRequest::setKeepAliveCallback(KeepAliveCallback &&kc) {
	klcb = std::move(kc);
}

bool HttpServerRequest::readHeader() {
	int m = 0;
	auto buf = stream.read();
	while (!buf.empty()) {
		if (readHeader(buf, m)) {
			stream.putBack(buf);
			return true;
		}
	}
	return false;
}

template<typename Fn>
void HttpServerRequest::readHeaderAsync(int m, Fn &&fn) {
	stream.readAsync([this, m, fn = std::move(fn)](std::string_view data) mutable {
		if (data.empty()) {
			fn(false);
			return;
		}
		initTime = std::chrono::system_clock::now();
		bool res = readHeader(data, m);
		if (res) {
			stream.putBack(data);
			fn(true);
		}
		else readHeaderAsync(m, std::move(fn));
	});
}


bool HttpServerRequest::parse() {

	std::string_view v(inHeaderData.data(), inHeaderData.size());
	if (!parseFirstLine(v)) return false;
	if (!parseHeaders(v)) return false;

	return true;
}

HeaderValue HttpServerRequest::get(const std::string_view &item) const {
	std::pair srch(item, std::string_view());
	auto iter =std::lower_bound(inHeader.begin(), inHeader.end(), srch, HeaderValue::lessHeader);
	if (iter == inHeader.end() || HeaderValue::lessHeader(srch, *iter)) return HeaderValue();
	else return HeaderValue(std::string_view(iter->second));
}

std::string_view HttpServerRequest::getMethod() const {
	return method;
}

std::string_view HttpServerRequest::getURI() const {
	return uri;
}

std::string_view HttpServerRequest::getHTTPVer() const {
	return httpver;
}

std::string_view HttpServerRequest::getHost() const {
	return host;
}

bool HttpServerRequest::parseFirstLine(std::string_view &v) {
	std::string_view fl;
	do {
		if (v.empty()) return false;
		fl = splitAt(CRLF, v);
	} while (fl.empty());
	auto x = fl.find(' ');
	if (x == fl.npos) return false;
	auto y = fl.find(' ',x+1);
	if (y == fl.npos) return false;
	std::copy(fl.begin(), fl.end(), std::back_inserter(firstLine));
	for (std::size_t i = 0; i < x; i++) firstLine[i] = std::toupper(firstLine[i]);
	for (std::size_t i = y, cnt = firstLine.size(); i < cnt; i++) firstLine[i] = std::toupper(firstLine[i]);
	method = std::string_view(firstLine.data(), x);
	uri = std::string_view(firstLine.data()+x+1, y - x - 1);
	httpver = std::string_view(firstLine.data()+y+1, firstLine.size() - y - 1);
	return true;
}


HttpServerRequest::HttpServerRequest()
{
}

void HttpServerRequest::reuse_buffers(HttpServerRequest &from) {
	//reuse buffers from other request to avoid reallocations
	std::swap(firstLine, from.firstLine);
	std::swap(inHeaderData, from.inHeaderData);
	std::swap(sendHeader, from.sendHeader);
	std::swap(logBuffer, from.logBuffer);
	firstLine.clear();
	inHeaderData.clear();
	sendHeader.clear();
	logBuffer.clear();
}

std::size_t HttpServerRequest::getIdent() const {
	return ident;
}

void HttpServerRequest::initAsync(Stream &&stream, CallbackT<void(bool)> &&initDone) {
	this->stream = std::move(stream);
	ident = ++identCounter;
	readHeaderAsync(0, [this, initDone = std::move(initDone)](bool v){
		initTime = std::chrono::system_clock::now();
		valid = v && parse() && processHeaders();
		if (logger) logger->log(ReqEvent::init, *this);
		initDone(valid);
	});

}

const std::chrono::system_clock::time_point &HttpServerRequest::getRecvTime() const {
	return initTime;
}

bool HttpServerRequest::init(Stream &&stream) {
	this->stream = std::move(stream);
	ident = ++identCounter;
	initTime = std::chrono::system_clock::now();
	valid = readHeader() && parse() && processHeaders();
	if (logger) logger->log(ReqEvent::init, *this);
	return valid;
}

HttpServerRequest::~HttpServerRequest() {
	try {
		if (stream.valid()) {
			if (!response_sent) {
				set(CONNECTION,CONN_CLOSE);
				if (!valid) {
					enableKeepAlive = false;
					if (httpver.substr(0,5) == "HTTP/1") {
						sendErrorPage(400);
					} else {
						logger = nullptr;
					}
				}
				else sendErrorPage(204);
			}
			stream.flush();
			if (logger) logger->log(ReqEvent::done,*this);
			if (enableKeepAlive && hasBody && klcb != nullptr) klcb(stream, *this);
		}
	} catch (...) {

	}
}

bool HttpServerRequest::isValid() {
	return valid;
}

Stream HttpServerRequest::getBody() {
	Stream bodyStream;
	if (hasBody) {
		auto te = get(TRANSFER_ENCODING);
		auto ctlh = get(CONTENT_LENGTH);
		if (method == "GET" || method == "HEAD") {
			bodyStream = Stream(std::make_unique<LimitedStream<Stream &> >(stream,0,0));
		} else if (te.defined && te != TE_CHUNKED) {
			bodyStream = Stream(std::make_unique<ChunkedStream<Stream &> >(stream, 0, true));
		} else if (ctlh.defined) {
			auto ctl = ctlh.getUInt();
			bodyStream = Stream(std::make_unique<LimitedStream<Stream &> >(stream,ctl,0));
		} else {
			bodyStream = Stream(std::make_unique<LimitedStream<Stream &> >(stream,0,0));
		}
		if (hasExpect) {
			stream.writeNB(httpver);
			stream.writeNB(" ");
			stream.writeNB("100 Continue\r\n\r\n");
			stream.flush();
		}
		hasBody = false;
	} else {
		bodyStream = Stream(std::make_unique<LimitedStream<Stream &> >(stream,0,0));
	}
	return bodyStream;

}

bool HttpServerRequest::processHeaders() {
	auto te = get(TRANSFER_ENCODING);
	auto ctlh = get(CONTENT_LENGTH);
	if (te.defined && te != TE_CHUNKED && !ctlh.defined) {
		sendErrorPage(411);return false;
	}
	hasBody = te == TE_CHUNKED || (ctlh.defined && ctlh != "0");
	host = get("Host");

	if (httpver == "HTTP/1.1") {
		if (get(CONNECTION) != CONN_CLOSE) {
			enableKeepAlive = true;
		}
	} else {
		if (get(CONNECTION) == "keep-alive") {
			enableKeepAlive = true;
		}
	}
	auto expect = get("Expect");
	if (expect.defined) {
		if (!HeaderValue::iequal(expect ,"100-continue")) {
			sendErrorPage(417);
			return false;
		} else {
			hasExpect = true;
		}
	}

	return true;
}


void HttpServerRequest::set(const std::string_view &key, const std::string_view &value) {
	if (HeaderValue::iequal(key, CONTENT_TYPE)) {
		has_content_type = true;
	}
	else if (HeaderValue::iequal(key, CONTENT_LENGTH)) {
		has_content_length = true;
		HeaderValue hv(value);
		send_content_length = hv.getUInt();
	}
	else if (HeaderValue::iequal(key, DATE)) {
		has_date = true;
	}
	else if (HeaderValue::iequal(key, TRANSFER_ENCODING)) {
		has_transfer_encoding = true;
		if (HeaderValue::iequal(value,TE_CHUNKED)) {
			has_transfer_encoding_chunked = true;
		}
	}
	else if (HeaderValue::iequal(key, CONNECTION)) {
		has_connection = true;
		if (HeaderValue::iequal(value, CONN_CLOSE)) enableKeepAlive = false;
	} else if (HeaderValue::iequal(key, "Last-Modified") || HeaderValue::iequal(key, "ETag")) {
		has_last_modified = true;
	}
	else if (HeaderValue::iequal(key, "Server")) {
		has_server = true;
	}

	std::string_view crlf(CRLF);
	std::string_view sep(": ");
	std::copy(crlf.begin(), crlf.end(), std::back_inserter(sendHeader));
	std::copy(key.begin(), key.end(), std::back_inserter(sendHeader));
	std::copy(sep.begin(), sep.end(), std::back_inserter(sendHeader));
	std::copy(value.begin(), value.end(), std::back_inserter(sendHeader));
}

static char *numToStr(char *buff, std::size_t x) {
	if (x) {
		char *c = numToStr(buff, x/10);
		*c = '0'+(x%10);
		return c+1;
	} else {
		return buff;
	}
}

static void putCode(Stream &stream, std::size_t x) {
	if (x) {
		putCode(stream, x/10);
		stream.putCharNB('0' + (x%10));
	}
}

void HttpServerRequest::set(const std::string_view &key, std::size_t number) {
	if (number == 0) {
		set(key,"0");
	}
	else {
		char buff[40];
		*numToStr(buff, number) = 0;
		set(key, buff);
	}
}

void HttpServerRequest::setStatus(int code) {
	statusCode = code;
	statusMessage.clear();
}

void HttpServerRequest::setStatus(int code, const std::string_view &message) {
	statusCode = code;
	statusMessage = message;
}

void HttpServerRequest::setContentType(const std::string_view &contentType) {
	set("Content-Type",contentType);
}

void HttpServerRequest::send(const std::string_view &body) {
	if (!has_content_length && statusCode != 204 && statusCode != 304) {
		set("Content-Length", body.length());
	}
	Stream s = send();
	s.write(body);
	s.flush();
}

Stream HttpServerRequest::send() {
	if (response_sent) {
		throw std::runtime_error("Response already sent (can't use send() twice during single request)s");
	}
	if (hasBody && !hasExpect) {
		//handler did not pick body - and Expect: 100-continue is not set
		//this can happen, when error is send without reading body
		//this request cannot be kept alive, because body will not be read
		enableKeepAlive = false;
	}
	bool nocontent = statusCode == 204 || statusCode == 304;
	if (!nocontent) {
		if (!has_content_type) {
			set("Content-Type","application/octet-stream");
		}
		if (!has_transfer_encoding) {
			if (!has_content_length) {
				if (enableKeepAlive && httpver == "HTTP/1.1") {
					set("Transfer-Encoding", "chunked");
				} else if (has_connection) {
					set("Connection","close");
				}
			}
		}
	}
	if (!has_connection && !enableKeepAlive) {
		set("Connection","close");
	}
	if (!has_date) {
		 char buf[256];
		 time_t now = time(0);
		 struct tm tm;
#ifdef _WIN32
		 gmtime_s(&tm, &now);
#else 
		 gmtime_r(&now, &tm);
#endif
		 strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
		 set("Date", buf);
	}

	if (!has_server) {
		set("Server","userver");
	}

	std::string_view statusMsg;
	if (statusMessage.empty()) statusMsg = getStatusCodeMsg(statusCode); else statusMsg = statusMessage;
	stream.writeNB(httpver);
	stream.putCharNB(' ');
	putCode(stream, statusCode);
	stream.putCharNB(' ');
	stream.writeNB(statusMsg);
	stream.writeNB(std::string_view(sendHeader.data(), sendHeader.size()));
	stream.writeNB("\r\n\r\n");
	response_sent = true;
	if (logger) logger->log(ReqEvent::header_sent,*this);
	if (nocontent || method == "HEAD" ) {
		return Stream(std::make_unique<LimitedStream<Stream &> >(stream, 0, 0));
	} else if (has_transfer_encoding_chunked) {
		return Stream(std::make_unique<ChunkedStream<Stream &> >(stream, maxChunkSize,false));
	} else if (has_content_length) {
		return Stream(std::make_unique<LimitedStream<Stream &> >(stream, 0, send_content_length));
	} else {
		return Stream(stream.makeReference());
	}
}

unsigned int HttpServerRequest::getStatus() const {
	return statusCode;
}

std::intptr_t HttpServerRequest::getResponseSize() const {
	if (has_content_length) return send_content_length;
	else return -1;
}

void HttpServerRequest::sendErrorPage(int code) {
	sendErrorPage(code, std::string_view());
}

void HttpServerRequest::sendErrorPage(int code, const std::string_view &description) {
	if (!response_sent) {
		std::ostringstream body;
		auto msg = getStatusCodeMsg(code);
		body << "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
				"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Transitional//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-transitional.dtd\">"
				"<html xmlns=\"http://www.w3.org/1999/xhtml\">"
				"<head>"
				"<title>" << code << " " << msg<<"</title>"
				"</head>"
				"<body>"
				"<h1>"  << code << " " << msg <<"</h1>"
				"<p><![CDATA[" << description << "]]></p>"
				"</body>"
				"</html>";
		setContentType("application/xhtml+xml");
		setStatus(code);
		send(body.str());
	}

}

void HttpServerRequest::setCookie(const std::string_view &name,
		const std::string_view &value, const CookieDef &cookieDef) {
	std::ostringstream def;
	def << name << "=" << '"' << value << '"';
	if (!cookieDef.domain.empty()) def << "; Domain=" << cookieDef.domain;
	if (!cookieDef.path.empty()) def << "; Path=" << cookieDef.path;
	if (!cookieDef.max_age) def << "; Max-Age=" << cookieDef.max_age;
	if (!cookieDef.secure) def << "; Secure";
	if (!cookieDef.http_only) def << "; HttpOnly";
	set("Set-Cookie", def.str());


}


bool HttpServerRequest::parseHeaders(std::string_view &dt) {
	while (!dt.empty()) {
		auto ln = splitAt(CRLF, dt);
		auto key = splitAt(":", ln);
		auto value = ln;
		trim(key);
		trim(value);
		inHeader.push_back(std::pair(key,value));
	}
	std::sort(inHeader.begin(), inHeader.end(), HeaderValue::lessHeader);
	return true;
}

static std::pair<std::string_view, std::string_view> mimeTypes[] = {

       {"txt","text/plain"},
       {"htm","text/html"},
       {"html","text/html"},
       {"php","text/html"},
       {"css","text/css"},
       {"js","application/javascript"},
       {"json","application/json"},
       {"xml","application/xml"},
       {"swf","application/x-shockwave-flash"},
       {"flv","video/x-flv"},

       // images
       {"png","image/png"},
       {"jpe","image/jpeg"},
       {"jpeg","image/jpeg"},
       {"jpg","image/jpeg"},
       {"gif","image/gif"},
       {"bmp","image/bmp"},
       {"ico","image/vnd.microsoft.icon"},
       {"tiff","image/tiff"},
       {"tif","image/tiff"},
       {"svg","image/svg+xml"},
       {"svgz","image/svg+xml"},

       // archives
       {"zip","application/zip"},
       {"rar","application/x-rar-compressed"},
       {"exe","application/x-msdownload"},
       {"msi","application/x-msdownload"},
       {"cab","application/vnd.ms-cab-compressed"},

       // audio/video
       {"mp3","audio/mpeg"},
       {"qt","video/quicktime"},
       {"mov","video/quicktime"},

       // adobe
       {"pdf","application/pdf"},
       {"psd","image/vnd.adobe.photoshop"},
       {"ai","application/postscript"},
       {"eps","application/postscript"},
       {"ps","application/postscript"},

       // ms office
       {"doc","application/msword"},
       {"rtf","application/rtf"},
       {"xls","application/vnd.ms-excel"},
       {"ppt","application/vnd.ms-powerpoint"},

       // open office
       {"odt","application/vnd.oasis.opendocument.text"},
       {"ods","application/vnd.oasis.opendocument.spreadsheet"}
};

bool HttpServerRequest::sendFile(std::unique_ptr<HttpServerRequest> &&reqptr, const std::string_view &pathname) {
	using namespace std::filesystem;
	try {
	path p(pathname);

	if (!reqptr->has_last_modified) {
		auto wrm = last_write_time(p);
		auto wrt = wrm.time_since_epoch().count();
		static char hexChars[]="0123456789ABCDEF";

		char *hexBuff = (char *)alloca(sizeof(wrt)*2+3);
		char *p = hexBuff;
		*p++='"';
		std::basic_string_view<unsigned char> data(reinterpret_cast<const unsigned char *>(&wrt), sizeof (wrt));
		for (unsigned int c : data) {
			*p++ = hexChars[c>>4];
			*p++ = hexChars[c & 0xF];
		}
		*p++='"';
		*p = 0;
		std::string_view curEtag(hexBuff, p-hexBuff);
		for (auto etg = std::string_view(reqptr->get("If-None-Match")); !etg.empty();) {
			auto tag = splitAt(",", etg);
			trim(tag);
			if (tag == curEtag) {
				reqptr->setStatus(304);
				reqptr->send("");
				return true;
			}
		}
		reqptr->set("ETag", curEtag);
	}
	if (!reqptr->has_content_type) {
		std::string_view content_type;

		auto ext = p.extension().string();
		if (ext.length() > 1) {
			std::string_view exte = std::string_view(ext).substr(1);
			for (auto &&itm : mimeTypes) {
				if (itm.first == exte) {
					content_type = itm.second;
					break;
				}
			}
		}
		if (content_type.empty()) {
			content_type ="application/octet-stream";
		}

		reqptr->set(CONTENT_TYPE, content_type);
	}
	std::unique_ptr<std::istream> file (std::make_unique<std::fstream>(p, std::ios::binary | std::ios::in ));
	if (!(*file)) {
		return false;
	} else {
		file->seekg(0,std::ios::end);
		std::size_t sz = file->tellg();
		if (sz == 0) {
			reqptr->sendErrorPage(204);
		} else {
			file->seekg(0,std::ios::beg);
			int p = file->get();
			if (p == EOF) {
				reqptr->sendErrorPage(403);
			} else{
				file->putback(p);
				reqptr->set(CONTENT_LENGTH,sz);
				Stream out = reqptr->send();

				sendFileAsync(reqptr, file, out);
			}
		}
	}
	return true;
	} catch (std::exception &e) {
		reqptr->log(e.what());
		return false;
	}
}

void HttpServerRequest::sendFileAsync(std::unique_ptr<HttpServerRequest> &reqptr, std::unique_ptr<std::istream>&in, Stream &out) {
	char buff[10000];
	while (!(!(*in))) {
		in->read(buff,sizeof(buff));
		std::size_t cnt = in->gcount();
		if (cnt) {
			if (out.writeNB(std::string_view(buff, cnt))) {
				out.flushAsync([reqptr = std::move(reqptr),  in = std::move(in)](Stream &out, bool ok) mutable {
					if (ok) {
						sendFileAsync(reqptr, in, out);
					}
				});
				return;
			}
		} else {
			break;
		}
	}
	//empty flush async, just held request until flushed
	out.flushAsync([reqptr = std::move(reqptr)](Stream &, bool ) {
		// empty
	});
}

void HttpServerMapper::addPath(const std::string_view &path, Handler &&handler) {
	std::lock_guard _(mapping->shrmux);
	if (handler == nullptr) mapping->pathMapping.erase(std::string(path));
	else mapping->pathMapping.emplace(std::string(path), std::move(handler));
}

void HttpServerMapper::serve(Stream &&stream) {
	auto req = std::make_unique<HttpServerRequest>();
	req->init(std::move(stream));
	if (!execHandlerByHost(req)) {
		req->sendErrorPage(404);
	}
}

bool HttpServerMapper::execHandler(PHttpServerRequest &req, const std::string_view &vpath) {
	try {
		std::shared_lock _(mapping->shrmux);
		std::string_view curvpath = vpath;
		curvpath = splitAt("?", curvpath);

		while (true) {
			auto iter = mapping->pathMapping.find(curvpath);
			if (iter != mapping->pathMapping.end()) {
				std::string_view restPath = vpath.substr(curvpath.size());
				if (req == nullptr || iter->second(req, restPath)) return true;
			}
			auto spos = curvpath.rfind('/');
			if (spos == curvpath.npos) break;
			curvpath = curvpath.substr(0,spos);
		}
		return false;
	} catch (std::exception &e) {
		if (req != nullptr) {
			req->log("Exception:", e.what());
			req->sendErrorPage(500);
		}
		return true;
	} catch (...) {
		if (req != nullptr) {
			req->sendErrorPage(500);
		}
		return true;
	}
}

HttpServerMapper::HttpServerMapper() {
	mapping = std::make_shared<PathMapping>();
}

bool HttpServerMapper::execHandlerByHost(PHttpServerRequest &req) {

	if (!req->isValid()) return true;
	std::string host (req->getHost());
	std::string vpathbuff ( req->getURI());
	std::string_view vpath(vpathbuff);
	std::string_view prefix;
	std::shared_lock _(mapping->shrmux);
	auto iter = mapping->hostMapping.find(host);
	auto iend = mapping->hostMapping.end();
	if (iter != iend) prefix = iter->second;

	if (vpath == "/" && req->getMethod() == "DELETE") {//special uri - clear mapping
		if (iter != iend) {
			_.unlock();
			std::unique_lock __(mapping->shrmux);
			mapping->hostMapping.erase(iter);
		}
		req->sendErrorPage(202);
		return true;
	}

	auto plen = prefix.length();
	if (vpath.substr(0,plen) == prefix
		&& vpath.length() > plen
		&& vpath[plen] == '/'
		&& execHandler(req, vpath.substr(plen))) {
			return true;
	}

	auto q = vpath.find('?');
	auto p = vpath.find('/',1);
	while (p < q) {
		prefix = vpath.substr(0, p);
		if (iter != iend && prefix.length() > plen) break;
		if (execHandler(req, vpath.substr(p))) {
			_.unlock();
			std::unique_lock __(mapping->shrmux);
			mapping->hostMapping[host] = prefix;
			return true;
		}
		p = vpath.find('/',p+1);
	}
	return false;
}

class Logger: public HttpServerRequest::ILogger {
public:
	Logger(HttpServer &owner):owner(owner) {}
	virtual void handler_log(const HttpServerRequest &req, const std::string_view &msg) noexcept;
	virtual void log(ReqEvent event, const HttpServerRequest &req) noexcept;
	HttpServer &owner;
};

void HttpServer::start(NetAddrList listenSockets, unsigned int threads, unsigned int dispatchers) {
	if (socketServer.has_value()) return;

	socketServer.emplace(listenSockets);
	AsyncProvider a = createAsyncProvider(dispatchers);
	for (unsigned int i = 0; i < threads; i++) {
		this->threads.emplace_back([a]() mutable {
			setThreadAsyncProvider(a);
			a.start_thread();
		});
	}
	asyncProvider = a;

	logger = std::make_unique<Logger>(*this);


	a.runAsync([=]{
		listen();
	});
}

void HttpServer::listen() {
	socketServer->waitAcceptAsync([&](std::optional<SocketServer::AcceptInfo> &acpt) {
		if (acpt.has_value()) {
			acpt->sock.setIOTimeout(5000);
			Stream s(std::make_unique<SocketStream>(std::make_unique<Socket>(std::move(acpt->sock))));
			if (!onConnect(s)) {
				PHttpServerRequest req = std::make_unique<HttpServerRequest>();
				beginRequest(std::move(s), std::move(req));
			}
			this->listen();
		}
	});
}

void HttpServer::addThread() {
	setThreadAsyncProvider(asyncProvider);
	asyncProvider.start_thread();
	setThreadAsyncProvider(nullptr);
}

AsyncProvider HttpServer::getAsyncProvider() {
	return asyncProvider;
}

HttpServer::~HttpServer() {
	stop();
}

void HttpServer::stop() {
	if (asyncProvider) {
		asyncProvider->stop();
	}
	for (auto &t: threads) t.join();
	threads.clear();
}

void HttpServer::log(ReqEvent event, const HttpServerRequest &req) {
	if (event == ReqEvent::done) {
		std::lock_guard _(lock);
		buildLogMsg(std::cout, req);
	}
}

void HttpServer::log(const HttpServerRequest &, const std::string_view &msg) {
	std::lock_guard _(lock);
	buildLogMsg(std::cout, msg);
}

void HttpServer::beginRequest(Stream &&s, PHttpServerRequest &&req) {
	req->setLogger(logger.get());
	s.readAsync([this, req = std::move(req)](Stream &s, const std::string_view &data) mutable {
		if (!data.empty()) {
			s.putBack(data);
			HttpServerRequest *preq = req.get();
			preq->initAsync(std::move(s), [req = std::move(req), this](bool v) mutable {
				if (v) {
					req->setKeepAliveCallback([this](Stream &s, HttpServerRequest &req){
						PHttpServerRequest newreq = std::make_unique<HttpServerRequest>();
						newreq->reuse_buffers(req);
						beginRequest(std::move(s), std::move(newreq));
					});
					if (!execHandlerByHost(req)) {
						req->sendErrorPage(404);
					}
				}
			});
		}
	});
}

void HttpServerRequest::log2() {
	logger->handler_log(*this, std::string_view(logBuffer.data(), logBuffer.size()));
	logBuffer.clear();
}

void HttpServer::process(Stream &&s) {
	if (getCurrentAsyncProvider() != asyncProvider) {
		asyncProvider.runAsync([s = std::move(s),this]() mutable {
			process(std::move(s));
		});
	} else {
		PHttpServerRequest req = std::make_unique<HttpServerRequest>();
		beginRequest(std::move(s), std::move(req));
	}
}

void HttpServer::buildLogMsg(std::ostream &stream, const HttpServerRequest &req) {
	auto now = std::chrono::system_clock::now();
	auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - req.getRecvTime()).count();
	auto sz = req.getResponseSize();
	stream.width(8);
	stream << dur << ' ';
	stream.width(8);
	if (sz == -1) stream<<"n/a"; else stream << sz;
	stream << ' ';
	stream.width(3);
	stream << req.getStatus() << ' ';
	stream.width(8);
	stream << req.getMethod() << ' ';
	stream.width(0);
	stream << req.getHost();
	stream << req.getURI();
	stream << std::endl;
}

void HttpServer::buildLogMsg(std::ostream &stream, const std::string_view &msg) {
	stream << msg << std::endl;
}

void formatToLog(std::vector<char> &log, const std::string_view &txt) {
	std::copy(txt.begin(), txt.end(), std::back_inserter(log));
}

void formatToLog(std::vector<char> &log, const std::intptr_t &v) {
	formatToLog(log, std::to_string(v));
}

void formatToLog(std::vector<char> &log, const std::uintptr_t &v) {
	formatToLog(log, std::to_string(v));
}

void formatToLog(std::vector<char> &log, const double &v) {
	formatToLog(log, std::to_string(v));
}

void Logger::handler_log(const HttpServerRequest &req, const std::string_view &msg) noexcept {
	owner.log(req, msg);
}

void Logger::log(ReqEvent event, const HttpServerRequest &req) noexcept {
	owner.log(event, req);
}

Stream& HttpServerRequest::getStream() {
	return stream;
}

bool HttpServerRequest::directoryRedir() {
	auto astq = uri.find('?');
	std::string_view query;
	std::string_view path;
	if (astq != uri.npos) {
		path = uri.substr(0,astq);
		query = uri.substr(astq);
	} else {
		path = uri;
	}
	if (path.empty() || path.back() != '/') {
		std::string newuri;
		newuri.reserve(path.length()+query.length()+1);
		newuri.append(uri);
		newuri.push_back('/');
		newuri.append(query);
		setStatus(301);
		set("Location",newuri);
		send("");
		return true;
	} else {
		return false;
	}
}

}
