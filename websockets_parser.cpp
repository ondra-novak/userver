#include <random>
#include "websockets_parser.h"
#include "websockets_stream.h"
#include "websockets_client.h"


namespace userver {



void WebSocketParser::discardFrame() {
	ftype = WSFrameType::incomplete;
}

std::string_view WebSocketParser::parse(const std::string_view& data) {
	if (data.empty()) return data;
	unsigned char lopc;
	std::size_t p = 0;
	do {
		unsigned char b = data[p];
		switch (currentState) {
		case opcodeFlags:
			fin = (b & 0x80) != 0;
			lopc = (b & 0xF);
			currentState = sizeMask;
			if (lopc != WebSocketsConstants::opcodeContFrame) {
				receivedData.clear();
				opcode = lopc;
			}

			ftype = WSFrameType::incomplete;

			mask[0] = mask[1] = mask[2] = mask[3] = 0;
			maskPos = 0;

			break;

		case sizeMask:
			masked = (b & 0x80) != 0;
			size = b & 0x7f;
			if (size == 126) {
				size = 0;
				currentState = sizeMulti;
				stateRemain = 2;
			} else if (size == 127) {
				size = 0;
				currentState = sizeMulti;
				stateRemain = 8;
			} else {
				afterSize();
			}
			break;
		case sizeMulti:
			size = (size << 8)+b;
			if (--stateRemain == 0) {
				afterSize();
			}
			break;
		case masking:
			mask[4-stateRemain] = b;
			if (--stateRemain == 0) {
				masked = false;
				afterSize();
			}
			break;
		case payload:
			receivedData.push_back(b ^ mask[maskPos]);
			maskPos = (maskPos + 1) & 0x3;
			if (--stateRemain == 0) {
				epilog();
			}
		}
		++p;
	} while (p < data.length() && ftype == WSFrameType::incomplete);
	return data.substr(p);
}

void WebSocketParser::afterSize() {
	if (masked) {
		currentState = masking;
		stateRemain = 4;
	}
	else if (size == 0) {
		epilog();
	} else {
		receivedData.reserve(receivedData.size()+ size);
		currentState = payload;
		stateRemain = size;
	}
}

void WebSocketParser::reset() {
	currentState = opcodeFlags;
	ftype = WSFrameType::init;
}

void WebSocketParser::epilog(){
	currentState = opcodeFlags;
	if (fin) {
		switch (opcode) {
		case WebSocketsConstants::opcodeBinaryFrame: ftype = WSFrameType::binary;break;
		case WebSocketsConstants::opcodeTextFrame: ftype = WSFrameType::text;break;
		case WebSocketsConstants::opcodePing: ftype = WSFrameType::ping;break;
		case WebSocketsConstants::opcodePong: ftype = WSFrameType::pong;break;
		case WebSocketsConstants::opcodeConnClose: ftype = WSFrameType::connClose;
			if (receivedData.size() < 2) closeCode = 0;
			else closeCode = (receivedData[0] << 8) + receivedData[1];
			break;
		default: return;
		}
	}
}


bool WebSocketParser::isComplete() const {
	return ftype != WSFrameType::incomplete && ftype != WSFrameType::init;
}

WSFrameType WebSocketParser::getFrameType() const {
	return ftype;
}

std::string_view WebSocketParser::getData() const {
	return std::string_view(receivedData.data(), receivedData.size());
}

std::string_view WebSocketParser::getText() const {
	return std::string_view(getData());
}

unsigned int WebSocketParser::getCode() const {
	return closeCode;
}

std::string_view WebSocketSerializer::forgeBinaryFrame(const std::string_view& data) {
	return forgeFrame(WebSocketsConstants::opcodeBinaryFrame, data);
}

std::string_view WebSocketSerializer::forgeTextFrame(const std::string_view& data) {
	return forgeFrame(WebSocketsConstants::opcodeTextFrame, std::string_view(data));
}

std::string_view WebSocketSerializer::forgePingFrame(const std::string_view& data) {
	return forgeFrame(WebSocketsConstants::opcodePing, data);
}

std::string_view WebSocketSerializer::forgePongFrame(const std::string_view& data) {
	return forgeFrame(WebSocketsConstants::opcodePong, data);
}

std::string_view WebSocketSerializer::forgeCloseFrame(unsigned int code) {
	unsigned char bc[2];
	bc[0] = code >> 8;
	bc[1] = code & 0xFF;
	return forgeFrame(WebSocketsConstants::opcodeConnClose, std::string_view(reinterpret_cast<char *>(bc),2));
}


std::string_view WebSocketSerializer::forgeFrame(int opcode, const std::string_view& data) {
	frameData.clear();
	frameData.push_back(((unsigned char)opcode & 0xF) | 0x80);
	unsigned char szcode;
	unsigned char szbytes;
	if (data.length()<126) {szcode = (unsigned char)data.length();szbytes = 0;}
	else if (data.length()<65536) {szcode = 126;szbytes = 2;}
	else {szcode = 127;szbytes = 8;}
	if (masking) szcode |=0x80;
	frameData.push_back(szcode);
	for (unsigned char i = szbytes; i > 0; ) {
		--i;
		frameData.push_back((data.length()>>(i*8)) & 0xFF);
	}
	unsigned char mask_bytes[4];
	if (masking) {
		for (unsigned char i = 0; i < 4; ++i) {
			mask_bytes[i] = (unsigned char)rnd() & 0xFF;
			frameData.push_back(mask_bytes[i]);
		}
	} else {
		for (unsigned char i = 0; i < 4; ++i) mask_bytes[i] = 0;
	}

	for (std::size_t i = 0; i < data.length(); ++i) {
		frameData.push_back(mask_bytes[i & 0x3] ^ data[i]);
	}


	return std::string_view(frameData.data(), frameData.size());
}

WebSocketSerializer::WebSocketSerializer(bool client):masking(client) {
	if (client) {
		std::random_device rdev;
		rnd.seed(rdev());
	}
}

}
