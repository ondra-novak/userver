/*
 * websockets_stream.h
 *
 *  Created on: 5. 11. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_WEBSOCKETS_STREAM_H_
#define SRC_USERVER_WEBSOCKETS_STREAM_H_
#include "stream.h"
#include "websockets_parser.h"
#include "async_provider.h"
#include <future>

namespace userver {

class WSStream_Impl;

///Wraps stream to websocket stream allowing to send or receive message
/**
 * The stream supports both sync or async operations and also receive loop, which
 * allows you to write function to process incoming messages. Note that only
 * thread can handle reading and there can by only one pending reading at time.
 *
 * Sending messages is MT safe, any thread can send message
 *
 * There is write buffer, which enqueues messages to be send. You can check
 * buffer size by function get_buffered_amount(). You can also synchronously
 * wait to empty this buffer
 *
 * Destruction of the stream ends all pending operations. Destructor blocks thread
 * until all pending operation completes (mostly unsuccessful). It also breaks receive
 * loop, if there is such active. Enqueued messages are not sent during destruction
 * (there is no linger). If you need to ensure, that all data has been sent, you
 * need wait manually, for example using flush() and then destroy the stream. It is also
 * good idea to inform other side about closing the stream before it is being closed.
 *
 * Stream available as WSStream or SharedWSStream, depend on how it is used. You can
 * call make_shared() to convert WSStream to SharedWSStream. This interface is
 * in fact implemented as a smart pointer, and actual implementation is in
 * WSStream_Impl, which is inaccessible directly. WSStream can be moved as needed
 */

template<typename PtrImpl>
class WSStreamT: protected PtrImpl, public WebSocketsConstants {
public:

    using PtrImpl::PtrImpl;

    ///Construct WSStream from existing Stream
    /**
     * @param stream Stream object. Must be Stream, not SharedStream
     * @param client set true, if you create a client connection (connect), set false if it is
     *    server connection (accept). This is required as RFC states, client connection
     *    have to mask messages, on other hand server connection must not mask messages.
     */
    WSStreamT(Stream &&stream, bool client);

    ///Converts WSStream to SharedWSStream
    /**
     * It is available for both WSStream an SharedWSStream, while WSStream moves
     * instance to newly created SharedWSStream, while SharedWSStream only copy
     * instance to make new reference. Please note that it is still necessary to comply
     * with MT Safety requirements. Only one thread can read messages regardless on how many
     * times the instance is shared
     *
     * @return shared stream
     */
    WSStreamT<std::shared_ptr<WSStream_Impl> > make_shared();


    using Message = WSMessage;

    ///Helps with recv()
    /**
     * @see recv
     */
    class RecvHelper final {
    public:
        RecvHelper(WSStreamT &owner):_owner(owner.get()) {}
        RecvHelper(const RecvHelper &) = delete;
        RecvHelper &operator=(const RecvHelper &) = delete;

        operator Message() const {
            return _owner->recv_sync();
        }
        template<typename Fn> void operator>>(Fn &&fn) {
            _owner->recv_async(std::forward<Fn>(fn));
        }
    protected:
        typename PtrImpl::element_type *_owner;
    };

    ///Helps with recv_loop()
    /**
     * @see recv_loop
     */
    class RecvLoopHelper final {
    public:
        RecvLoopHelper(WSStreamT &owner):_owner(owner.get()) {}
        RecvLoopHelper(const RecvLoopHelper &) = delete;
        RecvLoopHelper &operator=(const RecvLoopHelper &) = delete;

        template<typename Fn> void operator>>(Fn &&fn) {
            _owner->recv_async_loop(std::forward<Fn>(fn));
        }
    protected:
        typename PtrImpl::element_type *_owner;
    };


    ///Helps with flush()
    /**
     * @see flush
     */
    class FlushHelper final {
    public:
        FlushHelper(WSStreamT &owner):_owner(owner.get()) {}
        FlushHelper(const FlushHelper &) = delete;
        FlushHelper &operator=(const FlushHelper &) = delete;

        template<typename Fn> void operator>>(Fn &&fn) {
            _owner->flush_async(std::forward<Fn>(fn));
            _owner = nullptr;
        }
        operator std::promise<bool>() {
            auto r = flush_sync();
            _owner = nullptr;
            return r;
        }
        operator bool() {
            auto r = flush_sync();
            _owner = nullptr;
            return r.get();
        }

        ~FlushHelper() {
            if (_owner) {
                flush_sync().get();
            }
        }
    protected:
        std::future<bool> flush_sync() {
            std::promise<bool> p;
            auto f = p.get_future();
            _owner->flush_async([p = std::move(p)](bool ok) mutable {
               p.set_value(ok);
            });
            return f;
        }
        typename PtrImpl::element_type *_owner;

    };

