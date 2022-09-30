/*
 * http_client.h
 *
 *  Created on: 28. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_HTTP_CLIENT_H_
#define SRC_USERVER_HTTP_CLIENT_H_

#include <string_view>
#include <utility>
#include <vector>
#include <optional>

#include "helpers.h"
#include "netaddr.h"
#include "stream.h"
#include "header_value.h"
#include <sstream>
#include "coclasses.h"


namespace userver {


class HttpClientRequest {
public:

	HttpClientRequest(Stream &&s);

	///Open request
	/**
	 * Opens the request. Note the function is stupid, it doesn't check current request state. You
	 * need to call this function as very first action before the request is built
	 *
	 * @param method HTTP method (GET, POST, PUT, etc...)
	 * @param host target host. Note that this only prepares Host header. Target server must be already connected
	 * @param path path (note, not URL, only relative path to host)
	 *
	 * @note Don't forget to call addHeader("User-Agent",....)
	 *
	 * @note You can reuse existing request when it was processed, to open new request. However
	 * you need ensure, that previous request has been fully processed
	 */
	void open(const std::string_view &method, const std::string_view &host, const std::string_view &path);
	///Add header
	/**
	 * Adds header. Note the function is stupid, it doesn't check current request state. You can
	 * add headers until the beginBody() or send() is called, otherwise it damages communication
	 *
	 * @param key header key
	 * @param value header value - no escaping is involved, always check values especially when passed from users
	 */
	void addHeader(const std::string_view &key, const std::string_view &value);
	///Adds header (numeric value)
	/**
	 * Adds header. Note the function is stupid, it doesn't check current request state. You can
	 * add headers until the beginBody() or send() is called, otherwise it damages communication
	 *
	 * @param key header key
	 * @param value header value - no escaping is involved, always check values especially when passed from users
	 */
	void addHeader(const std::string_view &key, std::size_t value);
	///Sets expected length of the body
	/** It is better to set the length of the body, when it is known. This improves throughput
	 * @param sz expected size of the body.
	 *
	 * @note It is expected that if you set the body length, you will actually start the body
	 * otherwise the request is damaged
	 */
	void setBodyLength(std::size_t sz);
	///Sets content type
	/**
	 * @param ctx content tytpe
	 *
	 * @note shortcut to addHeader("Content-Type",ctx)
	 */
	void setContentType(const std::string_view &ctx);
	///Begins the body
	/**
	 * When this function is called, no futher headers can be added. It creates body part
	 * and returns the stream. You need to send the body through the stream. To finish the body,
	 * use send() or sendAsync()
	 * @return stream to send body
	 *
	 * @note data are send directly to the opened stream
	 *
	 * @note if length of the body was not set, then chunked transfer is used
	 */
	Stream &beginBody();
	
	
	template<typename CB>
	void beginBodyAsync(CB &&cb) {
	    if (!header_sent) {
	        finish_headers(true);
	        s.write_async(buff, [this, cb = std::forward<CB>(cb)](bool) mutable {           
	            cb(beginBody());
	        });
	    } else {
	        cb(beginBody());
	    }
	}

	class SendHelper {
	public:
		SendHelper(HttpClientRequest &owner):owner(owner) {};
		SendHelper(const SendHelper &other) = delete;
		operator int() {
			if (!status.has_value()) status = owner.sendSync();
			return *status;
		}
		template<typename Fn> void operator >> (Fn &&fn) {
			status=100;
			sendAsync(std::move(fn));
		}
		~SendHelper() noexcept(false) {
			if (!status.has_value()) status = owner.sendSync();
		}

	protected:
		HttpClientRequest &owner;
		std::optional<int> status;
		template<typename Fn> auto sendAsync(Fn &&fn) -> decltype(std::declval<Fn>()()) {
			owner.sendAsync([fn = std::move(fn)](int){
				fn();
			});
		}
		template<typename Fn> auto sendAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<int>())) {
			owner.sendAsync(std::forward<Fn>(fn));
		}
	};

	///Sends the prepared request
	/**
	 * The function just create helper object. You can attach a callback function to process request
	 * asynchronously. The callback function receives zero or one parameter (status as int).
	 *
	 * If you doesn't pick return value, the function is executed synchronously
	 */
	SendHelper send() {return SendHelper(*this);}

	///Send the prepared request
	/** Sends prepared request. It finalizes body, if it was started by beginBody(). Note that
	 * stream for the body becomes unavailable
	 *
	 * the function waits for response
	 *
	 * @return status code. Function returns -1 when the connection has been lost or
	 * timeouted
	 */
	int sendSync();

	///Send request with body,
	/**
	 * Function calls beginBody(), then writes body to the stream and finally calls send()
	 * to finalize request. Body is presented as function, which can generate object which can
	 * be converted to std::string_view. These objects are pushed to the stream synchronously
	 */
	template<typename Fn, typename = decltype(std::string_view(std::declval<Fn>()))>
	int send(Fn &&body);


	///Sends request with body asynchronously
	/**

	 */
	template<typename Fn, typename = decltype(std::string_view(std::declval<Fn>()()))>
	void sendAsync(Fn &&body, CallbackT<void(int)> &&cb);

	///Requests 100-continue
	/** This allows to validate request by the server before the body is actually send. The
	 * function finishes the header and sends it to the server. Then the response is expected.
	 *
	 * If the response is 100, you can call beginBody(), create the body and call send() to
	 * finalise the request. If a different response is received, the request is finished,
	 * you cannot send the body.
	 *
	 * @return a value 100 to continue with the body, other value to reject the request.
	 *
	 * @code
	 * req.open(...);
	 * req.addHeader(...);
	 * int status = req.requestContinue();
	 * if (status == 100) {
	 * 	  Stream &s = req.beginBody();
	 * 	  s.write(...);
	 * 	  status = req.send();
	 * }
	 *
	 * @endcode
	 */
	int requestContinue();
	///calls requestContinue asynchronously
	/**
	 * @param cb callback function
	 */
	void requestContinueAsync(CallbackT<void(int)> &&cb);

	///Retrieves last status
	int getStatus() const;
	///Retrieves last status message
	std::string_view getStatusMessage();
	///Retrieves protocol
	std::string_view getProtocol();
	///Retrieves response header
	HeaderValue get(const std::string_view &key) const;
	///Retrieves stream to response
	Stream &getResponse();

	///Retrieve underlying steam
	/** It is useful for websockets etc
	 *
	 * Note transfer encoding is not applied
	 */
	Stream &getStream();

	using HeaderMap = std::vector<std::pair<std::string_view, std::string_view> >;
	using iterator = HeaderMap::const_iterator;

	iterator begin() const;
	iterator end() const;
	std::pair<iterator, iterator> find(const std::string_view &key) const;
	const HeaderMap &getHeaders();


	///Returns host of current opened request
	const std::string &getHost() const {return host;}


