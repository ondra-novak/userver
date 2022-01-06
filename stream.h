/*
 * stream.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_STREAM_H_
#define SRC_MAIN_STREAM_H_

#include <memory>
#include <mutex>
#include "isocket.h"

namespace userver {

class Stream;

class AbstractStream {
public:
	virtual std::string_view read() = 0;
	virtual void readAsync(CallbackT<void(const std::string_view &data)> &&fn) = 0;
	virtual void putBack(const std::string_view &pb) = 0;
	virtual void write(const std::string_view &data) = 0;
	virtual bool writeNB(const std::string_view &data) = 0;
	virtual void closeOutput() = 0;
	virtual void closeInput() = 0;
	virtual void flush() = 0;
	virtual void flushAsync(CallbackT<void(bool)> &&fn) = 0;
	virtual bool timeouted() const = 0;
	virtual void clearTimeout() = 0;
	virtual ~AbstractStream() {};
	virtual std::size_t getOutputBufferSize() const = 0;

};

///Stream object controls an AbstractStream
/**
 * It is recommended to handle all operations from the Stream instead accessing AbstractStream
 *
 * Stream object tracks ownership of the AbstractStream. If Stream is destroyed,
 * the owned AbstracStream is automatically disposed. Acts similar to unique_ptr, however
 * there are some differences.
 *
 * Even not-owned Stream can be used for operations. you need to ensure, that
 * owner will not destroy its property during usage. In multithread envirnoment, you
 * need to use proper synchronization.
 */
class Stream{
public:

	///construct stream directly from AbstractStream pointer
	/**
	 * @param stream pointer to stream
	 * @param owner true when this object receives ownership
	 */
	Stream(AbstractStream *stream, bool owner = true):ptr(stream),owner(owner) {}
	///Construct from unique pointer
	/**
	 * @param ptr unique pointer. The argument looses the ownership, which is transfered to the Stream object
	 */
	Stream(std::unique_ptr<AbstractStream> &&ptr):ptr(ptr.release()),owner(this->ptr != nullptr) {}
	///Create empty stream object
	/** Don't use this object to control stream, it is not checked that stream is not initialized*/
	Stream():ptr(nullptr),owner(false) {}
	///Move ownership
	Stream(Stream &&other):ptr(other.ptr),owner(other.owner) {other.owner = false;}
	///Destroy the object
	~Stream() {
		if (owner) delete ptr;
	}

	///Assign and move ownership
	Stream &operator=(Stream &&x) {
		if (owner) delete ptr;
		ptr = x.ptr;
		owner = x.owner;
		x.owner = false;
		return *this;
	}

	///Make reference, ownership is retained
	Stream makeReference() {
		return Stream(ptr, false);
	}

	///Read helper - please look at description of read()
	/**
	 * Object is result of operation read(). It is useful as immediately object, not for storing.
	 * Outside it is acting as reference to the Stream (Stream &).
	 *
	 * You can use object to obtain data synchronously
	 *
	 * @code
	 * std::string_view data = stream.read();
	 * @endcode
	 *
	 * You can use object as source of characters, because object acts as function returning single byte
	 *
	 * @code
	 * parse(stream.read()); //parse from input - read charactes one by one
	 * @endcode
	 *
	 * You can use object to initiate asynchronous reading
	 *
	 * @code
	 * stream.read() >> [=](std::string_view data) {....};
	 * @endcode
	 *
	 * The ownership of the stream can be also transfered to the callback
	 *
	 * @code
	 * stream.read() >> [=](Stream &stream, std::string_view data) {....};
	 * @endcode
	 *
	 */
	class ReadHelper {
	public:
		ReadHelper(Stream &owner):owner(owner) {}
		ReadHelper(const ReadHelper &) = delete;
		//
		operator std::string_view() {return owner.ptr->read();}
		int operator()() {
			auto buff = owner.ptr->read();
			if (buff.empty()) return -1;
			int res = buff[0];
			owner.ptr->putBack(buff.substr(1));
			return res;
		}
		template<typename Fn> void operator>>(Fn &&fn) {
			readAsync(std::forward<Fn>(fn));
		}

