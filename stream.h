/*
 * stream2.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_STREAM_H_
#define SRC_LIBS_USERVER_STREAM_H_


#include "platform_def.h"
#include "isocket.h"

#include <vector>
#include <memory>
#include <iostream>

namespace userver {


class ReadData: public std::string_view {
public:
    enum _Timeout {timeout};
    
    using std::string_view::string_view;
    ReadData(const std::string_view &x):std::string_view(x) {}
    ReadData(_Timeout): _timeouted(true) {}
    
    bool is_timeouted() const {return _timeouted;}
    
protected:
    bool _timeouted = false;
    
};

class AbstractStreamInstance {
public:
    virtual ~AbstractStreamInstance() = default;

    
    
    
    //reading interface

     /// Read stream synchronously.
     /** @return buffer containing read data. The buffer is allocated internally and it
     * is valid until next call. Function always returns at least 1 byte, otherwise it
     * blocks. However, in case of timeout or error, the empty buffer is returned. To
     * determine whether it was because timeout, check function timeouted(). In case of
     * the timeout, you can clear timeout and repeat the request
     * @exception any Function can throw exception in case of network error
     *
     * @note function is not MT Safe for reading side, so only one thread can reat at
     * time - applies both sync or async
     */
    virtual ReadData read_sync() = 0;

    /// Read synchronously without blocking
    /**
     * @return read data, or empty string. The empty string doesn't say anything about
     * the state of the stream. It only means, that no data can be read without blocking.
     *
     * This should be fastest possible operation. In most of cases, it works with
     * put_back() data. if there are no such data, it always returns the empty string.
     *
     * If the stream doesn't support this operation, it could always return the empty string
     */
    virtual std::string_view read_sync_nb() = 0;

    ///Read stream asynchronously
    /**
     * @param callback a callback function which is called when at least one byte is ready to
     * read. If the empty buffer is passed, then error or timeout happened. To
     * determine whether it was because timeout, check function timeouted(). In case of
     * the timeout, you can clear timeout and repeat the request
     *
     * @note function is not MT Safe for reading side, so only one thread can reat at
     * time - applies both sync or async
     *
     * @note to determine, which error caused loosing the connection, you can check
     * for current exception. The exception pointer is set when there was an error. If
     * the exception pointer is not set, there were timeout out or EOF
     *
     */
    virtual void read_async(Callback<void(const ReadData &)> &&callback) = 0;

    ///Puts back unprocessed input
    /**
     * You cannot control, how many bytes can be received, however, you can put back
     * unprocessed data. These data can be later read by read_sync() or read_async().
     *
     * @param buffer buffer contains unprocessed input. The buffer should be substring of
     * a buffer passed or returned by the read function. However it is not error to
     * pass an other buffer, you only need to ensure, that the content of the buffer is
     * valid until it is retrieved and processed.
     *
     * @note function is not MT Safe for reading side, so only one thread can reat at
     * time - applies both sync or async
     */
    virtual void put_back(const std::string_view &buffer) = 0;

    ///Closes the input
    /**
     * Function closes the input part if the stream disallowing any further reading.
     * The function can be called from other thread. Any pending reading is interrupted
     * and reported as an error (connection close)
     */
    virtual void close_input() = 0;


    ///Shorten reading timeout to zero
    /**
     * Function shortens read timeout to zero even if the reading is currently
     * pending. This causes that callback is immediately called with argument set to false.
     *
     * Note the function also sets read timeout to zero. To restart reading, you
     * need to set timeout a meaningful value
     *
     * @note function is MT Safe for reading side, but it is not MT Safe for setting
     * the timeout.
     */
    virtual void timeout_async_read() = 0;

    //block write interface
    ///Writes to the output synchronously
    /**
     * @param buffer buffer to write. Function blocks until write is done or timeout
     * whichever comes first
     *
     * @retval true successfully written
     * @retval false error or timeout. You cannot distinguish between error or timeout, both
     * states are considered as lost connection.
     * @exception any Function can throw exception in case of network error
     *
     *
     * @note You should avoid to combine write_sync and write_async. It is okay only when
     * async. queue is empty. Once there is pending async. writing, invoking a sync writing
     * in this state causes undefined behavior
     */
    virtual bool write_sync(const std::string_view &buffer) = 0;


    ///Writes asynchronously
    virtual bool write_async(const std::string_view &buffer, Callback<void(bool)> &&callback) = 0;
    ///Closes the output
    /**
     * Operation closes output, so other side receives EOF. No futher writes
     * are allowed.
     *
     * If there are pending writes, the call is enqueued and the output is closed
     * once all requests are processed
     */
    virtual void close_output() = 0;

    ///Shorten writing timeout to zero
    /**
     * Function shortens write timeout to zero even if the writing is currently
     * pending. This causes that callback is called with argument set to false.
     *
     * Note the function also sets read timeout to zero. To restart reading, you
     * need to set timeout a meaningful value
     *
     * @note function is MT Safe for writing side, but it is not MT Safe for setting
     * the timeout.
     *
     */
    virtual void timeout_async_write() = 0;


    virtual void clear_timeout() = 0;


    ///Changes current read timeout
    /**
     * @param tm_in_ms new timeout duration in milliseconds. Set -1 to infinite
     */
    virtual void set_read_timeout(int tm_in_ms) = 0;
    ///Changes current write timeout
    /**
     * @param tm_in_ms new timeout duration in milliseconds. Set -1 to infinite
     */
    virtual void set_write_timeout(int tm_in_ms) = 0;

    virtual void set_rw_timeout(int tm_in_ms) = 0;


    virtual int get_read_timeout() const = 0;

    virtual int get_write_timeout() const = 0;

};

