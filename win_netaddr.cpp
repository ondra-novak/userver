/*
 * netaddr.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include "platform.h"
#include <iomanip>
#include <sstream>
#include <ws2ipdef.h>
#include <ws2tcpip.h>
#include "netaddr.h"
#include "win_category.h"

namespace userver {

	std::string INetAddr::unknownToString(const sockaddr* sockaddr,
		std::size_t slen) {

		std::ostringstream bld;
		bld << "?" << std::hex << std::setfill('0') << std::setw(2);
		const unsigned char* b = reinterpret_cast<const unsigned char*>(sockaddr);
		for (std::size_t i = 0; i < slen; i++) bld << b[i];
		bld << "?";
		return bld.str();

	}



	class NetAddrIPv4 : public NetAddrBase<sockaddr_in> {
	public:
		using NetAddrBase<sockaddr_in>::NetAddrBase;

		virtual std::string toString(bool resolve = false) const override;
		virtual SocketHandle listen() const override;
		virtual SocketHandle connect() const override;
		virtual SocketHandle bindUDP() const override;
		virtual std::unique_ptr<INetAddr> clone() const override {
			return std::make_unique<NetAddrIPv4>(addr);
		}

	};

	class NetAddrIPv6 : public NetAddrBase<sockaddr_in6> {
	public:
		using NetAddrBase<sockaddr_in6>::NetAddrBase;

		virtual std::string toString(bool resolve = false) const override;
		virtual SocketHandle listen() const override;
		virtual SocketHandle connect() const override;
		virtual SocketHandle bindUDP() const override;
		virtual std::unique_ptr<INetAddr> clone() const override {
			return std::make_unique<NetAddrIPv6>(addr);
		}

	};

	void INetAddr::error(const std::string_view& addr, int errnr, const char* desc) {
		std::ostringstream bld;
		bld << "Network error:  " << desc << " - " << addr;
		throw std::system_error(errnr, win32_error_category(), bld.str());
	}

	void INetAddr::error(const INetAddr* addr, int errnr, const char* desc) {
		error(addr->toString(true), errnr, desc);
	}


	NetAddrList NetAddr::fromString(const std::string_view& addr_str, const std::string_view& default_svc) {
		std::string name;
		std::string svc;

		if (addr_str.empty()) INetAddr::error(addr_str, EINVAL, "Address can't be empty");
		if (addr_str[0] == '[') {
			auto pos = addr_str.find(']');
			if (pos == addr_str.npos) INetAddr::error(addr_str, EINVAL, "Parse error/invalid address");
			auto ipv6part = addr_str.substr(1, pos - 1);
			if (pos + 1 == addr_str.length()) {
				name = ipv6part;
				svc = default_svc;
			}
			else {
				if (addr_str[pos + 1] != ':') INetAddr::error(addr_str, EINVAL, "Parse error/invalid address or port");
				name = ipv6part;
				svc = addr_str.substr(pos + 1);
			}
		}
		else {
			auto pos = addr_str.rfind(':');
			if (pos == addr_str.npos) {
				name = addr_str;
				svc = default_svc;
			}
			else {
				name = addr_str.substr(0, pos);
				svc = addr_str.substr(pos + 1);
			}
		}
		const addrinfo req = { (name.empty() ? AI_PASSIVE : 0) | AI_ADDRCONFIG,AF_UNSPEC,SOCK_STREAM, IPPROTO_TCP,0,0,0,0 };
		addrinfo* resp;

		auto res = getaddrinfo(name.empty() ? nullptr : name.c_str(), svc.c_str(), &req, &resp);
		if (res) {
			INetAddr::error(addr_str, WSASYSCALLFAILURE, gai_strerrorA(res));
		}

		NetAddrList lst;
		addrinfo* iter = resp;
		while (iter != nullptr) {
			lst.push_back(NetAddr::fromSockAddr(*iter->ai_addr));
			iter = iter->ai_next;
		}

		freeaddrinfo(resp);
		return lst;
	}

	NetAddr::NetAddr(const NetAddr& other) :NetAddr(other.addr->clone()) {
	}

	NetAddr::NetAddr(NetAddr&& other) : addr(std::move(other.addr)) {
	}

	NetAddr& NetAddr::operator =(const NetAddr& other) {
		addr = other.addr->clone();
		return *this;
	}

	NetAddr& NetAddr::operator =(NetAddr&& other) {
		addr = std::move(other.addr);
		return *this;
	}

	NetAddr NetAddr::fromSockAddr(const sockaddr& addr) {
		switch (addr.sa_family) {
		case AF_INET: return NetAddr(std::make_unique<NetAddrIPv4>(reinterpret_cast<const sockaddr_in&>(addr)));
		case AF_INET6: return NetAddr(std::make_unique<NetAddrIPv6>(reinterpret_cast<const sockaddr_in6&>(addr)));
		default: return NetAddr(std::make_unique<NetAddrBase<sockaddr> >(addr));
		}
	}

	std::string NetAddrIPv4::toString(bool resolve) const {
		if (resolve) {
			char host[256];
			char service[256];
			if (getnameinfo(getAddr(), getAddrLen(), host, sizeof(host), service, sizeof(service), NI_NUMERICSERV) == 0) {
				std::ostringstream out;
				out << host << ":" << service;
				return out.str();
			}
			else {
				return toString(false);
			}
		}
		else {
			std::ostringstream out;
			auto bytes = reinterpret_cast<const unsigned char*>(&addr.sin_addr.s_addr);
			out << static_cast<int>(bytes[0]) << "."
				<< static_cast<int>(bytes[1]) << "."
				<< static_cast<int>(bytes[2]) << "."
				<< static_cast<int>(bytes[3]);
			out << ":";
			out << htons(addr.sin_port);
			return out.str();
		}
	}


	SocketHandle NetAddrIPv4::listen() const {
		SOCKET sock = ::socket(AF_INET, SOCK_STREAM , IPPROTO_TCP);
		if (sock < 0) error(this, WSAGetLastError(), "socket()");
		try {
			u_long iMode = 1;
			int flag = 1;
			if (::ioctlsocket(sock, FIONBIO, &iMode)) error(this, WSAGetLastError(), "ioctlsocket FIONBIO");
			if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(int))) error(this, WSAGetLastError(), "setsockopt(TCP_NODELAY)");
			if (::bind(sock, getAddr(), getAddrLen())) error(this, WSAGetLastError(), "bind()");
			if (::listen(sock, SOMAXCONN)) error(this, WSAGetLastError(), "listen()");
			return sock;
		}
		catch (...) {
			::closesocket(sock);
			throw;
		}
	}

	SocketHandle NetAddrIPv4::connect() const {
		SOCKET sock = ::socket(AF_INET, SOCK_STREAM , IPPROTO_TCP);
		if (sock < 0) error(this, WSAGetLastError(), "socket()");
		try {
			u_long iMode = 1;
			int flag = 1;
			if (::ioctlsocket(sock, FIONBIO, &iMode)) error(this, WSAGetLastError(), "ioctlsocket FIONBIO");
			if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(int))) error(this, WSAGetLastError(), "setsockopt(TCP_NODELAY)");
			if (::connect(sock, getAddr(), getAddrLen())) {
				int err = WSAGetLastError();
				if (err != WSAEINPROGRESS && err != WSAEWOULDBLOCK) error(this, err, "connect()");
			}
			return sock;
		}
		catch (...) {
			::closesocket(sock);
			throw;
		}
	}

	std::string NetAddrIPv6::toString(bool resolve) const {
		if (resolve) {
			char host[256];
			char service[256];
			if (getnameinfo(getAddr(), getAddrLen(), host, sizeof(host), service, sizeof(service), NI_NUMERICSERV) == 0) {
				std::ostringstream out;
				out << host << ":" << service;
				return out.str();
			}
			else {
				return toString(false);
			}
		}
		else {
			std::ostringstream out;
			out << "[" << std::hex;
			bool sep = false;
			for (auto c : addr.sin6_addr.u.Word) {
				if (sep) out << ":"; else sep = true;
				out << htons(c);
			}
			out << "]" << std::dec;
			out << ":";
			out << htons(addr.sin6_port);
			return out.str();
		}
	}

	SocketHandle NetAddrIPv6::listen() const {
		SocketHandle sock = ::socket(AF_INET6, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) error(this, WSAGetLastError(), "socket()");
		try {
			int flag = 1;
			u_long iMode = 1;
			if (::ioctlsocket(sock, FIONBIO, &iMode)) error(this, WSAGetLastError(), "ioctlsocket FIONBIO");
			if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(int))) error(this, WSAGetLastError(), "setsockopt(TCP_NODELAY)");
			if (::bind(sock, getAddr(), getAddrLen())) error(this, WSAGetLastError(), "bind()");
			if (::listen(sock, SOMAXCONN)) error(this, WSAGetLastError(), "listen()");
			return sock;
		}
		catch (...) {
			::closesocket(sock);
			throw;
		}
	}

	SocketHandle NetAddrIPv6::connect() const {
		SocketHandle sock = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (sock < 0) error(this, WSAGetLastError(), "socket()");
		try {
			int flag = 1;
			u_long iMode = 1;
			if (::ioctlsocket(sock, FIONBIO, &iMode)) error(this, WSAGetLastError(), "ioctlsocket FIONBIO");
			if (::setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<char*>(&flag), sizeof(int))) error(this, WSAGetLastError(), "setsockopt(TCP_NODELAY)");
			if (::connect(sock, getAddr(), getAddrLen())) {
				int err = WSAGetLastError();
				if (err != WSAEINPROGRESS && err != WSAEWOULDBLOCK) error(this, err, "connect()");
			}
			return sock;
		}
		catch (...) {
			::closesocket(sock);
			throw;
		}
	}


	 SocketHandle NetAddrIPv4::bindUDP() const {
		SOCKET sock = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) error(this, WSAGetLastError(), "socket()");
		try {
			u_long iMode = 1;
			if (::ioctlsocket(sock, FIONBIO, &iMode)) error(this, WSAGetLastError(), "ioctlsocket FIONBIO");
			if (::bind(sock, getAddr(), getAddrLen())) {
				error(this, WSAGetLastError(), "bind()");
			}
			return sock;
		}
		catch (...) {
			::closesocket(sock);
			throw;
		}
	}

	 SocketHandle NetAddrIPv6::bindUDP() const {
		 SocketHandle sock = ::socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
		if (sock < 0) error(this, WSAGetLastError(), "socket()");
		try {
			u_long iMode = 1;
			if (::ioctlsocket(sock, FIONBIO, &iMode)) error(this, WSAGetLastError(), "ioctlsocket FIONBIO");
			if (::bind(sock, getAddr(), getAddrLen())) {
				error(this, WSAGetLastError(), "bind()");
			}
			return sock;
		}
		catch (...) {
			::closesocket(sock);
			throw;
		}
	}

}