	protected:
		Stream &owner;

		template<typename Fn> auto readAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<std::string_view>())) {
			owner.ptr->readAsync(std::forward<Fn>(fn));
		}
		template<typename Fn> auto readAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream &>(), std::declval<std::string_view>())) {
			auto ptr = owner.ptr;
			ptr->readAsync([fn = std::forward<Fn>(fn),stream = std::move(owner)](const std::string_view &data) mutable {
				fn(stream, data);
			});
		}
		template<typename Fn> auto readAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream>(), std::declval<std::string_view>())) {
			auto ptr = owner.ptr;
			ptr->readAsync([fn = std::forward<Fn>(fn),stream = std::move(owner)](const std::string_view &data) mutable {
				fn(stream, data);
			});
		}
	};

	ReadHelper read() {return ReadHelper(*this);}

	///Synchronous read
	/** Reads an undetermined count of bytes from the input stream
	 * Function blocks, if there no bytes to read. It always returns at least one
	 * byte. It returns empty buffer, if there some edge condition,  timeout,
	 * or connection close
	 *
	 * @return a string view to read data. Returns empty string, when connection
	 * has been closed or timeouted. To determine timeout, call timeouted(). Timeouted
	 * connection is considered dead and closed.
	 */
	std::string_view readSync() {return ptr->read();}


	///Asynchronously read whole stream to a string buffer
	/**
	 * Function receives callback function which is called with whole string buffer.
	 *
	 * @param buffer Buffer instance. The buffer must implement append() function
	 * @param maxSize specify size limit. Use 0 to unlimited read
	 * @param fn function called with result.
	 *
	 * @note reading stops when specified count of bytes is reached, or when end of stream is reached.
	 * In case of timeout, reading also stops and timeouted() is signaled
	 *
	 * @note function transfer ownership to the callback
	 */
	template<typename Buffer, typename Fn, typename = decltype(std::declval<Fn>()(std::declval<Stream &>(), std::declval<std::string>()))>
	void readToStringAsync(Buffer &&buffer, std::size_t maxSize, Fn &&fn);

	///Puts back a buffer
	/**
	 * Puts a buffer back to the stream, so it will be available by next read
	 * or readAsync. This allows to return unprocessed data back to the stream,
	 * so they can be process by next part of the program.
	 *
	 * The most common use of this function is to read a buffer of bytes, process
	 * some of the bytes from the beginning of the buffer, and put back the rest of
	 * the buffer.
	 *
	 * It is allowed to put back any arbitrary buffer which as no relation buffer
	 * returned by the function read(), however, you need to ensure, that content
	 * of the buffer remains valid, until it is read, otherwise, the any futher
	 * read() function will return a buffer which refers to an invalid memory location
	 *
	 * @param pb buffer put back.
	 *
	 * @note only one buffer can be put back befor it is read back.
	 * Multiple call of this function overwrites
	 * state of previous call.
	 */
	void putBack(const std::string_view &pb) {return ptr->putBack(pb);}
	///Synchronous write
	/**
	 *
	 * @param data data to write synchronously
	 *
	 * @note there is an output buffer which is not immediately flushed. If you need
	 * to ensure, that data has been actually send, you need to call flush() or flushAsync().
	 * There is no automatic flush on read, so always ensure, that you have a flush() before
	 * read, otherwise a deadlock can happen.
	 *
	 */
	void write(const std::string_view &data) {return ptr->write(data);}

	///Write without blocking (non blocking)
	/**
	 * This function is part of asynchronous write. Function only writes to output buffer
	 * without interact with underlying network
	 * @param data data to be written
	 * @retval true buffer is full enought, please call flush or flushAsync as soon
	 * as possible. There is no limit to output buffer, but additional writes can
	 * increase memory consuption
	 * @retval false buffer is not considered full, calling flush can be ineffective, if
	 * there are a lot of bytes to write.
	 */
	bool writeNB(const std::string_view &data) {return ptr->writeNB(data);}

	///Close output
	/** Calls flush() and immediatelly closes output, no futher writes are allowed
	 *
	 * By closing output, the other size receives end of stream. This doesn't close
	 * input, so you can still read a reply from the other side
	 */
	void closeOutput() {return ptr->closeOutput();}
	///Close input
	/**
	 * Closes the input, so any network read will return end of stream. Doesn't close
	 * output, so writing is still possible.
	 *
	 * @note if there are unprocessed input data, they can be still returned by
	 * the function read, until they are processed complete. The function only affect
	 * any blocking operation (or operation, which would block in case on asynchronous read)
	 */
	void closeInput() {return ptr->closeInput();}

	class FlushHelper {
	public:
		FlushHelper(Stream &owner):owner(owner),done(false) {}
		~FlushHelper() noexcept(false) {
			if (!done) owner.ptr->flush();
		}
		template<typename Fn> void operator >> (Fn &&fn) {
			done = true;
			flushAsync(std::forward<Fn>(fn));
		}
	protected:
		Stream &owner;
		bool done;

		template<typename Fn> auto flushAsync(Fn &&fn) -> decltype(std::declval<Fn>()()) {
			owner.ptr->flushAsync([fn = std::forward<Fn>(fn)](bool ok) {
				fn();
			});
		}
		template<typename Fn> auto flushAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<bool>())) {
			owner.ptr->flushAsync(std::forward<Fn>(fn));
		}
		template<typename Fn> auto flushAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream>())) {
			auto ptr = owner.ptr;
			ptr->flushAsync([owner = std::move(owner), fn = std::forward<Fn>(fn)](bool ok) mutable {
				fn(std::move(owner));
			});
		}
		template<typename Fn> auto flushAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream &>())) {
			auto ptr = owner.ptr;
			ptr->flushAsync([owner = std::move(owner), fn = std::forward<Fn>(fn)](bool ok) mutable {
				fn(owner);
			});
		}
		template<typename Fn> auto flushAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream>(), std::declval<bool>())) {
			auto ptr = owner.ptr;
			ptr->flushAsync([owner = std::move(owner), fn = std::forward<Fn>(fn)](bool ok) mutable {
				fn(std::move(owner), ok);
			});
		}
		template<typename Fn> auto flushAsync(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream &>(),std::declval<bool>())) {
			auto ptr = owner.ptr;
			ptr->flushAsync([owner = std::move(owner), fn = std::forward<Fn>(fn)](bool ok) mutable {
				fn(owner, ok);
			});
		}
	};

	///Flush stream
	/** Function itself only constructs FlushHelper, which can be used to either flush synchronously
	 * or asynchronously. If you append a callback function, the flush is performed asynchronously
	 * The callback function can also receive ownership of the stream.
	 *
	 * The callback function can have zero, one or two arguments
	 *
	 * just call when done - no information of success is carried (you can check for exception)
	 * @code
	 * stream.flush() >> [=]{....}
	 * @endcode
	 *
	 *
	 * recive succes info
	 * @code
	 * stream.flush() >>  [=](bool succ){....}
	 * @endcode
	 *
	 * carry the ownership and receive success info
	 * stream.flush() >> [=](Stream &stream, bool succ){....};
	 * @endcode
	 */
	FlushHelper flush() {return FlushHelper(*this);}

	///Flush buffer synchronously
	/** Flush any written content to the network, doesn't return, until whole buffer
	 * is send
	 */
	void flushSync() {return ptr->flush();}
	///Flush buffer asynchronously
	/**
	 * Flushes the buffer to the network. Function returns immediately, the completion
	 * of the operation is indicated by calling the callback function. The function
	 * receives a reference to the stream and status of the operation.
	 *
	 * The status of the operation can be true - success or false - failure. In the
	 * case of failure, the exception can be captured by function std::current_exception().
	 *
	 * @note Ownership! This object is loosing ownership of the stream, the ownership is
	 * transfered to the callback function, so you need to pick the ownership of the
	 * stream from the first argument of the callback function.
	 *
	 * @note there can be only one pending flush operation at time. It is also not allowed
	 *  to call write() or writeNB() until the flush operation is completed. By breaking
	 *  this rule, the state of the stream will be undefined.
	 *
	 * @param fn callback function
	 */
	/*template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<Stream &>(), std::declval<bool>()))>
	void flushAsync(Fn &&fn) {
		ptr->flushAsync([fn = std::move(fn), s = std::move(*this)](bool succ) mutable {
			fn(s, succ);
		});
	}*/
	///Flush buffer asynchronously
	/**
	 * Flushes the buffer to the network. Function returns immediately, the completion
	 * of the operation is indicated by calling the callback function. The function
	 * receives a status of the operation.
	 *
	 * The status of the operation can be true - success or false - failure. In the
	 * case of failure, the exception can be captured by function std::current_exception().
	 *
	 * @note Ownership! This object retains the ownership of the stream
	 *
	 * @note there can be only one pending flush operation at time. It is also not allowed
	 *  to call write() or writeNB() until the flush operation is completed. By breaking
	 *  this rule, the state of the stream will be undefined.
	 *
	 * @param fn callback function
	 */
	/*void flushAsync(CallbackT<void(bool)> &&fn) {
		ptr->flushAsync(std::move(fn));
	}*/
	///Determines wether the read or write operation timeouted
	bool timeouted() const {return ptr->timeouted();}

	void clearTimeout() {ptr->clearTimeout();}
	///retrieve a char from the stream (synchronously)
	int getChar() {
		auto c = readSync();
		if (c.empty()) return -1;
		else {
			putBack(c.substr(1));
			return c[0];
		}
	}
	///retrieve a line from the stream (synchronously)
	/**
	 * @param ln string which receives the line
	 * @param sep line separator (extracted but not stored)
	 * @retval true success
	 * @retval false unable to read line - stream read error
	 */
	bool getLine(std::string &ln, std::string_view sep = "\n") {
		ln.clear();
		auto b = readSync();
		int e = 0;
		while (!b.empty()) {
			ln.append(b);
			auto p = ln.find(sep, e);
			if (p != ln.npos) {
				auto rm = ln.length() - p - sep.length();
				putBack(b.substr(b.length() - rm));
				ln.resize(p);
				return true;
			}
			e = static_cast<int>(ln.length() - sep.length()+1);
			b = read();
		}
		return !ln.empty();
	}
	///Send a char - synchronously
	/**
	 * @param c character to send
	 *
	 * @note buffered
	 */
	void putChar(char c) {
		write(std::string_view(&c,1));
	}
	///Send a line - synchronously
	/**
	 * @param line line to send
	 * @param sep separator
	 *
	 * @note ensures, that whole line is sent in single batch, unless it is divided
	 * by network itself.
	 */
	void putLine(std::string_view &line, std::string_view sep = "\n") {
		writeNB(line);
		write(sep);
	}
	///Put character to output buffer, don't send yet (non blocking put)
	/**
	 * @param c character to put
	 * @retval true required flush
	 * @retval false no flush required yet
	 */
	bool putCharNB(char c) {
		return writeNB(std::string_view(&c,1));
	}
	///Put a line to to the output buffer;
	/**
	 * @param line line
	 * @param sep separator
	 * @retval true required flush
	 * @retval false no flush required yet
	 */
	bool putLineNB(std::string_view &line, std::string_view sep = "\n") {
		writeNB(line);
		return writeNB(sep);
	}
	bool valid() const {return ptr != nullptr;}
	bool owned() const {return owner;}
	template<typename T>
	bool putUnsignedNB(const T &x, unsigned int base = 10, int lpad = 1) {
		if (x == 0 && lpad < 0) return false;
		putUnsignedNB(x/base, base, lpad-1);
		unsigned int s = x%base;
		if (s < 10) return putCharNB(s+'0');
		else if (s < 36) return putCharNB(s+'A'-10);
		else return putCharNB(s+'a'-36);
	}
	template<typename T>
	void putUnsigned(const T &x, unsigned int base = 10, int lpad = 1) {
		if (putUnsignedNB(x,base,lpad)) flush();
	}
	template<typename T>
	bool putSignedNB(const T &x, unsigned int base = 10, int lpad = 1) {
		if (x < 0) {
			putCharNB('-');
			return putUnsignedNB(-x, base, lpad);
		} else {
			return putUnsignedNB(x, base, lpad);
		}
	}
	template<typename T>
	void putSigned(const T &x, unsigned int base = 10, int lpad = 1) {
		if (putSignedNB(x,base,lpad)) flush();
	}

	std::size_t getOutputBufferSize() const {return ptr->getOutputBufferSize();}
