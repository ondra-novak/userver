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

namespace userver {


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
    virtual std::string_view read_sync() = 0;

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
    virtual void read_async(Callback<void(std::string_view)> &&callback) = 0;

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

    ///Writes to the output asynchronously
    /**
     * @param buffer buffer to write. Note if the buffer is empty, the operation is still queued
     * @param copy_content se true to copy content, which allows to discard buffer during the pending operation.
     *  However copying is extra operation, so if you are to sure that buffer will not disappear
     *  during the operation, you can set this parameter to false
     * @param callback callback function which is called when operation is done. The value
     * 'true' is passed when data has been send, or false, when error happened (or timeout).
     * This parametr can be nullptr, in this case, no callback is called
     *
     * @note Do not use this function to send single characters, as the function
     * is to complex to be less efficient in this case. Use put_char() for this purpose
     *
     * @note Multiple threads can use this function, all write requests are queued and
     * processed serialized
     *
     * @note To determine, which error caused loosing the connection, you can check
     * for current exception. The exception pointer is set when there was an error. If
     * the exception pointer is not set, there were timeout out or EOF
     */
    virtual void write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) = 0;
    ///Closes the output
    /**
     * Operation closes output, so other side receives EOF. No futher writes
     * are allowed.
     *
     * If there are pending writes, the call is enqueued and the output is closed
     * once all requests are processed
     */
    virtual void close_output() = 0;

    ///Retrieves current pending write size
    /** total count of bytes to be written . You can use this to
     * chech high watermark level - to slow down writes if the
     * number is too high
     *
     * @return bytes waiting to be written
     *
     * @note function doesn't work for sync writes
     */
    virtual std::size_t get_pending_write_size() const = 0;


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


    //character write interface
    ///Puts character to an internal buffer
    /**
     * @param c character to put
     * @retval true internal buffer is large enough to be flushed
     * @retval false internal buffer is still small to be flushed, collect more bytes
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual bool put(char c) = 0;
    ///Puts block to an internal buffer
    /**
     * @param block block to put
     * @retval true internal buffer is large enough to be flushed
     * @retval false internal buffer is still small to be flushed, collect more bytes
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual bool put(const std::string_view &block) = 0;

    ///Retrieves size of put buffer (already written data)
    virtual std::size_t get_put_size() const = 0;

    ///Discard content of put buffer
    /**
     * @return content of put buffer is returned (it is cleared internally)
     */
    virtual std::vector<char> discard_put_buffer() = 0;

    ///Flushes internal buffer to the stream
    /**
     * @retval true data are sent
     * @retval false error detected
     *
     * @note this operation is at same level as write_sync and write_async. Internally it
     * is implemented as passing internal buffer to the function write_sync. Read
     * the description of this function for further informations
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */

    virtual bool flush_sync() = 0;
    ///Flush internal buffer asynchronously
    /**
     * @param cb a callback function invoked when operation is complete. The
     * passed argument contains 'true' as success and 'false' as error
     * This parametr can be nullptr, in this case, no callback is called
     *
     * @note this operation is at same level as write_sync and write_async. Internally it
     * is implemented as passing internal buffer to the function write_async. Read
     * the description of this function for further informations
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual void flush_async(Callback<void(bool)> &&cb) = 0;

    //misc
    ///Determines, whether read operation timeouted
    /**
     * @retval true operation timeouted, you can call clear_timeout() to continue
     * @retval false operation was not timeouted, probably error or eof, calling clear_timeout()
     * will not help
     */
    virtual bool timeouted() = 0;
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

template<typename PtrType>
class Stream_t: public PtrType {
public:

    using PtrType::PtrType;

    Stream_t(const PtrType &other):PtrType(other) {}
    Stream_t(PtrType &&other):PtrType(std::move(other)) {}

