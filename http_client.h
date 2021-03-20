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
	///Send the prepared request
	/** Sends prepared request. It finalizes body, if it was started by beginBody(). Note that
	 * stream for the bode becomes unavailable
	 *
	 * the function waits for response
	 *
	 * @return status code. Function returns -1 when the connection has been lost or
	 * timeouted
	 */
	int send();

	///Sends request asynchronously
	/**
	 * @param cb a callback function is called with a status code.
	 *
	 * @see send
	 */
	void sendAsync(CallbackT<void(int)> &&cb);

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

	using HeaderMap = std::vector<std::pair<std::string_view, std::string_view> >;
	using iterator = HeaderMap::const_iterator;

	iterator begin() const;
	iterator end() const;
	std::pair<iterator, iterator> find(const std::string_view &key) const;
	HeaderMap &getHeaders();


	///Returns host of current opened request
	const std::string &getHost() const {return host;}

	///Retrieves stream which carries whole response, which is released when stream is destroyed
	static Stream getResponseBody(std::unique_ptr<HttpClientRequest> &&req);

protected:

	void addHeaderInternal(const std::string_view &key, const std::string_view &value);

	Stream s;
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
	int status;

	void finish_headers(bool message);
	void prepareUserStream();
	bool parseResponse();
	void finish_headers_excpect_100();
};


struct HttpClientCfg {

	using PSocket = std::unique_ptr<ISocket>;

	std::string userAgent;
	int connectTimeout = 30000;
	CallbackT<PSocket(const NetAddr &, const std::string_view &host)> connect = nullptr;
	CallbackT<PSocket(const NetAddr &, const std::string_view &host)> sslConnect = nullptr;
	CallbackT<NetAddr(const std::string_view &)> resolve = nullptr;
};

class HttpClient {
public:

	using URL = std::string_view;
	using Data = std::string_view;
	using Method = std::string_view;
	using Callback = CallbackT<void(std::unique_ptr<HttpClientRequest> &&)>;


	HttpClient(HttpClientCfg &&cfg);


	///Open new request
	/**
	 * @param method method
	 * @param url url
	 * @return connected request. If result is null, then connection cannot be established
	 */
	std::unique_ptr<HttpClientRequest> open(const URL &method,
											const Method &url);

	///Open request asynchronously
	/**
	 * @param method method
	 * @param url url
	 * @param callback function called, when request is ready. The argument contains
	 * connected request. If the argument is null, then connection cannot be established
	 */
	void open(const URL &method, const Method &url, Callback &&callback);

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



protected:
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
	NetAddr resolve(const CrackedURL &cu);
	std::unique_ptr<ISocket> connect(const NetAddr &addr, const CrackedURL &cu);

	std::unique_ptr<HttpClientRequest> sendRequest(const Method &method, const URL &url, HeaderList headers);
	std::unique_ptr<HttpClientRequest> sendRequest(const Method &method, const URL &url, HeaderList headers, const Data &data);


};

}


#endif /* SRC_USERVER_HTTP_CLIENT_H_ */