class IBufferedStreamInfo {
public:
    virtual ~IBufferedStreamInfo() = default;

    virtual std::size_t get_buffered_amount() const = 0;
};


template<typename PtrType>
class Stream_t: public PtrType {
public:

    using PtrType::PtrType;

    Stream_t(const PtrType &other):PtrType(other) {}
    Stream_t(PtrType &&other):PtrType(std::move(other)) {}

    ///Read synchronously
    /**
     * @return all data fetch from the stream. The data are stored in stream's buffer,
     * and are valid until next read operation is called. If there are no data to be
     * read, function blocks and unblocks when at least one byte arrives, or timeout
     * ellapses. Note that synchronous reading without the timeout cannot be interrupted.
     * If you need this feature, use asynchronous reading. If the returned buffer is empty,
     * reading probably timeouted or reached end of stream (other side closed the connection).
     * You can distinguish between these two states by testing timeouted() function.
     * If timeouted() returns false, then empty buffer means end of stream.
     *
     * @note MT Safety - only one thread can read at time
     */

    std::string_view read_sync() {return (*this)->read_sync();}
    ///Read synchronously non blocking
    /**
     * @return immediately available data to be read and processed. Note that function
     * can return empty buffer, if there are not immediately available data. This doesn't
     * mean, that end of stream has been reached. You need to perform read_sync() to get
     * the valid stream state
     *
     * @note MT Safety - only one thread can read at time
     *
     * @note There is connection between read_sync_nb() and put_back(). This function
     * returns whole put_back buffer and clears it.
     */
    std::string_view read_sync_nb() {return (*this)->read_sync_nv();}

    ///Reads asynchronously
    /**
     * @param callback reads asynchronously and calls callback function with read data.
     * If the data are not immediately available, reading continues at the background.
     * The function must accept std::string_view. The passed buffer can be empty, if
     * operation timeouted or end of stream has been reached (other side closed the connection)
     * You can distinguish between these two states by testing timeouted() function.
     *
     * @note MT Safety - only one thread can read at time
     *
     */
    void read_async(Callback<void(std::string_view)> &&callback) {(*this)->read_async(std::move(callback));}

