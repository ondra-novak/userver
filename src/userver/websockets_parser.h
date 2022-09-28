#pragma once

#include <functional>
#include <random>

#include <string_view>
#include <vector>


namespace userver {


enum class WSFrameType {
    ///Unkown frame - this state is never returned, however it used to initialize variables
    unknown = 0,
    ///frame is not complete yet
    /** If incomplete received through recv(), it can mean, that remote stream
     * was closed or an error has been reported. You need to check stream
     * state, what happened.
     *
     * If the stream is timeouted, you can retry recv() to wait to rest of the
     * frame.
     */
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
    init,
    ///not actual message, only informs, that reading message timeouted
    timeout,
};



///Some constants defined for websockets
class WebSocketsConstants {
public:
	static const unsigned int opcodeContFrame = 0;
	static const unsigned int opcodeTextFrame = 1;
	static const unsigned int opcodeBinaryFrame = 2;
	static const unsigned int opcodeConnClose = 8;
	static const unsigned int opcodePing = 9;
	static const unsigned int opcodePong = 10;

	///Connection has been reset by peer
	/** This status is only returned by WSStream in case that connection has been closed */
	static const unsigned int closeConnReset = 1;
	///Connection has been timeouted, remote side did not respond to ping message
	/** This status is only returned by WSStream
	 *
	 * When underlying stream timeouts, the ping frame is send to other side. It is
	 * expected that pong frame (or other data) arrives to the next timeout. If nothing
	 * of this happens, connection is marked as closed with closeConnTimeout as reason
	 */
	static const unsigned int closeConnTimeout = 2;
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

struct WSMessage {
    ///frame type
    WSFrameType type;
    ///frame data
    std::string_view data;
    ///code associated with the frame
    /** It is applied only for certain types of frame, otherwise it is zero */
    unsigned int code;
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
