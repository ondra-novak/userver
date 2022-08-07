/*
 * websockets_client.h
 *
 *  Created on: 5. 11. 2021
 *      Author: ondra
 */


#ifndef SRC_USERVER_WEBSOCKETS_CLIENT_H_
#define SRC_USERVER_WEBSOCKETS_CLIENT_H_
#include <userver/base64.h>
#include <userver/http_exception.h>
#include "http_client.h"
#include "websockets_stream.h"
#include "sha1.h"

namespace userver {

///WebSocket client
/**
 * Purpose of this class is to create WSStream by either synchronously or asynchronously.
 * Once the WSStream is created, the instance of this class has no longer use.
 *
 * @code
 * WebSocketClient(httpc, url) >> [](std::optional<WSStream> &&stream) {
 *      if (stream.has_value()) {
 *          //use stream here
 *      } else {
 *          auto exp = std::current_exception();
 *          //handle exception
 *      }
 * };
 * @endcode
 */
class WebSocketClient {
public:
    using Headers = std::vector<std::pair<std::string, std::string> >;

    WebSocketClient(HttpClient &httpclient, const std::string_view &url, Headers &&headers = Headers())
        :_httpclient(httpclient), _url(url), _headers(std::move(headers)) {}
    WebSocketClient(const WebSocketClient &other) = default;
    WebSocketClient &operator=(const WebSocketClient &other) = delete;

    operator WSStream() const {
        std::unique_ptr<HttpClientRequest> req = _httpclient.open("GET", _url);
        if (req == nullptr) throw HttpStatusCodeException(-1,"Failed to connect");
        std::string digest = setup_headers(req);
        for (const auto &h: _headers) {
            req->addHeader(h.first, h.second);
        }
        int code = req->send();
        if (code != 101) {
            throw HttpStatusCodeException(code,req->getStatusMessage());
        }
        if (HeaderValue::iequal(req->get("Upgrade") ,"websocket")
            && req->get("Sec-WebSocket-Accept") == digest) {
            return WSStream(std::move(req->getStream()), true);
        } else {
            throw HttpStatusCodeException(-2, "Invalid WebSocket handshake");
        }

    }

    operator SharedWSStream() const {
        WSStream s(*this);
        return s.make_shared();
    }


    template<typename Fn>
    void operator>>(Fn &&cb) {
        _httpclient.open("GET", _url) >> [cb = std::forward<Fn>(cb), headers = std::move(_headers)]
                    (std::unique_ptr<HttpClientRequest> &&req) mutable {
            try {
                if (req == nullptr) throw HttpStatusCodeException(-1,"Failed to connect");
                std::string digest = setup_headers(req);
                for (const auto &h: headers) {
                    req->addHeader(h.first, h.second);
                }
                req->send() >> [=, cb = std::forward<Fn>(cb),
                                req = std::move(req)](int status) mutable {

                    try {
                        if (status != 101) {
                            throw HttpStatusCodeException(req->getStatus(),req->getStatusMessage());
                        }
                        if (HeaderValue::iequal(req->get("Upgrade") ,"websocket")
                                    && req->get("Sec-WebSocket-Accept") == digest) {
                                    cb(WSStream(std::move(req->getStream()), true));
                        } else {
                            throw HttpStatusCodeException(-2, "Invalid WebSocket handshake");
                        }
                    } catch (...) {
                        cb({});
                    }
                };


            } catch (...) {
                cb({});
            }
        };
    }



protected:
    HttpClient &_httpclient;
    std::string _url;
    Headers _headers;

    static std::string setup_headers(std::unique_ptr<HttpClientRequest> &req) {
        std::random_device rnd;
        std::uniform_int_distribution<char> dist(
            std::numeric_limits<char>::min(),
            std::numeric_limits<char>::max()
        );
        std::string rndkey;
        for (int i = 0; i < 16; i++) {
            rndkey.push_back(dist(rnd));
        }
        std::string b64key;
        base64encode(rndkey, [&](char c){b64key.push_back(c);});
        SHA1 sha1;
        sha1.update(b64key);
        sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        std::string digest=sha1.final();
        std::string b64digest;
        base64encode(digest, [&](char c){b64digest.push_back(c);});

        req->addHeader("Connection", "Upgrade");
        req->addHeader("Upgrade","websocket");
        req->addHeader("Sec-WebSocket-Version","13");
        req->addHeader("Sec-WebSocket-Key",b64key);

        return b64digest;
    }

};

}


#endif /* SRC_USERVER_WEBSOCKETS_CLIENT_H_ */