    ///Puts back unprocessed data
    /**
     * @param buffer contains view to unprocessed data. Note the returned buffer should
     * be sub-view of buffer returned by read_sync (or passed to the callback of the function
     * read_async). It is not mistake to pass different buffer, but you need to ensure,
     * that buffer stays valid, until it is retrieved again.
     *
     * @note MT Safety - only one thread can read at time
     *
     */
    void put_back(const std::string_view &buffer) {(*this)->put_back(buffer);}

    ///Closes the input
    /**
     * When input is closed, no more reading is possible, it is reported as end of stream
     *
     * @note MT Safety - this function can be called from the different thread
     *
     */
    void close_input() {(*this)->close_input();}

    ///Shorts timeout of reading to zero and iterrupts asynchronous reading
    /** This operation can interrupt asynchronous reading. It also sets read timeout
     * to zero, so all further reading immediately timeouts. Reading callback
     * function receives an empty buffer and timeouted() reports true
     *
     * @note MT Safety - this function can be called from the different thread. Note that
     * only one thread can call this function at time
     */
    void timeout_async_read() {(*this)->timeout_async_read();}


    ///Writes synchronously
    /**
     * @param buffer content of buffer to write
     * @retval true write successful
     * @retval false write failure - connection reset, or other unrecorverable error so
     * further writing is not possible
     *
     * @note MT Safety - only one thread can write at time
     */
    bool write_sync(const std::string_view &buffer) {return (*this)->write_sync(buffer);}
    ///Write asynchronously
    /**
     * @param buffer content of buffer to write. The content of the buffer must remain
     * valid until the write is finished. You can achieve this by allocating a buffer
     * before reading and release the buffer in the callback function.
     *
     * @param callback function called when writing is finished (or failed).
     * @retval true processed
     * @retval false writing rejected, because stream is disconnected.
     *                Note that callback is still called (with false), so it is safe to
     *                ignore result.
     *
     * @note MT Safety - only one thread can write at time. Don't start new writing
     * before the previous one finished (by calling the callback).
     */
    bool write_async(const std::string_view &buffer, Callback<void(bool)> &&callback) {
        return (*this)->write_async(buffer, std::move(callback));
    }
    ///Shorts timeout of writing to zero and interrupts asynchronous writing
    /** This operation can interrupt asynchronous writing. It also sets write timeout
     * to zero, so all further writing immediately timeouts. Writing callback
     * function receives false as result
     *
     * @note MT Safety - this function can be called from the different thread. Note that
     * only one thread can call this function at time
     */
    void timeout_async_write() {(*this)->timeout_async_write();}

    ///Closes the output
    /**
     * When output is closed, other side receives end of stream signal. However the reading
     * is still possible.
     *
     * @note MT Safety - this function can be called only if there is no pending writing
     */
    void close_output() {(*this)->close_output();}

    ///Clears read timeout
    void clear_timeout() {(*this)->clear_timeout();}

    ///Sets read timeout
    /**
     * @param tm_in_ms timeout in miliseconds
     */
    void set_read_timeout(int tm_in_ms) {(*this)->set_read_timeout(tm_in_ms);}
    ///Sets read timeout
    /**
     * @param tm_in_ms timeout in miliseconds
     */
    void set_write_timeout(int tm_in_ms) {(*this)->set_write_timeout(tm_in_ms);}
    ///Sets both read and write timeout
    /**
     * @param tm_in_ms timeout in miliseconds
     */
    void set_io_timeout(int tm_in_ms) {(*this)->set_rw_timeout(tm_in_ms);}

    ///Gets read timeout
    int get_read_timeout() const {return (*this)->get_read_timeout();}
    ///Gets write timeout
    int get_write_timeout() const {return (*this)->get_write_timeout();}

    static std::string_view extract_stream(std::istream &source, std::vector<char> &data) {
        source.seekg(0, std::ios::end);
        auto sz = source.tellg();
        source.seekg(0, std::ios::beg);
        data.resize(sz);
        source.read(data.data(),sz);
        return std::string_view(data.data(), data.size());
    }

