/*
 * websockets_stream.h
 *
 *  Created on: 5. 11. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_WEBSOCKETS_STREAM_H_
#define SRC_USERVER_WEBSOCKETS_STREAM_H_
#include <userver/stream.h>
#include "websockets_parser.h"

namespace userver {

class WSStream;

///Web socket stream
/**
 * Object also solves some MT safety
 *
 * Object allows to send message frames, through method send(). Multiple threads can
 * send message, and sending message is done asynchronously (without need to wait for flush())
 *
 * Object allows to receive messages through either synchronous or asynchronous call. In
 * both cases it can handle ping from remote server and close request from remote server by
 * sending apropriate response. However these frames are still obtainable by recv()
 *
 * Asynchronous recv() can also handle read timeout by sending ping packet to keep connection
 * alive. It sends only one ping packed and if the connection is inactive for next period of
 * time, the timeout event is passed to the caller. However connection is still not flagged as
 * broken, and caller can use another option to determine, whether connection is live or dead
 *
 * To continuously receive frames, caller must rearm the reading after processing each message.
 *
 *
 *
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
        int code;
    };

    class Promise final {
    public:
        Promise(WSStream &owner):owner(owner) {}
        Promise(const Promise &) = delete;
        Promise &operator=(const Promise &) = delete;
        Promise(Promise &&x):owner(x.owner) {}

        operator Message();
        template<typename Fn> void operator>>(Fn &&fn);
    protected:
        WSStream &owner;
    };

    ///Construct WSStream from opened TCP stream (wsConnect or wsConnectAsync)
    /**
     * @param s established connection - stream
     * @param client set true, if the this side of stream is client, set false for server
     */
    WSStream(Stream &&s, bool client);

    WSStream(WSStream &&other):state(other.state) {
        other.state = nullptr;
    }

    ///Destructor
    ~WSStream() {
        shutdownRecv();
    }
    ///Receive message either synchronously or asynchronously
    /**
     * @return helper object. You can convert result to Message to perform actual synchronous reading,
     * or chain >> lambda function to perform reading asynchronously
     *
     * @code
     * Message msg = wss.recv()
     *
     * wss.recv() >> [](const Message &msg) {
     *                          ...
     *             };
     *
     * @endcode
     *
     * @note MT safety is not satisfied. Only one thread is allowed to receive messages at time.
     */
    Promise recv();

    ///Send message
    /**
     * @param type type of message
     * @param data message payload
     * @retval true message sent or queued to be sent
     * @retval false message was not send, because stream is closed
     *
     * @note MT safety is satisfied. Multiple threads can write messages without need to lock
     */
    bool send(WSFrameType type, std::string_view data);
    ///Request to close
    /**
     * Sends close request to the other side. This can cause, that stream will be closed
     *
     * @note This call doesn't close the stream. You can still receive messages. However you
     * cannot send any futher message
     * @param code closing code
     * @retval true success
     * @retval false can't send, stream is probably closing
     */
    bool close(unsigned int code = WebSocketsConstants::closeNormal);
    ///Determines whether stream timeouted
    /**
     * During recv() you can receive WSFrameType::incomplete which means, that stream
     * was interrupted during receiving next frame. It can happen for either connection
     * reset or timeout. You can use this function to determine, whether connection timeouted.
     *
     * To continue reading timeouted connection, you need to clearTimeout();
     *
     * @retval true connection is flagged as timeouted
     * @retval false connection is not flagged as timeouted
     */
    bool timeouted() const;
    ///Clears timeout flag
    void clearTimeout();
    ///Allows to shutdown asynchronous receive before stream is destroyed
    /**
     * A big struggle can be to ensure, that there is no asynchronous pending call before
     * the stream is destroyed. This call causes, that if there is such pending call, it
     * can arrive only before this function exits, then any pending call is canceled. This
     * function also blocks any futher recv()
     */
    void shutdownRecv();


protected:

    struct State {
        std::recursive_mutex mx;
        ///sending is closed due error or peer reset, this prevent further send
        bool send_closed = false;
        ///receiving is closed because requested, so no recv() callback will not be called
        bool recv_closed = false;
        ///pending flush, send is temporary blocked putting all data to queue
        bool flushing = false;
        ///requested to close connection, no further sends are allowed, however current queue is still able to flush
        bool pending_close = false;
        ///ping has been sent and no other packet received yet
        bool ping_sent = false;
        ///write queue
        std::string wrqueue;
        //stream
        Stream s;
        ///parser
        WebSocketParser parser;
        ///serializer
        WebSocketSerializer serializer;

        State(Stream &&s, bool client):s(std::move(s)),serializer(client) {}

        static bool write(std::shared_ptr<State> state, std::string_view data);
        static bool flush(std::shared_ptr<State> state, std::string_view data);
        Message getMessage() ;
        static void handleStdMessage(std::shared_ptr<State> state, const Message &msg);
    };

    using PState = std::shared_ptr<State>;

    PState state;

    Message recvSync();
    template<typename Fn> void recvAsync(Fn &&fn);


};

