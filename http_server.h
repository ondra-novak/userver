/*
 * http_server_request.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_HTTP_SERVER_H_
#define SRC_MAIN_HTTP_SERVER_H_
#include <vector>
#include <string_view>
#include <functional>
#include <map>
#include <thread>
#include <shared_mutex>

#include "async_provider.h"
#include "isocket.h"
#include "socket_server.h"
#include "stream.h"
#include "header_value.h"

namespace userver {

enum class ReqEvent {
	init,
	header_sent,
	done
};

class HttpServerRequest {
public:

	class ILogger {
	public:
		virtual void log(ReqEvent event,const HttpServerRequest &req) noexcept = 0;
		virtual void handler_log(const HttpServerRequest &req, const std::string_view &msg) noexcept = 0;
		virtual ~ILogger() {}
	};

	using KeepAliveCallback = CallbackT<void(Stream &, HttpServerRequest &)>;

	///Create request and give it a stream. Also register callback for keepalive
	/**
	 * @param stream stream
	 * @param callback keep alive callback allows to continue in request - note, callback
	 * is not called when keep alive is not enabled
	 */
	HttpServerRequest();
	~HttpServerRequest();

	void initAsync(Stream &&stream, CallbackT<void(bool)> &&initDone);

	bool init(Stream &&stream);

	void reuse_buffers(HttpServerRequest &from);

	void setKeepAliveCallback(KeepAliveCallback &&kc);
	void setLogger(ILogger *log) {logger = log;}

	template<typename ... Args>
	void log(const Args & ... data);

	///Returns true, if successfully parsed, or false if parse error
	bool isValid();

	bool isGET() const;
	bool isPOST() const;
	bool isPUT() const;
	bool isDELETE() const;
	bool isOPTIONS() const;
	bool isHEAD() const;
	bool allowMethods(std::initializer_list<std::string_view> methods);


	HeaderValue get(const std::string_view &item) const;
	std::string_view getMethod() const;
	std::string_view getURI()  const;
	std::string_view getHTTPVer()  const;
	std::string_view getHost()  const;


	void set(const std::string_view &key, const std::string_view &value);
	void set(const std::string_view &key, std::size_t number);

	void setStatus(int code);
	void setStatus(int code, const std::string_view &message);
	void setContentType(const std::string_view &contentType);

	///Send response
	/** Sends response to output
	 *
	 * @param body content to send
	 */
	void send(const std::string_view &body);

	///Send response header and initialize stream
	Stream send();

	///Send file from filesystem
	/**
	 * @param reqptr request pointer (wrapped to unique ptr)
	 * @param path path to file to send
	 * @retval true file transfer started
	 * @retval false file not found
	 *
	 * @note etag will be set unless header has already etag. content-type will be set
	 * unless content-type is already set. When If-None-Match is set in request to matching
	 * etag, then 304 status is send
	 *
	 * @note File is transfered asynchronously. ownership of the pointer is held during
	 * the file transfer. In case of true return you should no longer access the
	 * request object.
	 *
	 */
	static bool sendFile(std::unique_ptr<HttpServerRequest> &&reqptr, const std::string_view &path);

	///Sends error page
	/**
	 * @param code error page code
	 *
	 */
	void sendErrorPage(int code);
	///Sends error page
	/**
	 *
	 * @param code error page code
	 * @param description error description
	 */
	void sendErrorPage(int code, const std::string_view &description);


	struct CookieDef {
		int max_age = 0;
		bool secure = false;
		bool http_only = false;
		std::string_view domain;
		std::string_view path;
	};

	void setCookie(const std::string_view &name, const std::string_view &value, const CookieDef &cookieDef = {0,false,false,std::string_view(),std::string_view()});

	static std::size_t maxChunkSize;

	///Get body
	/** The body can be read once only */
	Stream getBody();

	///Get original stream (various protocols need to access stream directly, such a websockets)
	Stream &getStream();

	bool isBodyAvailable() const {return hasBody;}
	///Returns true, when headers has been already sent
	bool headersSent() const {return response_sent;}

	std::size_t getIdent() const;

	const std::chrono::system_clock::time_point &getRecvTime() const;
	std::intptr_t getResponseSize() const;
	unsigned int getStatus() const;

	///Asynchronously read a large body
	/**
	 * @param maxSize maximum allowed size of the body. If the body is larger, apropriate error status is generated and callback is not called
	 * @param fn callback function called when reading is done. It receives buffer containing the whole body
	 */
	template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<std::string_view>()))>
	void readBodyAsync(std::size_t maxSize, Fn &&fn);


	///Asynchronously read a large body, transfer ownership of the request to the callback function
	/**
	 *
	 * @param reqptr request pointer - note this function is static and expects that
	 * it object is accessed through an unique pointer. The ownership of the pointer is
	 * transfered to the callback function
	 *
	 * @param maxSize maximum alloved size of the body
	 * @param fn function which receives pointer to request and the buffer containing the body
	 */
	template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<std::unique_ptr<HttpServerRequest> &>(), std::declval<std::string_view>()))>
	static void readBodyAsync(std::unique_ptr<HttpServerRequest> &&reqptr,std::size_t maxSize, Fn &&fn);

	///Redirects to directory
	/** you need to call this, if you receive empty vpath and you need to refer objects in subdirectory
	 * This generates redirect to the directory
	 * @retval true generated
	 * @retval false no action is needed
	 */
	bool directoryRedir();

	static std::string_view contentTypeFromExtension(std::string_view extension);

	void setContentTypeFromExt(std::string_view ext);