    ///Write async whole input stream
    /**
     * @param source source stream. It must be seekable, Function is intended to be used with
     * std::stringstream
     *
     * @param callback callback function called when data are sent. Can be nullptr
     */
    void write_async(std::istream &source, Callback<void(bool)> &&callback) {
        std::vector<char> data;
        write_async(extract_stream(source, data),[callback = std::move(callback), data = std::move(data)](bool b) mutable {
            if (callback != nullptr) callback(b);
        });
    }

    ///Write sync whole input stream
    /**
     * @param source source stream. It must be seekable, Function is intended to be used with
     * std::stringstream
     */
    bool write_sync(std::istream &source) {
        std::vector<char> data;
        return write_sync(extract_stream(source, data));
    }


    ///Reads line synchronously, lines are separed by a separator
    /**
     * @param buffer contains reference to an empty string which will be filled with line
     * @param separator contains separator, reading will stop by extracting the separator. the
     * separator is not stored
     * @retval true successfully read
     * @retval false failure during read. The buffer contains data already read. To restart
     * reading, you can put_back the buffer and read data again
     */
    bool get_line(std::string &buffer, const std::string_view &separator) {
        auto data = read_sync();
        buffer.clear();
        while (!data.empty()) {
            buffer.append(data);
            auto n = find_separator(buffer, separator, buffer.size() - data.size());
            if (n != buffer.npos) {
                put_back(data.substr(data.size()-(buffer.size() - n - separator.size())));
                buffer.resize(n);
                return true;
            }
        }
        return false;

    }

    ///Reads line asynchronously, lines a separated by a separator
    /**
     * @param separator separator
     * @param cb callback, which receives boolean value which can contain true for success and
     * false for failure = eof or timeout, and string contains the line. The reference to a string
     * is valid until the callback is finished. The reference is not const, so you can freely move
     * the string out of the callback instance to somewhere else
     */
    void get_line_async(std::string_view separator, Callback<void(bool,std::string &)> &&cb) {
        read_async([=, cb = std::move(cb)](std::string_view data) mutable {
            get_line_async_cont(data, std::string(separator), std::string(), std::move(cb));
        });
    }





