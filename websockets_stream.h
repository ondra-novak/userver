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

	struct State {
		std::recursive_mutex stlock;
		CallbackT<void(const std::string_view &data)> readcb;
		CallbackT<void(bool)> flushcb;
		bool closed = false;
		std::string wrqueue;
		bool pending() const {return flushcb != nullptr;}
	};

	using PState = std::shared_ptr<State>;

public:

	WSStream(Stream &&stream, bool client)
		:s(std::move(stream))
		,serializer(client)
		,state(std::make_shared<State>())
	{}

	~WSStream() {
		CallbackT<void(const std::string_view &data)> readcb;
		CallbackT<void(bool)> flushcb;
		{
			std::lock_guard _(state->stlock);
			readcb=std::move(state->readcb);
			flushcb=std::move(state->flushcb);
			state->closed = true;
		}

		if (readcb != nullptr) readcb(std::string_view());
		if (flushcb != nullptr) flushcb(false);
	}

	///Read frame synchronously
	/**
	 * @return Returns received frame type. If the frame cannot be read, because connection has been
	 * reset or timeout, the function returns WSFrameType::incomplete
	 */
	WSFrameType recv() {
		std::string_view data = s.read();
		if (data.empty()) return WSFrameType::incomplete;
		std::string_view extra = parser.parse(data);
		while (!parser.isComplete()) {
			s.putBack(extra);
			data = s.read();
			if (data.empty()) return WSFrameType::incomplete;
			extra = parser.parse(data);
		}
		s.putBack(extra);
		return parser.getFrameType();

	}

	///Read frame asynchronously
	/**
	 * @param callback function which receives received frame type (similar to synchronous function).
	 *
	 * If the frame cannot be read, because timeout or connection closed, function receives WSFrameType::incomplete
	 */
	template<typename Fn>
	void recvAsync(Fn &&callback) {
		this->state->readcb = [this,callback = std::forward<Fn>(callback)](std::string_view data) mutable {
			if (data.empty()) callback(WSFrameType::incomplete);
			else {
				auto out = parser.parse(data);
				s.putBack(out);
				if (parser.isComplete()) {
					auto type = parser.getFrameType();
					callback(type);
					if (type == WSFrameType::connClose) {
						state->closed = true;
					}
				} else {
					recvAsync(std::forward<Fn>(callback));
				}
			}
		};
		s.readAsync([state = this->state](std::string_view data){
			CallbackT<void(const std::string_view &data)> readcb;
			{
				std::lock_guard _(state->stlock);
				readcb=std::move(state->readcb);
			}
			if (readcb != nullptr) {
				readcb(data);
			}
		});
	}

	void discardFrame() {parser.discardFrame();}
	WSFrameType getFrameType() const {return parser.getFrameType();}
	std::string_view getData() const {return parser.getData();}
	std::string_view getText() const {return parser.getText();}
	unsigned int getCode() const {return parser.getCode();}


	///Sends frame
	/**
	 * @param frameType type of frame
	 * @param data data of the frame
	 *
	 * @note data are sent asynchronously. There is no blocking
	 */
	void send(WSFrameType frameType, const std::string_view &data) {
		std::lock_guard _(state->stlock);

		std::string_view out;
		switch (frameType) {
		case WSFrameType::binary: out = serializer.forgeBinaryFrame(data);break;
		case WSFrameType::text: out = serializer.forgeTextFrame(data);break;
		case WSFrameType::ping: out = serializer.forgePingFrame(data);break;
		case WSFrameType::pong: out = serializer.forgePongFrame(data);break;
		default: return;
		}

		if (state->pending()) {
			state->wrqueue.append(out);
		} else {
			write(out);
		}
	}

	///Sends close
	/**
	 * @param code closing code
	 */
	void close(unsigned int code = WebSocketsConstants::closeNormal) {
		std::lock_guard _(state->stlock);
		auto out = serializer.forgeCloseFrame(code);
		if (state->pending()) {
			state->wrqueue.append(out);
		} else {
			write(out);
		}
	}


	///Returns true, if reading timeouted
	/** this allows to distinguish between closed connection and timeout.
	 * You can reset timeout by calling clearTimeout();
	 */
	bool timeouted() const {
		return s.timeouted();
	}

	///Clears timeout state, after which read request can be repeated
	void clearTimeout() {
		s.clearTimeout();
	}


protected:

	Stream s;
	WebSocketParser parser;
	WebSocketSerializer serializer;
	PState state;

	void write(std::string_view data) {
		if (state->closed) return;
		s.writeNB(data);
		state->flushcb = [this](bool ok){
			if (!ok) {
				state->closed = true;
			} else if (!state->wrqueue.empty()) {
				std::string tmp (std::move(state->wrqueue));
				write(tmp);
			}
		};
		s.flushAsync([state = this->state](bool ok){
			std::lock_guard _(state->stlock);
			CallbackT<void(bool)> flushcb;
			flushcb=std::move(state->flushcb);
			if (flushcb != nullptr) flushcb(ok);
		});
	}


};


}




#endif /* SRC_USERVER_WEBSOCKETS_STREAM_H_ */