protected:
	///Sends request asynchronously
	/**
	 * @param cb a callback function is called with a status code.
	 *
	 * @see send
	 */
	void sendAsync(CallbackT<void(int)> &&cb);

	void addHeaderInternal(const std::string_view &key, const std::string_view &value);

	Stream s;
	std::stringstream buff;
	std::string host;
	bool has_te = false;
	bool has_te_chunked = false;
	bool header_sent = false;
	bool head_method = false;
	std::optional<std::size_t> send_ctx_len;
	std::optional<Stream> userStream;
	std::string responseBuffer;
	std::string_view st_message;
	std::string_view protocol;
	HeaderMap responseHeaders;
	int status = -1;

	void finish_headers(bool message);
	void prepareUserStream();
	bool parseResponse();
	void finish_headers_excpect_100();

	template<typename Fn>
	void sendAsyncCont(Fn &&body, CallbackT<void(int)> &&cb);

};

using PHttpClientRequest = std::unique_ptr<HttpClientRequest>;

struct HttpClientCfg {

	using PSocket = std::unique_ptr<ISocket>;

	std::string userAgent;
	int connectTimeout = 30000;
	int iotimeout =30000;
	CallbackT<PSocket(const NetAddr &, const std::string_view &host)> connect;
	CallbackT<PSocket(const NetAddr &, const std::string_view &host)> sslConnect;
	CallbackT<NetAddrList(const std::string_view &)> resolve;
};

class HttpClient {
public:

	using URL = std::string_view;
	using Data = std::string_view;
	using Method = std::string_view;
	using Callback = CallbackT<void(std::unique_ptr<HttpClientRequest> &&)>;
	using DataStream = CallbackT<std::string_view(void)>;


	HttpClient(HttpClientCfg &&cfg);

	class OpenHelper {
	public:
		OpenHelper(HttpClient &owner,const Method &method,const URL &url)
		:owner(owner),method(method),url(url) {}
		OpenHelper(const OpenHelper &) = delete;
		operator PHttpClientRequest() {
			return owner.openSync(method,url);
		}
		template<typename Fn>void operator >> (Fn &&fn) {openAsync(std::move(fn));}

	protected:
		template<typename Fn> auto openAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<PHttpClientRequest>())) {
			owner.openAsync(method, url, std::forward<Fn>(fn));
		}
		HttpClient &owner;
		const Method &method;
		const URL &url;
	};

	///Open the request
	/**
	 * @param method to open: GET, POST, PUT, etc
	 * @param url url to open.
	 * @return function returns PHttpClientRequest (you need to request PHttpClientRequest, auto will
	 * not work). You can also chain callback function to process the operation asynchronously
	 *
	 * @code
	 * httpc.open("GET","http://example.com") >> [=](PHttpClientRequest &req) {....};
	 * @endcode
	 */
	OpenHelper open(const Method &method,const URL &url) {
		return OpenHelper(*this,method, url);
	}

	using HeaderPair = std::pair<std::string_view, std::string_view>;

	class HeaderList {
	public:
		HeaderList(const std::initializer_list<HeaderPair> &lst);
		HeaderList(const std::vector<HeaderPair> &lst);
		HeaderList(const HeaderPair *lst, std::size_t count);

		const HeaderPair &operator[](int index) const {return ptr[index];}
		const HeaderPair *begin() const {return ptr;}
		const HeaderPair *end() const {return ptr+count;}

	protected:
		const HeaderPair *ptr;
		std::size_t count;
	};


	std::unique_ptr<HttpClientRequest> GET(const URL &url, HeaderList headers);
	std::unique_ptr<HttpClientRequest> POST(const URL &url, HeaderList headers, const Data &data);
	std::unique_ptr<HttpClientRequest> PUT(const URL &url, HeaderList &headers, const Data &data);
	std::unique_ptr<HttpClientRequest> DELETE(const URL &url, HeaderList headers, const Data &data);
	std::unique_ptr<HttpClientRequest> DELETE(const URL &url, HeaderList headers);

	void GET(const URL &url, HeaderList headers, Callback &&cb);
	void POST(const URL &url, HeaderList headers, const Data &data, Callback &&cb);
	void PUT(const URL &url, HeaderList &headers, const Data &data, Callback &&cb);
	void DELETE(const URL &url, HeaderList headers, const Data &data, Callback &&cb);
	void DELETE(const URL &url, HeaderList headers, Callback &&cb);