    class ReadHelper {
    public:
        operator std::string_view() {
            auto out = _owner->read_sync();
            _owner = nullptr;
            return out;
        }
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<std::string_view>())) {
            _owner->read_async(std::forward<Fn>(fn));
            _owner = nullptr;
        }

    protected:
        typename PtrType::element_type * _owner;
        ReadHelper(const Stream_t &owner):_owner(owner.get()) {}
        ReadHelper(const ReadHelper &other) = delete;
        ReadHelper &operator=(const ReadHelper &other) = delete;
        friend class Stream_t;
    };

    class WriteHelper {
    public:
        operator bool() {
            bool x = _owner->write_sync(_buffer);
            _owner = nullptr;
            return x;
        }
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<bool>())) {
            _owner->write_async(_buffer, std::forward<Fn>(fn));
            _owner = nullptr;
        }
        ~WriteHelper() {
            if (_owner) _owner->write_sync(_buffer);
        }
    protected:
        typename PtrType::element_type * _owner;
        std::string_view _buffer;
        WriteHelper(const Stream_t &owner, const std::string_view &buffer)
            :_owner(owner.get())
            ,_buffer(buffer) {}
        WriteHelper(const WriteHelper &other) = delete;
        WriteHelper &operator=(const WriteHelper &other) = delete;
        friend class Stream_t;
    };


    class ReadBlockHelper{
    public:

        operator bool() {
            while (_size) {
                auto d = _owner->read_sync();
                if (d.empty()) return false;
                auto s = d.substr(0,_size);
                auto c = d.substr(s.length());
                _owner->put_back(c);
                _buffer.insert(_buffer.end(), s.begin(), s.end());
                _size -= s.size();
            }
            return true;
        }

        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<bool>(),std::declval<std::vector<char> &>())) {
            _owner->read_block_async(_size, std::move(_buffer), std::forward<Fn>(fn));
            _owner = nullptr;
        }
        ~ReadBlockHelper() {
            _owner && (*this);
        }


    protected:
        typename PtrType::element_type * _owner;
        std::vector<char> &_buffer;
        std::size_t _size;
        ReadBlockHelper(const Stream_t &owner, std::vector<char> &buffer, std::size_t size)
            :_owner(owner.get()),_buffer(std::move(buffer)),_size(size) {}
        ReadBlockHelper(const ReadBlockHelper &other) = delete;
        ReadBlockHelper &operator=(const ReadBlockHelper &other) = delete;
        friend class Stream_t;

    };

    ///Generic read, you can choose between sync and async version by use
    /**
     * @return object can be converted to std::string_view which executes synchronous read.
     * You can also use operator >> to forward reading to completion function which executes
     * asynchronous read
     *
     * @code
     * std::string_view data = stream.read();
     *
     * stream.read() >> [=](std::string_view data) {
     *    ...
     * };
     * @endcode
     */
    ReadHelper read() {return ReadHelper(*this);}
    ///Generic write, you can choose between sync and async version by use
    /**
     * @param buffer buffer to write
     * @param copy_content se true to copy content, false to skip copying - little faster. This
     * doesn't effect on synchronous writing
     *
     * @return object can be converted to bool which executes synchronous write.
     * You can also use operator >> to forward reading to completion function which executes
     * asynchronous read. If none used, synchronous write is finally executed.
     *
     * @code
     *
     * stream.write(data);
     *
     * bool ok = stream.write(data);
     *
     * stream.write(data) >> [=](bool ok) {
     *    ...
     * };
     *
     * stream.write(data) >> nullptr;
     * @endcode
     */
    WriteHelper write(const std::string_view &buffer) {
        return WriteHelper(*this, buffer);
    }


    ///Reads block of specified size
    /**
     * Both synchronous and asynchronous are supported. For synchronous reading, the read
     * content is stored in buffer which is vector of chars. We don't use string to
     * express more binary origin of this function. For asynchronous reading, the content
     * of the vector is passed to the callback. It is moved using std::move to the callback,
     * and if you need to put the data back to the original variable, you need to just move it
     * back.
     *
     * @param buffer vector of characters. It should be empty, but it is not mandatory condition.
     * If the buffer is not empty, the new data are appended.
     * @param size expected size of data. Reading will stop after amount of data was read, or
     * when the EOF is reached. Both situations are reported in return value
     * @return Returns ReadBlockHelper. This object can be checked for bool value to perform
     * synchronous reading or you can chain >> callback function to perform asynchronous reading.
     *
     * @code
     * std::vector<char> data;
     * stream.read_block(data, size); //read data synchronously to the data vector
     *
     * bool ok = stream.read_block(data,size); //read data synchronously
     *
     * stream.read_block(data, size) >> [=](bool ok, std::vector<char> &data) {
     * ...
     * }
     *
     * stream.read_block(data, size) >> [=](Stream &stream, bool ok, std::vector<char> &data) {
     * ...  //transfer Stream ownership to the callback
     * }
     *
     */
    ReadBlockHelper read_block(std::vector<char> &buffer, std::size_t size) {
        return ReadBlockHelper(this, buffer, size);
    }


    ///Retrieves buffered amount of written data
    /**
     * @return buffered amount. Note this feature is available for buffered streams only,
     * otherwise function returns zero
     */
    std::size_t get_buffered_amount() const {
        auto ptr = dynamic_cast<const IBufferedStreamInfo *>(PtrType::get());
        if (ptr) return ptr->get_buffered_amount(); else return 0;
    }

