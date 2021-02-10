/*
 * udpsocket.cpp
 *
 *  Created on: 19. 1. 2021
 *      Author: ondra
 */


#include "platform.h"
#include "netaddr.h"
#include "async_resource.h"
#include "dgramsocket.h"

#include <system_error>
namespace userver {

DGramSocket::DGramSocket(int i):s(i) {
	inputBuffer.resize(4096);
	addrBuffer.resize(sizeof (sockaddr_storage));
}


DGramSocket::DGramSocket(const NetAddr &addr):DGramSocket(addr.bindUDP()) {}

bool DGramSocket::recv(int timeout) {
	sockaddr *sin = reinterpret_cast<sockaddr *>(addrBuffer.data());
	socklen_t slen = static_cast<socklen_t>(addrBuffer.size());
	int r = ::recvfrom(s, inputBuffer.data(), inputBuffer.size(), MSG_DONTWAIT|MSG_TRUNC, sin, &slen);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			pollfd pfd;
			pfd.fd = s;
			pfd.events = POLLIN;
			pfd.revents = 0;
			r = poll(&pfd, 1, timeout);
			if (r < 0) {
				if (err == EINTR) return recv(timeout);
				throw std::system_error(errno, std::generic_category());
			} else if (r == 0) {
				return false;
			} else {
				return recv(timeout);
			}
		} else {
			throw std::system_error(errno, std::generic_category());
		}
	}
	if (r > static_cast<int>(inputBuffer.size())) {
		rcvsize = 0;
		inputBuffer.resize(r);  //discard the packet and resize it
		return true;
	}
	rcvsize = r;
	return true;
}

std::string_view DGramSocket::getData() const {
	return std::string_view(inputBuffer.data(), rcvsize);
}

NetAddr DGramSocket::getPeerAddr() const {
	const sockaddr *sin = reinterpret_cast<const sockaddr *>(addrBuffer.data());
	return NetAddr::fromSockAddr(*sin);

}

void DGramSocket::send(const std::string_view &data, const NetAddr &target) {
	const sockaddr *sin = target.getAddr();
	socklen_t slen = target.getAddrLen();
	int r = ::sendto(s, data.data(), data.size(), 0, sin, slen);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			pollfd pfd;
			pfd.fd = s;
			pfd.events = POLLOUT;
			pfd.revents = 0;
			r = poll(&pfd, 1, -1);
			if (r < 0) {
				if (err == EINTR) return send(data, target);
				throw std::system_error(errno, std::generic_category());
			} else {
				return send(data,target);
			}

		}
	}

	if (r != static_cast<int>(data.size())) {
		throw std::runtime_error("DGramSocket::send() - Packet truncated");
	}
}

DGramSocket::DGramSocket(DGramSocket &&other)
:s(other.s)
,inputBuffer(std::move(other.inputBuffer))
,addrBuffer(std::move(other.addrBuffer))
,rcvsize(other.rcvsize)
{
	other.s = -1;
}

DGramSocket::~DGramSocket() {
	if (s != -1) ::close(s);
}

DGramSocket& DGramSocket::operator =(DGramSocket &&other) {
	s = other.s;
	inputBuffer = std::move(other.inputBuffer);
	addrBuffer = std::move(other.addrBuffer);
	rcvsize = other.rcvsize;
	other.s = -1;
	return *this;

}

const AsyncResource &DGramSocket::getReadAsync()  {
	AsyncResource *a = new(static_cast<void *>(inputBuffer.data())) AsyncResource(AsyncResource::read, s);
	return *a;
}

}