protected:

	bool parse();
	bool processHeaders();
	static void sendFileAsync(std::unique_ptr<HttpServerRequest> &reqptr, std::unique_ptr<std::istream>&in, Stream &out);


	Stream stream;
	KeepAliveCallback klcb;
	ILogger *logger = nullptr;
	bool enableKeepAlive = false;
	bool valid = false;
	bool hasBody = true;
	bool hasExpect = false;
	std::size_t ident = 0;
	std::chrono::system_clock::time_point initTime;

	static std::atomic<std::size_t> identCounter;

	std::vector<char> firstLine;
	std::vector<char> inHeaderData;
	std::vector<char> sendHeader;
	std::vector<char> logBuffer;
	std::vector<std::pair<std::string_view, std::string_view> > inHeader;
	std::string statusMessage;
	std::string_view method, uri, httpver, host;

	bool parseFirstLine(std::string_view &v);
	bool parseHeaders(std::string_view &v);

	//---- response fields

	bool response_sent = false;
	unsigned int statusCode = 200;
	bool has_content_type = false;
	bool has_date = false;
	bool has_transfer_encoding = false;
	bool has_transfer_encoding_chunked = false;
	bool has_content_length = false;
	bool has_connection = false;
	bool has_last_modified = false;
	bool has_server = false;
	std::size_t send_content_length = 0;

	bool readHeader();
	bool readHeader(std::string_view &buff, int &m);
	template<typename Fn>
	void readHeaderAsync(int m, Fn &&done);

	template<typename T, typename ... Args>
	void log2(const T &a, const Args & ... args);
	void log2();

	template<typename Fn>
	void readBodyAsync2(Stream &s, std::vector<char> &buffer, std::size_t maxSize, bool overflow, Fn &&fn);

};

using PHttpServerRequest = std::unique_ptr<HttpServerRequest>;

class SocketServer;

class HttpServerMapper {
public:

	HttpServerMapper();
	using Handler = CallbackT<bool(PHttpServerRequest &, const std::string_view &)>;

	void addPath(const std::string_view &path, Handler &&handler);
//	void serve(Stream &&stream);

	bool execHandler(PHttpServerRequest &req, const std::string_view &vpath);
	///Automatically detect prefix on given host
	/**
	 * @param req request as r-value, it is moved to the handler, when true is returned
	 * @retval true handler executed, request handled by the handler
	 * @retval false handler not executed, request is still owned by the caller
	 *
	 * @note function detects and remembers prefix for given host. This allows to perform 1:1
	 * mapping on proxy server without path transformation. Detection is done by removing
	 * prefixes until the handler process the request. Then the mapping is remembered
	 *
	 */
	bool execHandlerByHost(PHttpServerRequest &req);

protected:


	struct PathMapping {

		std::map<std::string, std::string, std::less<> > hostMapping;
		std::map<std::string, Handler, std::less<> > pathMapping;
		std::shared_timed_mutex shrmux;
	};
	using PPathMapping = std::shared_ptr<PathMapping>;
	PPathMapping mapping;

};

class HttpServer: public HttpServerMapper {
public:

	///Start the server
	/**
	 * @param listenSockets list of addresses to listen
	 * @param threads count of threads processing the requests. When 0 is passed, then no threads are created,
	 * but you can add threads manually by addThread()
	 * @param dispatchers count of dispatchers, which monitors pending connections. Must be less or equal to threads
	 */
	void start(NetAddrList listenSockets, unsigned int threads, unsigned int dispatchers = 1);
	///Stop the server
	/**
	 * Function joins all threads, will block until the operation completes
	 * @note if there is work in thread, it will wait until work is finish. All pending asynchronous
	 * operations are canceled. If there are associated data, they should be stored in
	 * callbacks' clousures, because they are deleted as expected, so these data can be disposed in
	 * this time.
	 */
	void stop();

	///Stops server on signal handler
	/**
	 * Installs signal handler on SIGINT and SIGTERM which, when the signal is triggered,
	 * stop the server and terminates all threads. This also allows to main thread
	 * exit from the function addThread()
	 *
	 * @note only one server can have this feature. You should use this on server which
	 * where the main thread is involved
	 *
	 * @note it is good idea to call stop() eventually even if this function is involved
	 */
	void stopOnSignal();

	///Add thread to pool
	/** Thread which called this function becomes the pool thread. Note that thread is not joined
	 * during stop
	 */
	void addThread();