    ///Read the next message
    /**
     * @return RecvHelper object which can be converted to Message,
     * which causes synchronous reading, or by chaining callback causes asynchronous
     * reading.
     *
     * When special message is read, it is automatically handled. For example, if ping
     * message is read, it is automatically responded with pong message. The same for
     * close message
     *
     * If stream timeouts, WSFrameType::incomplete is returned. The message doesn't contain
     * data.
     *
     * @code
     * WSStream ws = .....
     * WSMessage msh = ws.recv(); //sync reading
     *
     * ws.recv() >> [=](WSMessage &&msg) {
     *    //.... async reading
     * };
     * @endcode
     *
     */
    RecvHelper recv() {return RecvHelper(*this);}

    ///Read the messages in a loop
    /**
     * @return RecvHelper which helps to chain a callback lambda function
     *
     * Calback function is called for every received message. Automatically handles ping,
     * pong and close messages. The callback function must return true to continue
     * in loop or false to break the loop. Receiving close message breaks loop regadless
     * on return value.
     *
     * @code
     * // WS echo service
     *
     * WSStream ws = .....
     * ws.recv_loop() >> [ws = std::move(ws)](WSMessage &&msg){
     *      if (msg.type == WSFrameType::text) {
     *          ws.send_text(msg.data);
     *      }
     * };
     * @endcode
     *
     */
    RecvLoopHelper recv_loop() {return RecvLoopHelper(*this);}

    ///Sends text frame
    /**
     * @param data content of the frame
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe.
     */
    bool send_text(const std::string_view &data) {
        return this->get()->send_text(data);
    }

    ///Sends binary frame
    /**
     * @param data content of the frame
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_binary(const std::string_view &data) {
        return this->get()->send_binary(data);
    }

    ///Sends ping frame
    /**
     * @param data optionally data send along with the request
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_ping(const std::string_view &data = std::string_view()) {
        return this->get()->send_ping(data);
    }

    ///Sends pong frame
    /**
     * @param data data of the frame (mostly copied from the Message)
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_pong(const std::string_view &data) {
        return this->get()->send_pong(data);
    }

    ///Request to close connection
    /**
     * @param code reason of closing (optional)
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_close(int code = closeNormal) {
        return this->get()->send_close(code);
    }


    ///Retrieves buffered amount of bytes
    /**
     * @return the number of bytes of data that have been queued
     * using calls to send_xxxx() but not yet transmitted to the network.
     * This value resets to zero once all queued data has been sent.
     * This value does not reset to zero when the connection is closed;
     * if you keep calling send_xxx(), this will continue to climb.
     */
    std::size_t get_buffered_amount() const {
        return this->get()->get_buffered_amount();
    }

    ///Flush the output stream
    /**
     * Note all sent messages are automatically processed by network, so no
     * explicit flushing is necessery. However you often need to slow down sending
     * or receive notification about certain data has been processed by network (so
     * they no longer sit in output buffer). This function can handle operation
     * by either synchronously or asynchronously
     *
     *
     * @return FlushHelper, you can chain a lambda function or not
     *
     * @code
     * WSStream ws = ..
     * ws.flush();   //flush synchronously
     * bool res = ws.flush(); //flush synchronously, retrieve a result (success or failure)
     * std::future<bool> = ws.flush(); //flush asynchronously, receive future
     * ws.flush() >> [](bool ok){...}; //flush asynchronously, call the lambda function on completion
     * @endcode
     */
    FlushHelper flush() {
        return FlushHelper(*this);
    }

    ///Determines, whether reading timeouted. You can check this status after WSFrameType::incomplete is received
    /**
     * @retval true timeouted
     * @retval false not timeouted
     */
    bool timeouted() const {
        return this->get()->timeouted();
    }

    ///Clears timeout flag
    /**
     * You need to clear timeout if you want to continue in reading after stream timeouted
     */
    void clear_timeout() {
        this->get()->clear_timeout();
    }

    friend class WeakWSStreamRef;

    WSStreamT(PtrImpl &&imp): PtrImpl(std::move(imp)) {}


};

class WSStream_Impl: public WebSocketsConstants {
public:


    using Message = WSMessage;



    WSStream_Impl(Stream &&s, bool client);
    WSStream_Impl(WSStream_Impl &&other);
    ~WSStream_Impl();
    Message recv_sync();
    template<typename Fn>
    void recv_async(Fn &&fn);
    template<typename Fn>
    void recv_async_loop(Fn &&fn);
    bool send_text(const std::string_view &data);
    bool send_binary(const std::string_view &data);
    bool send_ping(const std::string_view &data = std::string_view());
    bool send_pong(const std::string_view &data);
    bool send_close(int code = closeNormal);
    std::size_t get_buffered_amount() const;
    std::future<bool> flush();
    template<typename Fn>
    void flush_async(Fn &&cb);
    void clear_timeout();

protected:

