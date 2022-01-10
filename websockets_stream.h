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
		Stream s;
		WebSocketParser parser;
		WebSocketSerializer serializer;

		State(Stream &&s, bool client)
			:s(std::move(s))
			,serializer(client)
		{}
		~State() {
			CallbackT<void(const std::string_view &data)> readcb;
			CallbackT<void(bool)> flushcb;
			{
				std::lock_guard _(stlock);
				readcb=std::move(readcb);
				flushcb=std::move(flushcb);
				closed = true;
			}

			if (readcb != nullptr) readcb(std::string_view());
			if (flushcb != nullptr) flushcb(false);
		}
	};

	using PState = std::shared_ptr<State>;

public:

	WSStream(Stream &&stream, bool client)
		:state(std::make_shared<State>(std::move(stream), client))
	{}

	~WSStream() {
	}

	class RecvHelper {
	public:
		RecvHelper(WSStream &owner):owner(owner) {};
		RecvHelper(const RecvHelper &other) = delete;
		operator WSFrameType()  {
			if (type == WSFrameType::unknown) type =owner.recvSync();
			return type;
		}
		~RecvHelper() noexcept(false) {
			if (type == WSFrameType::unknown) owner.recvSync();
		}

		template<typename Fn>
		void operator >> (Fn &&fn) {
			owner.recvAsync(owner.state, std::forward<Fn>(fn));
			type = WSFrameType::incomplete;
		}
	protected:
		WSStream &owner;
		WSFrameType type = WSFrameType::unknown;



	};

	RecvHelper recv() {
		return RecvHelper(*this);
	}

	///Read frame synchronously
	/**
	 * @return Returns received frame type. If the frame cannot be read, because connection has been
	 * reset or timeout, the function returns WSFrameType::incomplete
	 */
	WSFrameType recvSync() {
		auto &st = *state;
		std::string_view data = st.s.read();
		if (data.empty()) return WSFrameType::incomplete;
		std::string_view extra = st.parser.parse(data);
		while (!st.parser.isComplete()) {
			st.s.putBack(extra);
			data = st.s.read();
			if (data.empty()) return WSFrameType::incomplete;
			extra = st.parser.parse(data);
		}
		st.s.putBack(extra);
		return st.parser.getFrameType();

	}

	///Read frame asynchronously
	/**
	 * @param callback function which receives received frame type (similar to synchronous function).
	 *
	 * If the frame cannot be read, because timeout or connection closed, function receives WSFrameType::incomplete
	 */
/*	template<typename Fn>
	void recvAsync(Fn &&callback) {
		recvAsync(state, std::forward<Fn>(callback));
	}*/

	template<typename Fn>
	static void recvAsync(std::shared_ptr<State> state, Fn &&callback) {
		state->readcb = [state,callback = std::forward<Fn>(callback)](std::string_view data) mutable {
			auto &st = *state;
			if (data.empty()) callback(WSFrameType::incomplete);
			else {
				auto out = state->parser.parse(data);
				state->s.putBack(out);
				if (st.parser.isComplete()) {
					auto type = st.parser.getFrameType();
					callback(type);
					if (type == WSFrameType::connClose) {
						state->closed = true;
					}
				} else {
					recvAsync(state, std::forward<Fn>(callback));
				}
			}
		};
		state->s.read() >> [state](std::string_view data){
			CallbackT<void(const std::string_view &data)> readcb;
			{
				std::lock_guard _(state->stlock);
				readcb=std::move(state->readcb);
			}
			if (readcb != nullptr) {
				readcb(data);
			}
		};
	}

	void discardFrame() {state->parser.discardFrame();}
	WSFrameType getFrameType() const {return state->parser.getFrameType();}
	std::string_view getData() const {return state->parser.getData();}
	std::string_view getText() const {return state->parser.getText();}
	unsigned int getCode() const {return state->parser.getCode();}


	///Sends frame
	/**
	 * @param frameType type of frame
	 * @param data data of the frame
	 *
	 * @note data are sent asynchronously. There is no blocking
	 */
	void send(WSFrameType frameType, const std::string_view &data) {
		auto &st = *state;
		std::lock_guard _(st.stlock);

		std::string_view out;
		switch (frameType) {
		case WSFrameType::binary: out = st.serializer.forgeBinaryFrame(data);break;
		case WSFrameType::text: out = st.serializer.forgeTextFrame(data);break;
		case WSFrameType::ping: out = st.serializer.forgePingFrame(data);break;
		case WSFrameType::pong: out = st.serializer.forgePongFrame(data);break;
		default: return;
		}

		if (state->pending()) {
			state->wrqueue.append(out);
		} else {
			write(state, out);
		}
	}

	///Sends close
	/**
	 * @param code closing code
	 */
	void close(unsigned int code = WebSocketsConstants::closeNormal) {
		auto &st = *state;
		std::lock_guard _(state->stlock);
		auto out = st.serializer.forgeCloseFrame(code);
		if (state->pending()) {
			state->wrqueue.append(out);
		} else {
			write(state, out);
		}
	}


	///Returns true, if reading timeouted
	/** this allows to distinguish between closed connection and timeout.
	 * You can reset timeout by calling clearTimeout();
	 */
	bool timeouted() const {
		return state->s.timeouted();
	}

	///Clears timeout state, after which read request can be repeated
	void clearTimeout() {
		state->s.clearTimeout();
	}


protected:

	PState state;

	static void write(std::shared_ptr<State> state, std::string_view data) {
		auto &st = *state;
		if (st.closed) return;
		st.s.writeNB(data);
		st.flushcb = [state](bool ok){
			auto &st = *state;
			if (!ok) {
				st.closed = true;
			} else if (!st.wrqueue.empty()) {
				std::string tmp (std::move(st.wrqueue));
				write(state, tmp);
			}
		};
		st.s.flush() >> [state](bool ok){
			std::lock_guard _(state->stlock);
			CallbackT<void(bool)> flushcb;
			flushcb=std::move(state->flushcb);
			if (flushcb != nullptr) flushcb(ok);
		};
	}


};


}




#endif /* SRC_USERVER_WEBSOCKETS_STREAM_H_ */
