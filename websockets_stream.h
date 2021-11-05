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

class WSStream: public WebSocketsConstants {
public:

	WSStream(Stream &&stream, bool client)
		:s(std::move(stream))
		,serializer(client) {}

	WSFrameType read();

	template<typename Fn>
	void readAsync(Fn &&callback) {
		s.readAsync([this, callback = std::forward<Fn>(callback)](std::string_view data){
			if (data.empty()) callback(WSFrameType::incomplete);
			else {
				auto out = parser.parse(data);
				s.putBack(out);
				if (parser.isComplete()) {
					callback(parser.getFrameType());
				} else {
					readAsync(std::forward<Fn>(callback));
				}
			}
		});
	}

	void discardFrame() {parser.discardFrame();}
	WSFrameType getFrameType() const {return parser.getFrameType();}
	std::string_view getData() const {return parser.getData();}
	std::string_view getText() const {return parser.getText();}
	unsigned int getCode() const {return parser.getCode();}

	void writeNB(WSFrameType frameType, const std::string_view &data) {
		std::string_view out;
		switch (frameType) {
		case WSFrameType::binary: out = serializer.forgeBinaryFrame(data);break;
		case WSFrameType::text: out = serializer.forgeTextFrame(data);break;
		case WSFrameType::ping: out = serializer.forgePingFrame(data);break;
		case WSFrameType::pong: out = serializer.forgePongFrame(data);break;
		default: return;
		}
		s.writeNB(out);
	}
	void writeCloseNB(unsigned int code = WebSocketsConstants::closeNormal) {
		s.writeNB(serializer.forgeCloseFrame(code));

	}
	void flush() {
		s.flush();
	}
	template<typename Fn>
	void flushAsync(Fn &&fn) {
		s.flushAsync(std::forward<Fn>(fn));
	}
	void write(WSFrameType frameType, const std::string_view &data) {
		writeNB(frameType, data);
		flush();
	}
	void writeClose(WSFrameType frameType, unsigned int code = WebSocketsConstants::closeNormal) {
		writeCloseNB(code);
		flush();
	}

protected:

	Stream s;
	WebSocketParser parser;
	WebSocketSerializer serializer;
};


}




#endif /* SRC_USERVER_WEBSOCKETS_STREAM_H_ */