    using FlushList = std::vector<Callback<void(bool)> >;

    WebSocketParser _parser;
    WebSocketSerializer _serializer;
    mutable std::recursive_mutex _mx;
    std::atomic<unsigned int> _close_code = 0;
    std::vector<char> _buffer;
    FlushList _flush_list;
    bool _pending_write = false;
    Stream _s;


    Message get_message() const;
    Message get_close_message() const;
    void handle_special_message(const Message &msg);

    template<typename Fn>
    void recv_async_loop2(Fn &&fn, bool ping_sent);

    void finish_write(bool ok);
    bool send_frame(const std::string_view &frame);

};


using WSStream = WSStreamT<std::unique_ptr<WSStream_Impl> >;
using SharedWSStream = WSStreamT<std::shared_ptr<WSStream_Impl> >;

///Held weak reference to WSStream. You need to lock to obtain WSStream.
class WeakWSStreamRef {
public:
    WeakWSStreamRef(const SharedWSStream &s):_ptr(s) {}
    ///Locks the stream, retrieves SharedWSStream
    /**
     * @param target empty object SharedWSStream (uninitialized)
     * @retval true lock successful
     * @retval false stream no longer available
     */
    bool lock(SharedWSStream &target)  const{
        auto p = _ptr.lock();
        if (p != nullptr) {
            target = SharedWSStream(std::move(p));
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
    std::weak_ptr<WSStream_Impl> _ptr;
};


inline WSStream_Impl::Message WSStream_Impl::recv_sync() {
    if (_close_code.load(std::memory_order_relaxed)) {
        return get_close_message();
    }
    do {
        std::string_view data = _s.read();
        if (data.empty()) {
            return Message{WSFrameType::incomplete};
        }
        _s.put_back(_parser.parse(data));
        if (_parser.isComplete()) {
            Message m = get_message();
            handle_special_message(m);
            _parser.reset();
            return m;
        }
    } while (true);
}

template<typename Fn>
inline void WSStream_Impl::recv_async(Fn &&fn) {
    if (_close_code.load(std::memory_order_relaxed)) {
        fn(get_close_message());
    } else {
        _s.read() >> [=, fn = std::forward<Fn>(fn)](const ReadData &data) mutable {
            if (data.is_timeouted()) {
                fn(Message{WSFrameType::timeout});
            } else if (data.empty()) {
                fn(Message{WSFrameType::incomplete});
            } else {
                _s.put_back(_parser.parse(data));
                if (_parser.isComplete()) {
                    Message m = get_message();
                    handle_special_message(m);
                    _parser.reset();
                    fn(std::move(m));
                } else {
                    recv_async(std::forward<Fn>(fn));
                }
            }
        };
    }
}

template<typename Fn>
inline void WSStream_Impl::recv_async_loop(Fn &&fn) {
    recv_async_loop2(std::forward<Fn>(fn), false);
}

template<typename Fn>
inline void WSStream_Impl::recv_async_loop2(Fn &&fn, bool ping_sent) {
    auto stream = _s.get();
    recv_async([=, fn = std::forward<Fn>(fn)](Message &&msg) mutable {
        do {
            //examine message
            switch (msg.type) {
                case WSFrameType::connClose:
                        fn(msg);
                        return;
                case WSFrameType::timeout:
                    if (ping_sent) {
                        unsigned int need = 0;
                        _close_code.compare_exchange_strong(need, closeConnTimeout, std::memory_order_relaxed);
                        fn(get_close_message());
                    } else {
                        clear_timeout();
                        send_ping();
                        recv_async_loop2(std::forward<Fn>(fn), true);
                    }                    
                    return;
                case WSFrameType::incomplete:
                    unsigned int need = 0;
                    _close_code.compare_exchange_strong(need, closeConnReset, std::memory_order_relaxed);
                    fn(get_close_message());
                    return;
                case WSFrameType::ping:
                case WSFrameType::pong:
                        break;
                default:
                    if (!fn(msg)) return;
                    break;
            };

            _parser.reset();
            //fetch unprocessed data (can be empty)
            std::string_view pb = stream->read_sync_nb();
            //process data and put back rest (function checks whether it is empty)
            stream->put_back(_parser.parse(pb));
            //if message is not complete)
            if (!_parser.isComplete()) {
                //continue asynchronously
                recv_async_loop2(std::forward<Fn>(fn), false);
                return;
            }
            //retrieve message (overwrite previous message);
            msg = get_message();
            //handle special messages
            handle_special_message(msg);
            //continue by examining message
        }while (true);
    });
}

inline bool WSStream_Impl::send_text(const std::string_view &data) {
    std::lock_guard _(_mx);
    return send_frame(_serializer.forgeTextFrame(data));
}

inline bool WSStream_Impl::send_binary(const std::string_view &data) {
    std::lock_guard _(_mx);
    return send_frame(_serializer.forgeBinaryFrame(data));
}

inline bool WSStream_Impl::send_ping(const std::string_view &data) {
    std::lock_guard _(_mx);
    return send_frame(_serializer.forgePingFrame(data));
}

inline bool WSStream_Impl::send_pong(const std::string_view &data) {
    std::lock_guard _(_mx);
    return send_frame(_serializer.forgePongFrame(data));
}

inline bool WSStream_Impl::send_close(int code) {
    std::lock_guard _(_mx);
    return send_frame(_serializer.forgeCloseFrame(code));
}

inline std::size_t WSStream_Impl::get_buffered_amount() const {
    std::lock_guard _(_mx);
    return  _buffer.size();
}

template<typename Fn>
void WSStream_Impl::flush_async(Fn &&cb) {
    std::lock_guard _(_mx);
    _flush_list.push_back(std::forward<Fn>(cb));
}



inline void WSStream_Impl::clear_timeout() {
    _s.clear_timeout();
}

inline WSStream_Impl::Message WSStream_Impl::get_message() const {
    if (_close_code.load()) return get_close_message();
    auto t = _parser.getFrameType();
    if (t == WSFrameType::connClose) {
        return Message{t,std::string_view(),_parser.getCode()};
    } else {
        return Message{t, _parser.getData()};
    }
}

inline WSStream_Impl::Message WSStream_Impl::get_close_message() const {
    return Message{WSFrameType::connClose, std::string_view(), _close_code.load()};
}

inline void WSStream_Impl::handle_special_message(const Message &msg) {
    unsigned int zero = 0;
    switch (msg.type) {
    case WSFrameType::connClose:
        send_close(closeNormal);
        _close_code.compare_exchange_strong(zero, msg.code);
        break;
    case WSFrameType::ping:
        send_pong(msg.data);
        break;
    default:
        break;
    };
}

inline WSStream_Impl::WSStream_Impl(Stream &&s, bool client)
:_serializer(client),_s(std::move(s))  {}

inline WSStream_Impl::WSStream_Impl(WSStream_Impl &&other)
: _parser(std::move(other._parser))
, _serializer(std::move(other._serializer))
, _s(std::move(other._s))
{
}

inline void WSStream_Impl::finish_write(bool ok) {
    FlushList tmp;
    {
        std::lock_guard _(_mx);
        std::swap(tmp,_flush_list);
        unsigned int zero = 0;
        if (!ok) _close_code.compare_exchange_strong(zero, closeConnReset,std::memory_order_relaxed);
        if (ok && !_buffer.empty()) {
            _s.write_async(std::string_view(_buffer.data(), _buffer.size()),
                           [=, b = std::move(_buffer)](bool ok) {
                return finish_write(ok);
            });
        } else {
            _pending_write = false;
        }
    }
    for (const auto &cb : tmp) {
        cb(ok);
    }
}

inline bool WSStream_Impl::send_frame(const std::string_view &frame) {
    if (_close_code.load(std::memory_order_relaxed)) return false;
    if (_pending_write) {
        _buffer.insert(_buffer.end(), frame.begin(), frame.end());
    } else {
        _pending_write = true;
        _s.write_async(frame, [=](bool ok){finish_write(ok);});
    }
    return true;
}

inline WSStream_Impl::~WSStream_Impl() {
    unsigned int zero = 0;
    //set this variable, during destruction of the stream, this halts all pending operations
    _close_code.compare_exchange_strong(zero, closeConnReset);
    //reset stream;
    _s.reset();
}



template<>
inline WSStreamT<std::shared_ptr<WSStream_Impl> > WSStreamT<std::unique_ptr<WSStream_Impl> >::make_shared() {
    return WSStreamT<std::shared_ptr<WSStream_Impl> >(release());
}

template<>
inline WSStreamT<std::shared_ptr<WSStream_Impl> > WSStreamT<std::shared_ptr<WSStream_Impl> >::make_shared() {
    return *this;
}
template<>
inline WSStreamT<std::unique_ptr<WSStream_Impl> >::WSStreamT(Stream &&stream, bool client)
:std::unique_ptr<WSStream_Impl> (std::make_unique<WSStream_Impl>(std::move(stream), client)) {}

template<>
inline WSStreamT<std::shared_ptr<WSStream_Impl> >::WSStreamT(Stream &&stream, bool client)
:std::shared_ptr<WSStream_Impl> (std::make_shared<WSStream_Impl>(std::move(stream), client)) {}


}
#endif /* SRC_USERVER_WEBSOCKETS_STREAM_H_ */