protected:
    static auto find_separator(const std::string_view &text, const std::string_view &separator, std::size_t newdatapos) {
        if (newdatapos<separator.size()) {
            return text.find(separator);
        } else {
            return text.find(separator, newdatapos-separator.size());
        }
    }

    void get_line_async_cont(std::string_view data, std::string &&separator, std::string &&buffer, Callback<void(bool, std::string &)> &&cb) {
        if (data.empty()) {
            cb(false, buffer);
        } else {
            buffer.append(data);
            auto n = find_separator(buffer, separator, buffer.size() - data.size());
            if (n != buffer.npos) {
                put_back(data.substr(data.size() - (buffer.size() - n - separator.size())));
                buffer.resize(n);
                cb(true,buffer);
                return;
            }
            read_async([this,separator = std::move(separator), buffer = std::move(buffer), cb = std::move(cb)](std::string_view data) mutable {
                get_line_async_cont(data, std::move(separator), std::move(buffer), std::move(cb));
            });
        }
    }

    template<typename Fn>
    void read_block_async(std::size_t sz, std::vector<char> &&buffer, Fn &&fn) {
        read_async([this,sz, buffer = std::move(buffer), fn = std::forward<fn>(fn)](std::string_view data) mutable {
            if (data.empty()) {
                fn(false, buffer);
            } else {
                auto s = data.substr(0,sz);
                auto c = data.substr(s.length());
                put_back(c);
                buffer.insert(buffer.end(), s.begin(), s.end());
                auto newsz = sz - s.length();
                if (newsz) {
                    read_block_async(newsz, std::move(buffer), std::move(fn));
                } else {
                    fn(true,buffer);
                }
            }
        });
    }
};

struct EmptyDeleter {
    template<typename T>
    void operator()(T *) {/* empty */}
};

using Stream = Stream_t<std::unique_ptr<AbstractStreamInstance> >;
using StreamRef = Stream_t<std::unique_ptr<AbstractStreamInstance, EmptyDeleter> >;
using SharedStream = Stream_t<std::shared_ptr<AbstractStreamInstance> >;

///Weak shared stream is similar to weak_ptr.
/**
 * It refers to existing stream, reflects its closing and disappearence.
 * To access the stream, you need to call lock(), which converts this object
 * to SharedStream.
 *
 * Useful in for example subscribe-publisher pattern, when original stream is closed,
 * publisher finds stream closed and removes it from subscribers
 */
class WeakStreamRef {
public:

    WeakStreamRef(const SharedStream &stream):
        _ptr(stream) {}

    ///Locks the stream, retrieves SharedStream
    /**
     * @param target empty object SharedStream (uninitialized)
     * @retval true lock successful
     * @retval false stream no longer available
     */
    bool lock(SharedStream &target) {
        auto p = _ptr.lock();
        if (p != nullptr) {
            target = SharedStream(std::move(p));
            return true;
        } else {
            return false;
        }
    }

    ///Determines, whether stream has been closed
    /**
     * @retval true, stream has been closed
     * @retval false, stream is not closed - note this information doesn't mean, that
     * stream is still opened. You need to call lock() to be sure
     */
    bool expired() const {
        return _ptr.expired();
    }

protected:
    std::weak_ptr<AbstractStreamInstance> _ptr;
};

class Socket;

Stream createSocketStream(Socket &&s);
Stream createSocketStream(std::unique_ptr<ISocket> &&socket);

///Creates new object which refers the stream passed as argument
/**
 * @param stream object to be referenced
 * @return new stream object
 *
 * New stream object shares state of original object.
 *
 * @note Reference lifetime is limited to lifetime of original object. It is undefined
 * behavior accessing stream object after original object is destroyed. It is better to use
 *
 */
Stream createStreamReference(Stream &stream);

///Converts stream to buffered stream
/**
 * Buffered stream allows multi-threaded writes without need to wait on completion.
 * Function write_async() can accept nullptr as callback function.
 *
 *
 * @param stream non-buffered stream
 * @return buffered stream
 *
 * @exception std::bad_cast if the underlying stream cannot be converted to buffered stream
 *
 * @note passed stream is moved - so it can be no longer used.;
 */
Stream createBufferedStream(Stream &&stream);


}



#endif /* SRC_LIBS_USERVER_STREAM_H_ */