protected:
	AbstractStream *ptr;
	bool owner;
};

///Stream handles reads or writes from/to the socket
class SocketStream: public AbstractStream {
public:
	SocketStream(std::unique_ptr<ISocket> sock):sock(std::move(sock)) {}

	virtual std::string_view read() override;
	virtual void readAsync(CallbackT<void(const std::string_view &data)> &&fn) override;
	virtual void putBack(const std::string_view &pb) override;
	virtual void write(const std::string_view &data) override;
	virtual bool writeNB(const std::string_view &data) override;
	virtual void closeOutput() override;
	virtual void closeInput() override;
	virtual void flush() override;
	virtual void flushAsync(CallbackT<void(bool)> &&fn) override;
	virtual bool timeouted() const override;
	virtual std::size_t getOutputBufferSize() const override;
	virtual void clearTimeout() override;
	ISocket &getSocket() const;

	static std::size_t maxWrBufferSize;

protected:
	std::unique_ptr<ISocket> sock;
	std::string rdbuff;
	std::string wrbuff;
	std::string_view curbuff;
	bool eof = false;
	std::size_t wrbufflimit = 1000;

	void flush_lk();
	void flushAsync(const std::string_view &data, bool firstCall, CallbackT<void(bool)> &&fn);
};

///Stream handles reads or writes to other stream can limit how much bytes can be read or written
/**
 * Extra bytes will be thrown out. If no enough bytes are read or written, the destructor
 * processes remaining data.
 */