	///Receive asynchronous provider
	AsyncProvider getAsyncProvider();
	///automatically calls stop()
	virtual ~HttpServer();
	///Overridable
	/**
	 * Logs every request
	 * @param event which event is logged
	 * @param req request itself
	 *
	 * @note this function can be called from various threads and in parallel. If you log some
	 * events, you need to use proper synchronization while accessing shared log file. Use synchronization
	 * when you really needit, because this destroys purpose of paralelisation. It is better to use
	 * per-thread buffers and occasionaly flush them with synchronization
	 */
	virtual void log(ReqEvent event, const HttpServerRequest &req);

	///Overridable
	/**
	 * Receives log message generated by handler. Useful to collect logs from handlers, which are
	 * executed in various threads.
	 *
	 * @param msg message logged.
	 */
	virtual void log(const HttpServerRequest &req, const std::string_view &msg);

	///Overridable
	/**
	 * Called when unhandled exception is detected;
	 *
	 * @note function has no arguments. Function is called inside catch handler, so you can call try-throw-catch
	 *
	 * @note Default implementations sends what() message to the std.error.
	 */
	virtual void unhandled();
	///Allows to catch special connections before they are processed as HTTP request
	/**
	 * @param s stream containing new connection
	 * @retval true connection handled, no futher processing is done.
	 * @retval false connection is not handled, continue processing as http
	 *
	 * If you need process connection asynchronously, you need to pick ownership of the stream
	 * and return true. Once you know, that connection is a HTTP request, you can call process()
	 * to continue processing as HTTP
	 */
	virtual bool onConnect(Stream &) {return false;}

	///Process arbitrary stream as HTTP request
	/**
	 * @param s stream to process. Note that fuction immediatelly returns because stream is
	 * processed asynchronously. You are loosing ownership of the stream
	 */
	void process(Stream &&s);

	///Called to create request
	/** You can override this function if you need customize request object */
	virtual PHttpServerRequest createRequest();

	///Called during keep alive, to reuse requests buffers
	/**
	 * If you override this method, then you need to call parent implementation
	 * @param old_req old request
	 * @param new_req new request
	 */
	virtual void reuse_buffers(HttpServerRequest &old_req, HttpServerRequest &new_req);

protected:
	std::vector<std::thread> threads;
	AsyncProvider asyncProvider;
	std::optional<SocketServer> socketServer;
	std::unique_ptr<HttpServerRequest::ILogger> logger;
	std::mutex lock;

	void listen();
	void beginRequest(Stream &&s, PHttpServerRequest &&req);

	void buildLogMsg(std::ostream &stream, const HttpServerRequest &req);
	void buildLogMsg(std::ostream &stream, const std::string_view &msg);


};

void formatToLog(std::vector<char> &log, const std::string_view &txt);
void formatToLog(std::vector<char> &log, const std::intptr_t &v);
void formatToLog(std::vector<char> &log, const std::uintptr_t &v);
void formatToLog(std::vector<char> &log, const double &v);


template<typename ... Args>
inline void HttpServerRequest::log(const Args &... data) {
	if (!logger) return;
	log2(data...);
}

template<typename T, typename ... Args>
inline void HttpServerRequest::log2(const T &a, const Args &... args) {
	formatToLog(logBuffer, a);
	log2(args...);
}

template<typename Fn, typename>
void HttpServerRequest::readBodyAsync(std::size_t maxSize, Fn &&fn) {
	std::vector<char> buffer;
	HeaderValue hv = get("Content-Length");
	if (hv.defined){
		auto sz = hv.getUInt();
		if (sz >maxSize) {
			sendErrorPage(413);
			return ;
		} else {
			buffer.reserve(sz);
		}
	}
	Stream s = getBody();
	readBodyAsync2(s, buffer, maxSize, false, std::move(fn));

}

template<typename Fn, typename>
void HttpServerRequest::readBodyAsync(std::unique_ptr<HttpServerRequest> &&req, std::size_t maxSize, Fn &&fn) {
	auto p = req.get();
	p->readBodyAsync(maxSize, [fn = std::move(fn), req = std::move(req)](const std::string_view &data) mutable {
		return fn(req, data);
	});
}


template<typename Fn>
void HttpServerRequest::readBodyAsync2(Stream &s, std::vector<char> &buffer, std::size_t maxSize, bool overflow, Fn &&fn) {
	s.readAsync([buffer = std::move(buffer), maxSize, fn = std::move(fn), overflow, this](Stream &s, const std::string_view &data) mutable {
		if (data.size() + buffer.size() > maxSize) {
			overflow = true;
		} else if (data.empty()) {
			if (overflow) {
				sendErrorPage(413);
			} else {
				try {
					fn(std::string_view(buffer.data(), buffer.size()));
				} catch (std::exception &e) {
					log("Exception:", e.what());
					sendErrorPage(500);
				} catch (...) {
					sendErrorPage(500);
				}
			}
			return;
		} else {
			std::copy(data.begin(), data.end(), std::back_inserter(buffer));
		}
		readBodyAsync2(s, buffer, maxSize, overflow, std::move(fn));
	});
}



}

#endif /* SRC_MAIN_HTTP_SERVER_H_ */
