#pragma once

#include <functional>
#include <random>

#include <string_view>
#include <vector>


namespace userver {



///Some constants defined for websockets
class WebSocketsConstants {
public:
	static const unsigned int opcodeContFrame = 0;
	static const unsigned int opcodeTextFrame = 1;
	static const unsigned int opcodeBinaryFrame = 2;
	static const unsigned int opcodeConnClose = 8;
	static const unsigned int opcodePing = 9;
	static const unsigned int opcodePong = 10;

	static const unsigned int closeNormal = 1000;
	static const unsigned int closeGoingAway = 1001;
	static const unsigned int closeProtocolError = 1002;
	static const unsigned int closeUnsupportedData = 1003;
	static const unsigned int closeNoStatus = 1005;
	static const unsigned int closeAbnormal = 1006;
	static const unsigned int closeInvalidPayload = 1007;
	static const unsigned int closePolicyViolation = 1008;
	static const unsigned int closeMessageTooBig = 1009;
	static const unsigned int closeMandatoryExtension = 1010;
	static const unsigned int closeInternalServerError = 1011;
	static const unsigned int closeTLSHandshake = 1015;

};

enum class WSFrameType {
	///frame is not complete yet
	incomplete,
	///text frame
	text,
	///binary frame
	binary,
	///connection close frame
	connClose,
	///ping frame
	ping,
	///pong frame
	pong,
	///Object is initial state
	/**
	 * No data has been retrieved yet
	 *
	 * This frame has no data
	 */
	init
};


class WebSocketParser {
public:


	///parse received block
	/**
	 * @param data read from the stream
	 * @return unused data
	 *
	 * @note If incomplete data are received, you should
	 * call it again with additional data. Test throught function isCoplette();
	 */
	std::string_view parse(const std::string_view &data);


	///Returns true, when previous parse() finished whole frame and the frame is ready to collect
	bool isComplete() const;

	///Discards current frame.
	/** this causes, that function isComplete starts to return false */
	void discardFrame();

	WSFrameType getFrameType() const;

	///Retrieve data as binary view
	std::string_view getData() const;

	///Retrieve data as text view
	std::string_view getText() const;

	///Get code (for opcodeConnClose)
	unsigned int getCode() const;

	///Resets the state
	void reset();
public:

	enum State {
		opcodeFlags,
		sizeMask,
		sizeMulti,
		masking,
		payload
	};

	State currentState = opcodeFlags;
	std::size_t stateRemain;

	std::size_t size;
	WSFrameType ftype = WSFrameType::init;
	unsigned int closeCode;
	unsigned char opcode;
	unsigned char mask[4];
	unsigned char maskPos;
	bool masked;
	bool fin;

	std::vector<char> receivedData;

	void afterSize();
	void epilog();




};

class WebSocketSerializer {
public:


	explicit WebSocketSerializer(bool client);


	std::string_view forgeBinaryFrame(const std::string_view &data);
	std::string_view forgeTextFrame(const std::string_view &data);
	std::string_view forgePingFrame(const std::string_view &data);
	std::string_view forgePongFrame(const std::string_view &data);
	std::string_view forgeCloseFrame(unsigned int code = WebSocketsConstants::closeNormal);
protected:

	std::string_view forgeFrame(int opcode, const std::string_view &data);

	std::default_random_engine rnd;
	bool masking;

	std::vector<char> frameData;


};


}