#ifdef SRC_LIBS_USERVER_COCLASSES_H_
	cocls::task<std::unique_ptr<HttpClientRequest> > async_GET(const URL &url, HeaderList headers);
#endif

protected:
	///Open new request
	/**
	 * @param method method
	 * @param url url
	 * @return connected request. If result is null, then connection cannot be established
	 */
	std::unique_ptr<HttpClientRequest> openSync(const Method &method,
											const URL &url);

	///Open request asynchronously
	/**
	 * @param method method
	 * @param url url
	 * @param callback function called, when request is ready. The argument contains
	 * connected request. If the argument is null, then connection cannot be established
	 */
	void openAsync(const Method &method, const URL &url, Callback &&callback);

	HttpClientCfg cfg;

	struct CrackedURL {
		bool valid = false;
		bool ssl = false;
		int port = 80;
		std::string_view host;
		std::string_view path;
		std::string_view auth;
		std::string_view domain;
	};

	CrackedURL crackUrl(const std::string_view &url);
	NetAddrList resolve(const CrackedURL &cu);
	std::unique_ptr<ISocket> connect(const NetAddr &addr, const CrackedURL &cu);

	template<typename Fn>
	void connectAsync(NetAddrList &&list, CrackedURL &&cu, Fn &&fn, unsigned int index);

public:

	std::unique_ptr<HttpClientRequest> sendRequest(const Method &method, const URL &url, HeaderList headers);
	std::unique_ptr<HttpClientRequest> sendRequest(const Method &method, const URL &url, HeaderList headers, const Data &data);
	void sendRequest(const Method &method, const URL &url, HeaderList headers, Callback &&cb);
	void sendRequest(const Method &method, const URL &url, HeaderList headers, DataStream &&data, Callback &&cb);
	void sendRequest(const Method &method, const URL &url, HeaderList headers, const Data &data, Callback &&cb);


};

template<typename Fn, typename>
inline int HttpClientRequest::send(Fn &&body) {
	Stream &s=beginBody();
	std::string_view data = body();
	while (!data.empty()) {
		s.write(data);
		 data = body();
	}
	return send();
}

template<typename Fn, typename>
inline void HttpClientRequest::sendAsync(Fn &&body, CallbackT<void(int)> &&cb) {
	beginBodyAsync([this, cb = std::move(cb), body = std::forward<Fn>(body)](Stream &s) mutable {
	    sendAsyncCont(std::forward<Fn>(body),std::move(cb));
	});

}

template<typename Fn>
inline void userver::HttpClientRequest::sendAsyncCont(Fn &&body, CallbackT<void(int)> &&cb) {
    //read body part
    std::string_view data = body();
    //empty means end, not empty
    if (!data.empty()) {
        //for small buffer
        if (data.size()<1000) {
            //use stringstream to collect more data
            buff.str(std::string());
            //write first part
            buff.write(data.data(),data.size());
            //repeat until buffer is larger or data are empty
            while (buff.tellp() < 4096) {
                //read next part of body
                data = body();
                //empty data, exit
                if (data.empty()) break;
                //append data
                buff.write(data.data(),data.size());
            }
            //write buffer asynchronously - continue when it is ok
            s.write_async(buff, [this,isend = data.empty(), cb = std::move(cb), body = std::forward<Fn>(body)](bool ok) mutable {
               //not ok - return -1
               if (!ok) cb(-1);
               //body is end? finish request
               else if (isend) sendAsync(std::move(cb));
               //else continue reading body
               else sendAsyncCont(std::forward<Fn>(body), std::move(cb));
            });
        } else {
            //body part is large enough, send it at once
            s.write_async(data, [this,cb = std::move(cb), body = std::forward<Fn>(body)](bool ok) mutable {
                if (!ok) cb(-1);
                else sendAsyncCont(std::forward<Fn>(body), std::move(cb));
            });
        }
    } else {
        //empty body, finish request
        sendAsync(std::move(cb));
    }
}

}


#endif /* SRC_USERVER_HTTP_CLIENT_H_ */