template<typename SS>
class LimitedStream: public AbstractStream {
public:
	LimitedStream(SS &&source, std::size_t maxRead, std::size_t maxWrite)
		:source(std::forward<SS>(source)), maxRead(maxRead), maxWrite(maxWrite) {}
	~LimitedStream();
	virtual std::string_view read() override;
	virtual void readAsync(CallbackT<void(const std::string_view &data)> &&fn) override;
	virtual void putBack(const std::string_view &pb) override;
	virtual void write(const std::string_view &data) override;
	virtual bool writeNB(const std::string_view &data) override;
	virtual void closeOutput() override;
	virtual void closeInput() override;
	virtual void flush() override;
	virtual void flushAsync(CallbackT<void(bool)> &&fn) override;
	virtual bool timeouted() const override;
	virtual void clearTimeout() override;
	virtual std::size_t getOutputBufferSize() const override;
protected:
	SS source;
	std::size_t maxRead;
	std::size_t maxWrite;
	std::string_view curBuff;

};

///Implement chunked stream
/**
 * Acts as virtual stream inside of other stream, which is encoded as chunked
 * The stream ends by terminating chunk. By closing output, terminating chunk
 * is written.
 *
 */
template<typename SS>
class ChunkedStream: public AbstractStream {
public:
	ChunkedStream(SS &&source,bool writing, bool reading)
		:source(std::forward<SS>(source)),eof(!reading),writing(writing),reading(reading),closed(!writing) {
		maxChunkSize = std::max<decltype(maxChunkSize)>(source.getOutputBufferSize(),20)-20;
	}
	~ChunkedStream();
	virtual std::string_view read() override;
	virtual void readAsync(CallbackT<void(const std::string_view &data)> &&fn) override;
	virtual void putBack(const std::string_view &pb) override;
	virtual void write(const std::string_view &data) override;
	virtual bool writeNB(const std::string_view &data) override;
	virtual void closeOutput() override;
	virtual void closeInput() override;
	virtual void flush() override;
	virtual void flushAsync(CallbackT<void(bool)> &&fn) override;
	virtual bool timeouted() const override;
	virtual void clearTimeout()  override;

