/*
 * netaddr.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include "platform.h"
#include "init.h"
#include <iomanip>
#include <sstream>
#include "netaddr.h"
#include "init.h"

namespace userver {

std::string INetAddr::unknownToString(const sockaddr *sockaddr,
		std::size_t slen) {

	std::ostringstream bld;
	bld << "?" << std::hex << std::setfill ('0') << std::setw(2);
	const unsigned char *b = reinterpret_cast<const unsigned char *>(sockaddr);
	for (std::size_t i = 0; i < slen; i++) bld << b[i];
	bld << "?";
	return bld.str();

}



class NetAddrIPv4: public NetAddrBase<sockaddr_in> {
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

class NetAddrIPv6: public NetAddrBase<sockaddr_in6> {
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

#ifndef _WIN32
class NetAddrUnix: public NetAddrBase<sockaddr_un> {
public:
	NetAddrUnix(const sockaddr_un &item, int perm):NetAddrBase<sockaddr_un>(item),permission(perm) {}
	NetAddrUnix(const std::string_view &addr);

	virtual std::string toString(bool resolve = false) const override;
	virtual int listen() const override;
	virtual int connect() const override;
	virtual int bindUDP() const override;
	virtual std::unique_ptr<INetAddr> clone() const override {
		return std::make_unique<NetAddrUnix>(addr,permission);
	}
protected:
	int permission;

};
#endif

void INetAddr::error(const std::string_view &addr, int errnr, const char *desc) {
	std::ostringstream bld;
		bld << "Network error:  " << desc << " - " << addr;
		throw std::system_error(errnr, error_category(), bld.str());
}

void INetAddr::error(const INetAddr *addr, int errnr, const char *desc) {
	error(addr->toString(true), errnr, desc);
}


NetAddrList NetAddr::fromStringMulti(const std::string_view &addr_str, const std::string_view &default_svc) {
	std::string blok;
	NetAddrList out;
	auto appendBlok=[&]{
		if (!blok.empty()) {
			auto n = fromString(blok, default_svc);
			for (auto &x: n) {
				out.push_back(std::move(x));
			}
		}
	};
	for (char c: addr_str) {
		if (isspace(c)) {
			appendBlok();
			blok.clear();
		} else{
			blok.push_back(c);
		}
	}
	appendBlok();
	return out;
}

NetAddrList NetAddr::fromString(const std::string_view &addr_str, const std::string_view &default_svc) {
	std::string name;
	std::string svc;

	initNetwork();

	if (addr_str.empty()) INetAddr::error(addr_str, EINVAL, "Address can't be empty");
	if (addr_str[0]=='[' ) {
		auto pos = addr_str.find(']');
		if (pos == addr_str.npos) INetAddr::error(addr_str, EINVAL, "Parse error/invalid address");
		auto ipv6part = addr_str.substr(1,pos-1);
		if (pos+1 == addr_str.length()) {
			name = ipv6part;
			svc = default_svc;
		} else {
			if (addr_str[pos+1] != ':') INetAddr::error(addr_str, EINVAL, "Parse error/invalid address or port");
			name = ipv6part;
			svc = addr_str.substr(pos+1);
		}
#ifndef _WIN32
	} else if (addr_str.substr(0,5) == "unix:") {
		NetAddrList lst;
		lst.push_back(NetAddr(std::make_unique<NetAddrUnix>(addr_str.substr(5))));
		return lst;
#endif		
	} else {
		auto pos = addr_str.rfind(':');
		if (pos == addr_str.npos) {
			name = addr_str;
			svc = default_svc;
		} else {
			name = addr_str.substr(0,pos);
			svc = addr_str.substr(pos+1);
		}
	}
	const addrinfo req = {(name.empty()?AI_PASSIVE:0)|AI_ADDRCONFIG,AF_UNSPEC,SOCK_STREAM, IPPROTO_TCP,0,0,0,0};
	addrinfo *resp;

	auto res =getaddrinfo(name.empty()?nullptr:name.c_str(), svc.c_str(), &req, &resp);
	if (res) {
	#ifdef _WIN32
		INetAddr::error(addr_str, WSASYSCALLFAILURE, gai_strerrorA(res));
	#else
		INetAddr::error(addr_str, ENOENT, gai_strerror(res));
	#endif
	}

	NetAddrList lst;
	addrinfo *iter = resp;
	while (iter != nullptr) {
		lst.push_back(NetAddr::fromSockAddr(*iter->ai_addr));
		iter = iter->ai_next;
	}

	freeaddrinfo(resp);
	return lst;
}

NetAddr::NetAddr(const NetAddr &other):NetAddr(other.addr->clone()) {
}

NetAddr::NetAddr(NetAddr &&other):addr(std::move(other.addr)) {
}

NetAddr& NetAddr::operator =(const NetAddr &other) {
		addr = other.addr->clone();
		return *this;
}

NetAddr& NetAddr::operator =(NetAddr &&other) {
	addr = std::move(other.addr);
	return *this;
}

NetAddr NetAddr::fromSockAddr(const sockaddr &addr) {
	switch (addr.sa_family) {
#ifndef _WIN32
	case AF_UNIX: return NetAddr(std::make_unique<NetAddrUnix>(reinterpret_cast<const sockaddr_un &>(addr),0600));
#endif
	case AF_INET: return NetAddr(std::make_unique<NetAddrIPv4>(reinterpret_cast<const sockaddr_in &>(addr)));
	case AF_INET6: return NetAddr(std::make_unique<NetAddrIPv6>(reinterpret_cast<const sockaddr_in6 &>(addr)));
	default: return NetAddr(std::make_unique<NetAddrBase<sockaddr> >(addr));
	}
}

std::string NetAddrIPv4::toString(bool resolve) const {
	initNetwork();
	if (resolve) {
		char host[256];
		char service[256];
		if (getnameinfo(getAddr(), getAddrLen(), host, sizeof(host), service, sizeof(service), NI_NUMERICSERV) == 0) {
			std::ostringstream out;
			out << host << ":" << service;
			return out.str();
		} else {
			return toString(false);
		}
	} else {
		std::ostringstream out;
		auto bytes = reinterpret_cast<const unsigned char *>(&addr.sin_addr.s_addr);
		out << static_cast<int>(bytes[0]) << "."
			<< static_cast<int>(bytes[1]) << "."
			<< static_cast<int>(bytes[2]) << "."
			<< static_cast<int>(bytes[3]);
		out << ":";
		out << htons(addr.sin_port);
		return out.str();
	}
}

static inline auto lastError() {
#ifdef _WIN32
	return WSAGetLastError();
#else
	return errno;
#endif
}

SocketHandle newSocket(const INetAddr *owner, int family, int type, int proto) {
	initNetwork();
#ifdef _WIN32
	SOCKET sock = ::socket(family, type , proto);
	if (sock  == INVALID_SOCKET) INetAddr::error(owner, lastError(), "socket()");
	u_long iMode = 1;
	if (::ioctlsocket(sock, FIONBIO, &iMode)) {
		closesocket(sock);
		INetAddr::error(owner, lastError(), "ioctlsocket FIONBIO");
	}
	return sock;
#else
	int sock = ::socket(family, type|SOCK_NONBLOCK|SOCK_CLOEXEC, proto);
	if (sock < 0) INetAddr::error(owner, lastError(), "socket()");
	return sock;
#endif
}

SocketHandle NetAddrIPv4::listen() const {
	SocketHandle sock = newSocket(this, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	try {
		int flag = 1;
#ifndef _WIN32
		if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&flag), sizeof(int))) error(this, lastError(), "setsockopt(SO_REUSEADDR)");
#endif
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, lastError(), "setsockopt(TCP_NODELAY)");
		if (::bind(sock,getAddr(), getAddrLen())) error(this, lastError(), "bind()");
		if (::listen(sock, SOMAXCONN)) error(this, lastError(), "listen()");
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}
}

SocketHandle NetAddrIPv4::connect() const {
	SocketHandle sock = newSocket(this, AF_INET, SOCK_STREAM, IPPROTO_TCP);
	try {
		int flag = 1;
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, lastError(), "setsockopt(TCP_NODELAY)");
		if (::connect(sock, getAddr(), getAddrLen())) {
			int err = lastError();
#ifdef _WIN32
			if (err != WSAEINPROGRESS && err != WSAEWOULDBLOCK) error(this, err, "connect()");
#else
			if (err != EINPROGRESS && err != EWOULDBLOCK) error(this, err, "connect()");
#endif
		}
		return sock;
	} catch (...) {
		closesocket(sock);
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
		} else {
			return toString(false);
		}
	} else {
		std::ostringstream out;
		out << "[" << std::hex;
		bool sep = false;
#ifdef _WIN32
		for (auto c : addr.sin6_addr.u.Word) {
#else
		for (auto c : addr.sin6_addr.__in6_u.__u6_addr16) {
#endif
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
	SocketHandle sock = newSocket(this, AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	try {
		int flag = 1;
		if (::setsockopt(sock, IPPROTO_IPV6, IPV6_V6ONLY, reinterpret_cast<char *>(&flag), sizeof(int))) error(this, lastError(), "setsockopt(IPV6_V6ONLY)");
#ifndef _WIN32
		if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&flag), sizeof(int))) error(this, lastError(), "setsockopt(SO_REUSEADDR)");
#endif
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, lastError(), "setsockopt(TCP_NODELAY)");
		if (::bind(sock,getAddr(), getAddrLen())) error(this, lastError(), "bind()");
		if (::listen(sock, SOMAXCONN)) error(this, lastError(), "listen()");
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}
}

SocketHandle NetAddrIPv6::connect() const {
	SocketHandle sock = newSocket(this, AF_INET6, SOCK_STREAM, IPPROTO_TCP);
	try {
		int flag = 1;
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, lastError(), "setsockopt(TCP_NODELAY)");
		if (::connect(sock, getAddr(), getAddrLen())) {
			int err = lastError();
#ifdef _WIN32
			if (err != WSAEINPROGRESS && err != WSAEWOULDBLOCK) error(this, err, "connect()");
#else
			if (err != EINPROGRESS && err != EWOULDBLOCK) error(this, err, "connect()");
#endif
		}
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}
}
#ifndef _WIN32

static sockaddr_un createUnAddress(const std::string_view &addr) {
	sockaddr_un s;
	s.sun_family = AF_UNIX;
	if (addr.length() >= sizeof(s.sun_path)-1)
		INetAddr::error(addr, EINVAL, "Socket path is too long.");
	char *c = s.sun_path;
	for (char x: addr) *c++ = x;
	*c = 0;
	return s;
}

NetAddrUnix::NetAddrUnix(const std::string_view &addr):NetAddrBase<sockaddr_un>(createUnAddress(addr))
{
	auto splt = addr.rfind(':');
    permission = 0;
    if (splt != addr.npos) {
    	auto iter = addr.begin() + splt+1;
    	auto end = addr.end();
    	while (iter != end) {
    		auto c = *iter;
    		++iter;
    		switch (c) {
				case '0': permission = permission * 8;break;
				case '1': permission = permission * 8 + 1;break;
				case '2': permission = permission * 8 + 2;break;
				case '3': permission = permission * 8 + 3;break;
				case '4': permission = permission * 8 + 4;break;
				case '5': permission = permission * 8 + 5;break;
				case '6': permission = permission * 8 + 6;break;
				case '7': permission = permission * 8 + 7;break;
				case 'u': permission = permission | S_IRUSR | S_IWUSR; break;
				case 'g': permission = permission | S_IRGRP | S_IWGRP; break;
				case 'o': permission = permission | S_IROTH | S_IWOTH; break;
				default:
					splt = addr.length();
					iter = end;
					break;
    		}
    	}
    	this->addr.sun_path[splt] = 0;
    }
}

std::string NetAddrUnix::toString(bool ) const {
	return std::string("unix:").append(addr.sun_path);
}

int NetAddrUnix::listen() const {
	if (access(addr.sun_path, 0) == 0) {
		bool attempt = true;
		try {
			int s = connect();
			attempt = false;
			close(s);
			error(this, EBUSY, "listen()");
		} catch(...) {
			if (!attempt) throw;
			unlink(addr.sun_path);
		}
	}
	SocketHandle sock = newSocket(this, AF_UNIX, SOCK_STREAM, 0);
	try {
		if (::bind(sock,getAddr(), getAddrLen())) error(this, lastError(), "bind()");
		if (::listen(sock, SOMAXCONN)) error(this, lastError(), "listen()");
		if (permission) chmod(addr.sun_path, permission);
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}
}

int NetAddrUnix::connect() const {
	SocketHandle sock = newSocket(this, AF_UNIX, SOCK_STREAM, 0);
	if (sock < 0) error(this, lastError(), "socket()");
	try {
		if (::connect(sock, getAddr(), getAddrLen())) {
			int err = lastError();
			if (err != EINPROGRESS && err != EWOULDBLOCK) error(this, err, "connect()");
		}
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}

}

inline int NetAddrUnix::bindUDP() const {
	error(this, EINVAL, "Cannot use this address");
	throw;
}

#endif

SocketHandle NetAddrIPv4::bindUDP() const {
	SocketHandle sock = newSocket(this, AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	try {
		if (::bind(sock,getAddr(), getAddrLen())) {
			error(this, lastError(), "bind()");
		}
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}
}


SocketHandle NetAddrIPv6::bindUDP() const {
	SocketHandle sock = newSocket(this, AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
	try {
		if (::bind(sock,getAddr(), getAddrLen())) {
			error(this, lastError(), "bind()");
		}
		return sock;
	} catch (...) {
		closesocket(sock);
		throw;
	}
}

}