inline WSStream::WSStream(Stream &&s, bool client)
: state(std::make_shared<State>(std::move(s), client)) {

}

inline WSStream::Promise WSStream::recv() {
    return Promise(*this);
}
inline WSStream::Promise::operator Message() {
    return owner.recvSync();
}
template<typename Fn> inline void WSStream::Promise::operator >>(Fn &&fn) {
    owner.recvAsync(std::forward<Fn>(fn));
}
inline bool WSStream::send(WSFrameType type, std::string_view data){
    auto &st = *state;
    std::lock_guard _(st.mx);
    if (st.send_closed || st.pending_close) return false;

    std::string_view out;
    switch (type) {
    case WSFrameType::binary: out = st.serializer.forgeBinaryFrame(data);break;
    case WSFrameType::text: out = st.serializer.forgeTextFrame(data);break;
    case WSFrameType::ping: out = st.serializer.forgePingFrame(data);break;
    case WSFrameType::pong: out = st.serializer.forgePongFrame(data);break;
    default: return false;
    }

    return st.write(state, out);


}
inline bool WSStream::State::write(std::shared_ptr<State> state, std::string_view data) {
    auto &st = *state;
    if (st.flushing) {
        st.wrqueue.append(data);
        return true;
    } else {
        return st.flush(state, data);
    }
}
inline bool WSStream::State::flush(std::shared_ptr<State> state, std::string_view data) {
    auto &st = *state;
    if (st.send_closed) return false;
    st.s.writeNB(data);
    st.flushing = true;
    st.s.flush() >> [state](bool ok) {
           auto &st = *state;
           std::lock_guard _(st.mx);
           if (ok) {
               st.flushing = false;
               if (!st.wrqueue.empty()) {
                   write(state, st.wrqueue);
                   st.wrqueue.clear();
               }
           } else {
               st.send_closed = true;
           }

    };
    return true;
}

inline bool WSStream::close(unsigned int code) {
    auto &st = *state;
    std::lock_guard _(st.mx);
    if (st.send_closed) return false;
    st.pending_close = true;
    return st.write(state,st.serializer.forgeCloseFrame(code));

}


inline bool WSStream::timeouted() const {
    return state->s.timeouted();
}

///Clears timeout state, after which read request can be repeated
inline void WSStream::clearTimeout() {
    state->s.clearTimeout();
}

inline WSStream::Message WSStream::State::getMessage() {
    Message msg;
    msg.type = parser.getFrameType();
    if (msg.type == WSFrameType::connClose) {
        msg.code = parser.getCode();
    }
    msg.data = parser.getData();
    return msg;

}

inline WSStream::Message WSStream::recvSync() {
    auto &st = *state;
    std::string_view data = st.s.read();
    if (data.empty()) return Message{WSFrameType::incomplete};
    std::string_view extra = st.parser.parse(data);
    while (!st.parser.isComplete()) {
        st.s.putBack(extra);
        data = st.s.read();
        if (data.empty()) return Message{WSFrameType::incomplete};
        extra = st.parser.parse(data);
    }
    st.s.putBack(extra);
    Message msg = st.getMessage();
    st.handleStdMessage(state, msg);
    return msg;
}

template<typename Fn> inline void WSStream::recvAsync(Fn &&fn) {
    using CB = Callback<void(const std::string_view &)>;
    auto &st = *state;
    std::lock_guard _(st.mx);
    if (!st.recv_closed) {
        st.s.read() >> CB([state = this->state, fn = std::forward<Fn>(fn)](CB &me, std::string_view data) mutable {
            auto &st = *state;
            std::lock_guard _(st.mx);
            if (!st.recv_closed) {
                if (data.empty()) {
                    if (st.s.timeouted() && !st.ping_sent && st.write(state, st.serializer.forgePingFrame(""))) {
                        st.ping_sent = true;
                        st.s.clearTimeout();
                        st.s.read() >> std::move(me);
                    } else {
                        fn({WSFrameType::incomplete});
                    }
                } else {
                    st.ping_sent = false;
                    auto out = st.parser.parse(data);
                    st.s.putBack(out);
                    if (st.parser.isComplete()) {
                        Message msg= st.getMessage();
                        st.handleStdMessage(state, msg);
                        fn(msg);
                    } else {
                        st.s.read() >> std::move(me);
                    }
                }

            }
        });
    }
}

inline void WSStream::shutdownRecv() {
    if (state != nullptr) {
        auto &st = *state;
        std::lock_guard _(st.mx);
        st.recv_closed = true;
    }
}

inline void WSStream::State::handleStdMessage(std::shared_ptr<State> state, const Message &msg) {
    auto &st = *state;
    switch (msg.type) {
    case WSFrameType::connClose:
        write(state, st.serializer.forgeCloseFrame(msg.code));
        st.pending_close = true;
        st.send_closed = true;
        break;
    case WSFrameType::ping:
        write(state, st.serializer.forgePongFrame(msg.data));
        break;
    default:
        break;
    }
}


}




#endif /* SRC_USERVER_WEBSOCKETS_STREAM_H_ */