	virtual std::size_t getOutputBufferSize() const override;
protected:
	SS source;
	std::size_t maxChunkSize;
	bool eof = false;
	bool writing = false;
	bool reading = false;
	bool closed  =false;
	std::size_t readRemain = 0;
	std::string curChunk;
	std::string ln;
	std::string_view curBuff;

	void putHex(std::size_t sz);
	bool flushNB();
};



template<typename SS>
std::string_view LimitedStream<SS>::read() {
	if (curBuff.empty()) {
		if (maxRead == 0) return std::string_view();
		auto rd = source.readSync();
		auto res = rd.substr(0, maxRead);
		source.putBack(rd.substr(res.length()));
		maxRead -= res.length();
		return res;
	} else {
		std::string_view res;
		std::swap(res, curBuff);
		return res;
	}
}

template<typename SS>
void LimitedStream<SS>::putBack(const std::string_view &pb) {
	curBuff = pb;
}


template<typename SS>
void LimitedStream<SS>::write(const std::string_view &data) {
	auto rmn = data.substr(0, maxWrite);
	source.write(rmn);
	maxWrite -= rmn.length();
}

template<typename SS>
inline void LimitedStream<SS>::closeOutput() {
	std::string_view c("\0");
	while (maxWrite) {
		source.write(c);
		--maxWrite;
	}
}

