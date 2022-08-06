/*
 * websocket_server_handler.h
 *
 *  Created on: 6. 8. 2022
 *      Author: ondra
 */

#ifndef _SRC_USERVER_WEBSOCKETS_SERVER_HANDLER_H_32093djwoe2839edw
#define _SRC_USERVER_WEBSOCKETS_SERVER_HANDLER_H_32093djwoe2839edw

#include "websockets_stream.h"
#include "sha1.h"
#include "http_server.h"
#include "callback.h"
#include "base64.h"

namespace userver {


///Handler which creates websocket connections
/**
 * You can install the handler to the server through addPath
 *
 * At given path websocket connections will be accepted. Non-websocket connections are
 * passed to parent handler (handler of parent path), so they can be handled as well as
 * ordinary request.
 *
 * You need to specify a callback function, which is called for every new connection.
 * You can also specify check callback which can check the request's headers and
 * reject every request which doesn't meet an expectation
 */
class WebsocketServerHandler {
public:
    using CheckCB = Callback<bool(PHttpServerRequest &, const std::string_view &)>;
    using ConnectCB = Callback<void(WSStream &)>;


    ///Construct handler
    /**
     * @param cb callback function called for newly created websocket connection
     */
    explicit WebsocketServerHandler(ConnectCB &&cb)
        :_check(nullptr)
        ,_connect(std::move(cb)) {}

    ///Construct handler
    /**
     * @param check callback function called for every request. The callback function
     * has the same format as HTTP handler, so it also returns bool value, where
     * true is accept and false is reject the request. Note that function must also
     * generate a valid http response if rejects the request. Function doesn't need
     * to check websockets specific headers, because they are always checked.
     * @param cb callback function called for newly created websocket connection

     */
    WebsocketServerHandler(CheckCB &&check, ConnectCB &&cb)
        :_check(std::move(check))
        ,_connect(std::move(cb)) {}


    ///Function called for http request
    bool operator()(PHttpServerRequest &req, const std::string_view &vpath) {

        if (!req->allowMethods({"GET"})) return true;

        if (HeaderValue::iequal(req->get("Upgrade"),"websocket")
            && HeaderValue::iequal(req->get("Connection"),"upgrade")) {

            std::string_view key = req->get("Sec-WebSocket-Key");
            if (!key.empty()) {

                if (_check != nullptr) {
                    if (!_check(req, vpath)) {
                        return true;
                    }
                }

                SHA1 sha1;
                sha1.update(key);
                sha1.update("258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
                std::string digest = sha1.final();
                std::string digestResult;
                digestResult.reserve((digest.size()*4 + 3)/3);
                base64encode(digest, [&](char c){digestResult.push_back(c);});

                req->setStatus(101);
                req->set("Upgrade","websocket");
                req->set("Connection","Upgrade");
                req->set("Sec-WebSocket-Accept",digestResult);
                Stream s = req->send();

                WSStream wss(std::move(s), false);
                _connect(wss);

                return true;
            }

        }

        return false;
    }


protected:
    CheckCB _check;
    ConnectCB _connect;

};


}



#endif /* _SRC_USERVER_WEBSOCKETS_SERVER_HANDLER_H_32093djwoe2839edw */
