/*
 * mtwritestream.h
 *
 *  Created on: 2. 12. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_MTWRITESTREAM_H_
#define SRC_USERVER_MTWRITESTREAM_H_

#include <atomic>
#include <mutex>
#include "stream.h"

namespace userver {



///Stream which allows multiple threads to write, including asynchronous flush
/**
 * Useful to send events generated from multiple sources. Function send() is MT safe regardless on state of stream
 */
class MTWriteStream: public Stream {
public:

	using Stream::Stream;


	///Send data (line) atomically
	/**
	 * @param me shared pointer to this instance. Function requires shared_ptr to the stream
	 * @param ln data to send
	 * @retval true send or enqueued
	 * @retval false stream already closed
	 */
	static bool send(std::shared_ptr<MTWriteStream> me, const std::string_view &ln);

	///Close stream, stops sending anything
	void close();

	///Starts monitoring
	/**
	 * Monitors stream
	 *
	 * @param me shared pointer to this instance. Function requires shared_ptr to the stream
	 * @param cb callback. Function is called repeatedly for every piece data received (receiving
	 * is restarted when callback returns). In case of end of stream is received, callback is
	 * called with empty string for the last time.
	 *
	 */

	static void monitor(std::shared_ptr<MTWriteStream> me, CallbackT<void(const std::string_view &)> &&cb);

	~MTWriteStream();

protected:


	struct Line {

		Line(const std::string_view &str):next(0),str(reinterpret_cast<const char *>(this)+sizeof(Line),str.size()) {}

		Line *next;
		std::string_view str;

		void *operator new(std::size_t sz, const std::string_view &text);
		void operator delete(void *ptr, const std::string_view &text);
		void operator delete(void *ptr, std::size_t sz);
	};


	static Line unlocked;

	std::atomic<Line *> lines = &unlocked;
//	std::atomic<bool> ip = false;
	std::atomic<bool> closed = false;



	static void sendAsync(std::shared_ptr<MTWriteStream> me, const std::string_view &ln);

};



}




#endif /* SRC_USERVER_MTWRITESTREAM_H_ */