template<typename SS>
inline void LimitedStream<SS>::closeInput() {
	while (maxRead) read();
}

template<typename SS>
inline void LimitedStream<SS>::flush() {
	source.flush();
}

template<typename SS>
inline LimitedStream<SS>::~LimitedStream() {
	try {
		closeOutput();
		closeInput();
	} catch (...) {

	}
}

template<typename SS>
inline void LimitedStream<SS>::readAsync(CallbackT<void(const std::string_view &data)> &&fn) {
	if (curBuff.empty()) {
		if (maxRead == 0) {
			fn(std::string_view());
		} else {
			source.read() >> [fn = std::move(fn), this](const std::string_view &data) mutable {
				source.putBack(data);
				fn(read());
			};
		}
	} else {
		std::string_view b;
		std::swap(b, curBuff);
		fn(b);
	}
}

template<typename SS>
inline bool LimitedStream<SS>::writeNB(const std::string_view &data) {
	auto rmn = data.substr(0, maxWrite);
	bool r = source.writeNB(rmn);
	maxWrite -= rmn.length();
	return r;
}

template<typename SS>
inline void LimitedStream<SS>::flushAsync(CallbackT<void(bool)> &&fn) {
	source.flush()>> std::move(fn);
}

template<typename SS>
inline bool LimitedStream<SS>::timeouted() const {
	return source.timeouted();
}

template<typename SS>
inline ChunkedStream<SS>::~ChunkedStream() {
	try {
		closeOutput();
		closeInput();
	} catch (...) {

	}
}

template<typename SS>
inline std::string_view ChunkedStream<SS>::read() {
	if (curBuff.empty()) {
		if (!reading)
			return std::string_view();
		if (eof) return std::string_view();
		if (readRemain == 0) {
			ln.clear();
			if (!source.getLine(ln,"\r\n")) return std::string_view();
			while (ln.empty()) {
				if (!source.getLine(ln,"\r\n")) return std::string_view();
			}
			readRemain = std::strtoul(ln.c_str(), 0, 16);
			if (readRemain == 0) {
				eof = true;return std::string_view();
			}
		}
		auto rd = source.readSync();
		auto out = rd.substr(0,readRemain);
		source.putBack(rd.substr(out.length()));
		readRemain -= out.length();
		return out;
	} else {
		std::string_view res;
		std::swap(res, curBuff);
		return res;
	}
}