    std::string_view read_sync() {return (*this)->read_sync();}
    void read_async(Callback<void(std::string_view)> &&callback) {(*this)->read_async(std::move(callback));}
    void put_back(const std::string_view &buffer) {(*this)->put_back(buffer);}
    void close_input() {(*this)->close_input();}
    void timeout_async_read() {(*this)->timeout_async_read();}
    bool write_sync(const std::string_view &buffer) {return (*this)->write_sync(buffer);}
    void write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) {(*this)->write_async(buffer, copy_content, std::move(callback));}
    void timeout_async_write() {(*this)->timeout_async_write();}
    void close_output() {(*this)->close_output();}
    bool put(char c) {return (*this)->put(c);}
    bool put(const std::string_view &block) {return (*this)->put(block);}
    bool flush_sync() {return (*this)->flush_sync();}
    void flush_async(Callback<void(bool)> &&cb) {(*this)->flush_async(std::move(cb));}
    bool timeouted() {return (*this)->timeouted();}
    void clear_timeout() {(*this)->clear_timeout();}
    void set_read_timeout(int tm_in_ms) {(*this)->set_read_timeout(tm_in_ms);}
    void set_write_timeout(int tm_in_ms) {(*this)->set_write_timeout(tm_in_ms);}
    void set_io_timeout(int tm_in_ms) {(*this)->set_rw_timeout(tm_in_ms);}
    int get_read_timeout() const {return (*this)->get_read_timeout();}
    int get_write_timeout() const {return (*this)->get_write_timeout();}
    std::size_t get_put_size() const {return (*this)->get_put_size();}
    std::vector<char> discard_put_buffer() {return (*this)->discard_put_buffer();}

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
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream_t &>(),std::declval<std::string_view>())) {
            _owner->read_async([s = std::move(*_owner), fn = std::forward<Fn>(fn)](const std::string_view &data) mutable {
                fn(s, data);
            });
            _owner = nullptr;
        }

    protected:
        Stream_t *_owner;
        ReadHelper(Stream_t *owner):_owner(owner) {}
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
            _owner->write_async(_buffer, _copy_content, std::forward<Fn>(fn));
            _owner = nullptr;
        }
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream_t &>(),std::declval<bool>())) {
            _owner->read_async([s = std::move(*_owner), fn = std::forward<Fn>(fn)](bool x) mutable {
                fn(s, x);
            });
            _owner = nullptr;
        }
        ~WriteHelper() {
            if (_owner) _owner->write_sync(_buffer);
        }
    protected:
        Stream_t *_owner;
        std::string_view _buffer;
        bool _copy_content;
        WriteHelper(Stream_t *owner, const std::string_view &buffer, bool copy_content)
            :_owner(owner)
            ,_buffer(buffer)
            ,_copy_content(copy_content) {}
        WriteHelper(const WriteHelper &other) = delete;
        WriteHelper &operator=(const WriteHelper &other) = delete;
        friend class Stream_t;
    };



    class FlushHelper {
    public:
        operator bool() {
            auto out = _owner->flush_sync();
            _owner = nullptr;
            return out;
        }
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<bool>())) {
            _owner->flush_async(std::forward<Fn>(fn));
            _owner = nullptr;
        }
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream_t &>(),std::declval<bool>())) {
            _owner->flush_async([s = std::move(*_owner), fn = std::forward<Fn>(fn)](bool x) mutable {
                fn(s, x);
            });
            _owner = nullptr;
        }
        ~FlushHelper() {
            if (_owner) _owner->flush_sync();
        }

    protected:
        Stream_t *_owner;
        FlushHelper(Stream_t *owner):_owner(owner) {}
        FlushHelper(const FlushHelper &other) = delete;
        FlushHelper &operator=(const FlushHelper &other) = delete;
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
        template<typename Fn>
        auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<Stream_t &>(),std::declval<bool>(),std::declval<std::vector<char> &>())) {
            _owner->read_block_async(_size, std::move(_buffer),
                        [s = std::move(*_owner),fn = std::forward<Fn>(fn)](bool ok, std::vector<char> &buffer) mutable{
                fn(s, ok, buffer);
            });
            _owner = nullptr;
        }
        ~ReadBlockHelper() {
            _owner && (*this);
        }


    protected:
        Stream_t *_owner;
        std::vector<char> &_buffer;
        std::size_t _size;
        ReadBlockHelper(Stream_t *owner, std::vector<char> &buffer, std::size_t size)
            :_owner(owner),_buffer(std::move(buffer)),_size(size) {}
        ReadBlockHelper(const FlushHelper &other) = delete;
        ReadBlockHelper &operator=(const FlushHelper &other) = delete;
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
    ReadHelper read() {return ReadHelper(this);}
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
    WriteHelper write(const std::string_view &buffer, bool copy_content = true) {
        return WriteHelper(this, buffer, copy_content);
    }
    ///Generic flush, you can choose between sync and async version by use
    /**
     * @return object can be converted to bool which executes synchronous flush.
     * You can also use operator >> to forward reading to completion function which executes
     * asynchronous flush. If none used, synchronous write is finally executed.
     *
     * @code
     *
     * stream.flush(); //synchronous
     *
     * bool ok = stream.flush(); //synchronous
     *
     * stream.flush() >> [=](bool ok) { //asynchronous
     *    ...
     * };
     *
     * stream.flush() >> nullptr;   //asynchronous
     * @endcode
     */
    FlushHelper flush() {return FlushHelper(this);}

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
}



#endif /* SRC_LIBS_USERVER_STREAM_H_ */
