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


inline std::unique_ptr<WSStream> wsConnect(HttpClient &httpclient, const HttpClient::URL &url, int *code_out = nullptr) {

	if (code_out) *code_out = 0;
	std::unique_ptr<HttpClientRequest> req = httpclient.open("GET", url);
	if (req == nullptr) return nullptr;
	req->addHeader("Connection", "upgrade");
	req->addHeader("Upgrade","websocket");
	req->addHeader("Sec-WebSocket-Version","13");
	req->addHeader("Sec-WebSocket-Key","dGhlIHNhbXBsZSBub25jZQ==");
	int code = req->send();
	if (code_out) *code_out = code;
	if (code != 101) return nullptr;
	if (req->get("Upgrade") != "websocket") {
		if (code_out) *code_out = -1;
		return nullptr;
	}
	return std::make_unique<WSStream>(std::move(req->getStream()), true);
}

template<typename Fn>
void wsConnectAsync(HttpClient &httpclient, const HttpClient::URL &url, Fn &&callback) {
	httpclient.open("GET", url, [callback = std::forward<Fn>(callback)](std::unique_ptr<HttpClientRequest> &&req) mutable {
		if (req == nullptr) {
			callback(0, nullptr);
		} else {
			req->addHeader("Connection", "upgrade");
			req->addHeader("Upgrade","websocket");
			req->addHeader("Sec-WebSocket-Version","13");
			req->addHeader("Sec-WebSocket-Key","dGhlIHNhbXBsZSBub25jZQ==");
			auto r = req.get();
			r->sendAsync([callback = std::forward<Fn>(callback), req = std::move(req)](int status) mutable {
				if (status != 101) callback(status, nullptr);
				if (req->get("Upgrade") != "websocket") callback(-1,nullptr);
				callback(status, std::make_unique<WSStream>(std::move(req->getStream()), true));
			});
		}
	});
}



}


#endif /* SRC_USERVER_WEBSOCKETS_CLIENT_H_ */