template<typename SS>
inline void ChunkedStream<SS>::putBack(const std::string_view &pb) {
	curBuff = pb;
}

template<typename SS>
inline void ChunkedStream<SS>::write(const std::string_view &data) {
	curChunk.append(data);
	if (curChunk.length() >= maxChunkSize) flush();

}

template<typename SS>
inline void ChunkedStream<SS>::closeOutput() {
	if (!closed) {
		flush();
		source.write("0\r\n\r\n");
		source.flush();
		closed = true;
	}
}

template<typename SS>
inline void ChunkedStream<SS>::closeInput() {
	if (reading) {
		while (!eof) read();
	}
}

template<typename SS>
inline void ChunkedStream<SS>::flush() {
	flushNB();
	if (closed) curChunk.clear();
	else {
		source.flush();
		maxChunkSize = std::max<decltype(maxChunkSize)>(source.getOutputBufferSize(),20)-20;
	}
}

template<typename SS>
inline std::size_t ChunkedStream<SS>::getOutputBufferSize() const {
	return maxChunkSize;
}

template<typename SS>
inline void ChunkedStream<SS>::clearTimeout()  {
	source.clearTimeout();
}

template<typename SS>
inline bool ChunkedStream<SS>::flushNB() {
	if (!curChunk.empty()) {
		putHex(curChunk.length());
		source.writeNB("\r\n");
		source.writeNB(curChunk);
		bool ret = source.writeNB("\r\n");
		curChunk.clear();
		return ret;
	} else {
		return false;
	}
}

template<typename SS>
inline bool ChunkedStream<SS>::timeouted() const {
	return source.timeouted();
}

template<typename SS>
inline void ChunkedStream<SS>::readAsync(CallbackT<void(const std::string_view &data)> &&fn) {
	if (curBuff.empty()) {
		source.read() >> [fn = std::move(fn),this](const std::string_view &data) {
			source.putBack(data);
			fn(read());
		};
	} else {
		std::string_view out;
		std::swap(out,curBuff);
		fn(out);
	}
}

template<typename SS>
inline bool ChunkedStream<SS>::writeNB(const std::string_view &data) {
	curChunk.append(data);
	return (curChunk.length() >= maxChunkSize);
}

template<typename SS>
inline void ChunkedStream<SS>::flushAsync(CallbackT<void(bool)> &&fn) {
	flushNB();
	if (closed) curChunk.clear();
	else {
		maxChunkSize = std::max<decltype(maxChunkSize)>(source.getOutputBufferSize(),20)-20;
		source.flush() >> std::move(fn);
	}
}

template<typename SS>
inline void ChunkedStream<SS>::putHex(std::size_t sz) {
	if (sz != 0) {
		putHex(sz>>4);
		char chars[] = "0123456789ABCDEF";
		source.putCharNB(chars[sz & 0xF]);
	}
}

template<typename SS>
inline void LimitedStream<SS>::clearTimeout()  {
	source.clearTimeout();
}

template<typename SS>
inline std::size_t LimitedStream<SS>::getOutputBufferSize() const {
	return source.getOutputBufferSize();
}


template<typename Buffer, typename Fn, typename >
inline void Stream::readToStringAsync(Buffer &&buffer, std::size_t maxSize, Fn &&fn) {
	readAsync([this, maxSize, buffer = std::forward<Buffer>(buffer), fn = std::forward<Fn>(fn)](Stream &s, std::string_view data)mutable {
		if (data.empty()) {
			fn(s,buffer);
		}
		else if (maxSize && data.length() >= maxSize) {
			putBack(data.substr(maxSize));
			buffer.append(data.substr(0,maxSize));
			fn(s,buffer);
		} else {
			buffer.append(data);
			maxSize -= data.length();
			s.readToStringAsync(std::forward<Buffer>(buffer), maxSize, std::forward<Fn>(fn));
		}
	});
}

}

#endif /* SRC_MAIN_STREAM_H_ */
