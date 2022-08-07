/*
 * websockets_client.h
 *
 *  Created on: 5. 11. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_WEBSOCKETS_CLIENT_H_
#define SRC_USERVER_WEBSOCKETS_CLIENT_H_
#include "http_client.h"
#include "websockets_stream.h"

namespace userver {



inline std::optional<WSStream> wsConnect(HttpClient &httpclient, const HttpClient::URL &url, int *code_out = nullptr) {

	if (code_out) *code_out = 0;
	std::unique_ptr<HttpClientRequest> req = httpclient.open("GET", url);
	if (req == nullptr) return {};
	req->addHeader("Connection", "upgrade");
	req->addHeader("Upgrade","websocket");
	req->addHeader("Sec-WebSocket-Version","13");
	req->addHeader("Sec-WebSocket-Key","dGhlIHNhbXBsZSBub25jZQ==");
	int code = req->send();
	if (code_out) *code_out = code;
	if (code != 101) return {};
	if (req->get("Upgrade") != "websocket") {
		if (code_out) *code_out = -1;
		return {};
	}
	return WSStream(std::move(req->getStream()), true);
}

template<typename Fn>
void wsConnectAsync(HttpClient &httpclient, const HttpClient::URL &url, Fn &&callback) {
	httpclient.open("GET", url) >> [callback = std::forward<Fn>(callback)](std::unique_ptr<HttpClientRequest> &&req) mutable {
		if (req == nullptr) {
		    std::optional<WSStream> ws;
			callback(0, ws);
		} else {
			req->addHeader("Connection", "upgrade");
			req->addHeader("Upgrade","websocket");
			req->addHeader("Sec-WebSocket-Version","13");
			req->addHeader("Sec-WebSocket-Key","dGhlIHNhbXBsZSBub25jZQ==");
			auto r = req.get();
			r->send() >> [callback = std::forward<Fn>(callback), req = std::move(req)](int status) mutable {
			    std::optional<WSStream> ws;
				if (status != 101) {
					callback(status, ws);
				} else if (req->get("Upgrade") != "websocket") {
					callback(-1,ws);
				} else {
				    ws.emplace(WSStream(std::move(req->getStream()), true));
					callback(status, ws);
				}

			};
		}
	};
}

///Simple to use websocket client based on WSStream
/**
 * To use this object, you need to create HttpClient and link the reference to httpclient
 * in constructor. The instance must be valid before connect() is called, and must remain
 * valid if the futher reconnects can happen
 *
 * The client must be also initialized by one callback functions. see onMessage
 *
 * When connection is established, the message callback is called with event WSFrameType::init
 * allows to handle perform an initial setup.
 *
 * If the connection stalls or disconnected, WSFrameType::incomplete is received. If connection
 * is closed by peer, connClose event is reported,
 *
 * The client itself handles ping-pong messages and a valid response to connClose, so you don't
 * need to bother.
 *
 * The client can be reused after connection is disconnected by disconnect().
 */
class WebSocketClient {
public:

    using Message = WSStream::Message;

    using MsgCallback = Callback<void(const Message &msg)>;

    WebSocketClient(HttpClient &httpc):httpc(httpc) {}


    ~WebSocketClient() {
        disconnect();
    }

    void connect(const std::string_view &url);
    void disconnect();

    bool send_text(const std::string_view &data);
    bool send_binary(const std::string_view &data);
    bool send_close(unsigned int code);
    void on_message(MsgCallback &&cb);


    WebSocketClient(const WebSocketClient &) = delete;
    WebSocketClient &operator=(const WebSocketClient &) = delete;

protected:
    HttpClient &httpc;
    std::string url;
    std::recursive_mutex mx;
    std::optional<WSStream> ws;
    MsgCallback msgcb;

    PendingOp pending_connect;

};

inline void WebSocketClient::connect(const std::string_view &url) {
    std::unique_lock _(mx);
    pending_connect.cancel_clear(_);
    ws.reset();
    pending_connect.init();
    wsConnectAsync(httpc, url, [=, pc = pending_connect](int status, std::optional<WSStream> &stream ) mutable {
        pc >> [&] {
            if (stream.has_value()) {
                std::lock_guard _(mx);
                ws.emplace(std::move(*stream));
                pending_connect.clear();
                if (msgcb != nullptr) msgcb({WSFrameType::init});
                ws->recv() >> MsgCallback([=](MsgCallback &me, const Message &msg){
                   if (msgcb != nullptr) msgcb(msg);
                   ws->recv() >> std::move(me);
                });
            } else {
                std::lock_guard _(mx);
                pending_connect.clear();
                if (msgcb != nullptr) msgcb(Message{WSFrameType::connClose,"",static_cast<unsigned int>(status)});
            }
        };
    });
}

inline void WebSocketClient::disconnect() {
    std::unique_lock _(mx);
    pending_connect.cancel_clear(_);
    ws.reset();
}

inline bool WebSocketClient::send_text(const std::string_view &data) {
    std::unique_lock _(mx);
    if (!ws.has_value()) return false;
    return ws->send_text(data);

}

inline bool WebSocketClient::send_binary(const std::string_view &data) {
    std::unique_lock _(mx);
    if (!ws.has_value()) return false;
    return ws->send_binary(data);
}

inline bool WebSocketClient::send_close(unsigned int code) {
    std::unique_lock _(mx);
    if (!ws.has_value()) return false;
    return ws->send_close(code);
}

inline void WebSocketClient::on_message(MsgCallback &&cb) {
    msgcb = std::move(cb);
}



}


#endif /* SRC_USERVER_WEBSOCKETS_CLIENT_H_ */
