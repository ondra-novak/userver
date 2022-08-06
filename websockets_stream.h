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
 * until all pending operation completes (mostly unsuccessful). It also beaks receive
 * loop, if there is such active. Enequeued messages are not sent during destruction
 * (there is no linger). If you need to ensure, that all data has been sent, you
 * need wait manually, for example using flush() and then destroy the stream. It is also
 * good idea to inform other side about closing the stream before it is being closed.
 */
class WSStream: public WebSocketsConstants {
public:

    struct Message {
        ///frame type
        WSFrameType type;
        ///frame data
        std::string_view data;
        ///code associated with the frame
        /** It is applied only for certain types of frame, otherwise it is zero */
        unsigned int code;
    };

    class ReadHelper final {
    public:
        ReadHelper(WSStream &owner):owner(owner) {}
        ReadHelper(const ReadHelper &) = default;
        ReadHelper &operator=(const ReadHelper &) = delete;

        operator Message() const {
            return owner.recv_sync();
        }
        template<typename Fn> void operator>>(Fn &&fn) {
            owner.recv_async(std::forward<Fn>(fn));
        }
    protected:
        WSStream &owner;
    };


    WSStream(Stream &&s, bool client);
    /**
     * @param other moves instance to the different object
     *
     * @note, you can move instance until it is used for the first time! When there are
     * pending operations, moving instance causes undefined behavior
     */
    WSStream(WSStream &&other);
    ~WSStream();


    ///Read the next message
    /**
     * @return ReadHelper object which can be converted to Message,
     * which causes synchronous reading, or by chaining callback causes asynchronous
     * reading.
     *
     * When special message is read, it is automatically handled. For example, if ping
     * message is read, it is automatically responded with pong message. The same for
     * close message
     *
     * If stream timeouts, WSFrameType::incomplete is returned. The message doesn't contain
     * data.
     */
    ReadHelper recv() {return ReadHelper(*this);}

    ///Reads message synchronously
    /**
     * @return block and returns message, when it is received. In case of timeout,
     * WSFrameType::incomplete is returned. In case of reset, WSFrameType::connClose
     * is returned with reason closeConnReset
     *
     * When special message is read, it is automatically handled. For example, if ping
     * message is read, it is automatically responded with pong message. The same for
     * close message
     *
     * @note Function is not MT Safe for receiving. Only one thread can call this function.
     * It is also not possible to use recvSync while recvAsync is still pending, this
     * results as undefined behavior
     */
    Message recv_sync();

    ///Reads message asynchronously
    /**
     * @param fn a callback function which receives Message. see recvSync() for description
     * of various messages
     *
     * @note Function is not MT Safe for receiving. Only one thread can call this function.
     * Function can be called only if there is no other pending receiving including
     * recvSync(), otherwise results as undefined behavior
     *
     */
    template<typename Fn>
    void recv_async(Fn &&fn);


    ///Reads messages in a loop
    /**
     * @param fn a callback function which receives Message. It can return true to continue
     * loop, or false to exit loop. Automatically handles ping/pong messages, so these
     * messages wouldn't be passed to the callback function. If the connection is closed,
     * then WSFrameType::connClose is passed to the callback function. In this case,
     * return value is ignored and loop is exited
     */
    template<typename Fn>
    void recv_async_loop(Fn &&fn);

    ///Sends text frame
    /**
     * @param data content of the frame
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe.
     */
    bool send_text(const std::string_view &data);

    ///Sends binary frame
    /**
     * @param data content of the frame
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_binary(const std::string_view &data);

    ///Sends ping frame
    /**
     * @param data optionally data send along with the request
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_ping(const std::string_view &data = std::string_view());

    ///Sends pong frame
    /**
     * @param data data of the frame (mostly copied from the Message)
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_pong(const std::string_view &data);

    ///Request to close connection
    /**
     * @param code reason of closing (optional)
     * @retval true enqueued
     * @retval false connection is closed
     *
     * @note MT Safety - function is MT Safe
     */
    bool send_close(int code = closeNormal);


    ///Retrieves buffered amount of bytes
    /**
     * @return the number of bytes of data that have been queued
     * using calls to send_xxxx() but not yet transmitted to the network.
     * This value resets to zero once all queued data has been sent.
     * This value does not reset to zero when the connection is closed;
     * if you keep calling send_xxx(), this will continue to climb.
     */
    std::size_t get_buffered_amount() const;

    ///Creates future, which is resolved, one the output buffer is emptied
    /**
     * @return future. The future resolves by true, if the flush completed, or false, if
     * the flush failed, because stream is in disconnected/timeouted state. It also resolves
     * as false in case, that stream is being destroyed
     *
     * @note MT Safety - function is MT Safe
     *
     * @note If there are additional sends after the future is created, they are not
     * counted in. So the future becomes resolved once the buffered amount bytes
     * known in time of future creation is written to the network regardless on
     * how many bytes has been enqueued later
     */
    std::future<bool> flush();

    ///Flushes asynchronously
    /**
     * For more information, see flush(). This function allows to perform flush
     * asynchronously, which means, that it calls callback, when flush is done
     * @param cb
     */
    template<typename Fn>
    void flush_async(Fn &&cb);

    ///Determines, whether reading timeouted. You can check this status after WSFrameType::incomplete is received
    /**
     * @retval true timeouted
     * @retval false not timeouted
     */
    bool timeouted() const;

    ///Clears timeout flag
    /**
     * You need to clear timeout if you want to continue in reading after stream timeouted
     */
    void clear_timeout();

protected:


    WebSocketParser _parser;
    WebSocketSerializer _serializer;
    std::mutex _serializer_lock;
    std::atomic<unsigned int> _close_code = 0;
    Stream _s;


    Message get_message() const;
    Message get_close_message() const;
    void handle_special_message(const Message &msg);

    template<typename Fn>
    void recv_async_loop2(Fn &&fn, bool ping_sent);

    void finish_write(bool ok);

};




inline WSStream::Message WSStream::recv_sync() {
    if (_close_code.load()) {
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
inline void WSStream::recv_async(Fn &&fn) {
    if (_close_code.load()) {
        fn(get_close_message());
    } else {
        _s.read() >> [=, fn = std::forward<Fn>(fn)](const std::string_view &data) mutable {
            if (data.empty()) {
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
inline void WSStream::recv_async_loop(Fn &&fn) {
    recv_async_loop2(std::forward<Fn>(fn), false);
}

template<typename Fn>
inline void WSStream::recv_async_loop2(Fn &&fn, bool ping_sent) {
    auto stream = _s.get();
    recv() >> [=, fn = std::forward<Fn>(fn)](Message &&msg) mutable {
        do {
            //examine message
            switch (msg.type) {
                case WSFrameType::connClose:
                        fn(msg);
                        return;
                case WSFrameType::incomplete:
                    if (timeouted()) {
                        if (ping_sent) {
                            unsigned int need = 0;
                            _close_code.compare_exchange_strong(need, closeConnTimeout);
                            fn(get_close_message());
                        } else {
                            clear_timeout();
                            send_ping();
                            recv_async_loop2(std::forward<Fn>(fn), true);
                        }
                    } else {
                        unsigned int need = 0;
                        _close_code.compare_exchange_strong(need, closeConnReset);
                        fn(get_close_message());
                    }
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
    };
}

inline bool WSStream::send_text(const std::string_view &data) {
    std::lock_guard _(_serializer_lock);
    if (!_close_code) {
        _s.write_async(_serializer.forgeTextFrame(data),true,
                       Callback<void(bool)>(this,&WSStream::finish_write));
        return true;
    } else {
        return false;
    }
}

inline bool WSStream::send_binary(const std::string_view &data) {
    std::lock_guard _(_serializer_lock);
    if (!_close_code) {
        _s.write_async(_serializer.forgeBinaryFrame(data),true,
                       Callback<void(bool)>(this,&WSStream::finish_write));
        return true;
    } else {
        return false;
    }
}

inline bool WSStream::send_ping(const std::string_view &data) {
    std::lock_guard _(_serializer_lock);
    if (!_close_code) {
        _s.write_async(_serializer.forgePingFrame(data),true,
                       Callback<void(bool)>(this,&WSStream::finish_write));
        return true;
    } else {
        return false;
    }
}

inline bool WSStream::send_pong(const std::string_view &data) {
    std::lock_guard _(_serializer_lock);
    if (!_close_code) {
        _s.write_async(_serializer.forgePongFrame(data),true,
                       Callback<void(bool)>(this,&WSStream::finish_write));
        return true;
    } else {
        return false;
    }
}

inline bool WSStream::send_close(int code) {
    std::lock_guard _(_serializer_lock);
    if (!_close_code) {
        _s.write_async(_serializer.forgeCloseFrame(code),true,
                       Callback<void(bool)>(this,&WSStream::finish_write));
        return true;
    } else {
        return false;
    }
}

inline std::size_t WSStream::get_buffered_amount() const {
    return _s->get_pending_write_size();
}

inline std::future<bool> WSStream::flush() {
    std::promise<bool> promise;
    std::future<bool> future = promise.get_future();
    _s.write(std::string_view(), false) >> [promise = std::move(promise)](bool ok) mutable {
        promise.set_value(ok);
    };
    return future;
}

template<typename Fn>
void WSStream::flush_async(Fn &&cb) {
    _s.write_async(std::string_view(), false, std::forward<Fn>(cb));
}


inline bool WSStream::timeouted() const {
    return _close_code.load() == 0 && _s.timeouted();
}


inline void WSStream::clear_timeout() {
    _s.clear_timeout();
}

inline WSStream::Message WSStream::get_message() const {
    if (_close_code.load()) return get_close_message();
    auto t = _parser.getFrameType();
    if (t == WSFrameType::connClose) {
        return Message{t,std::string_view(),_parser.getCode()};
    } else {
        return Message{t, _parser.getData()};
    }
}

inline WSStream::Message WSStream::get_close_message() const {
    return Message{WSFrameType::connClose, std::string_view(), _close_code.load()};
}

inline void WSStream::handle_special_message(const Message &msg) {
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

inline WSStream::WSStream(Stream &&s, bool client)
:_serializer(client),_s(std::move(s))  {}

inline WSStream::WSStream(WSStream &&other)
: _parser(std::move(other._parser))
, _serializer(std::move(other._serializer))
, _s(std::move(other._s))
{
}

inline void WSStream::finish_write(bool ok) {
    unsigned int zero = 0;
    if (!ok) _close_code.compare_exchange_strong(zero, closeConnReset);
}

inline WSStream::~WSStream() {
    unsigned int zero = 0;
    //set this variable, during destruction of the stream, this halts all pending operations
    _close_code.compare_exchange_strong(zero, closeConnReset);
    //reset stream;
    _s.reset();
}


}
#endif /* SRC_USERVER_WEBSOCKETS_STREAM_H_ */
