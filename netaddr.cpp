/*
 * netaddr.cpp
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#include <errno.h>
#include <unistd.h>
#include <iomanip>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>
#include <sstream>
 #include <sys/types.h>
#include <netdb.h>
#include "netaddr.h"

#include <sys/socket.h>
#include <sys/stat.h>
#include <cerrno>

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
	virtual int listen() const override;
	virtual int connect() const override;
	virtual std::unique_ptr<INetAddr> clone() const override {
		return std::make_unique<NetAddrIPv4>(addr);
	}

};

class NetAddrIPv6: public NetAddrBase<sockaddr_in6> {
public:
	using NetAddrBase<sockaddr_in6>::NetAddrBase;

	virtual std::string toString(bool resolve = false) const override;
	virtual int listen() const override;
	virtual int connect() const override;
	virtual std::unique_ptr<INetAddr> clone() const override {
		return std::make_unique<NetAddrIPv6>(addr);
	}

};

class NetAddrUnix: public NetAddrBase<sockaddr_un> {
public:
	using NetAddrBase<sockaddr_un>::NetAddrBase;
	NetAddrUnix(const std::string_view &addr);

	virtual std::string toString(bool resolve = false) const override;
	virtual int listen() const override;
	virtual int connect() const override;
	virtual std::unique_ptr<INetAddr> clone() const override {
		return std::make_unique<NetAddrUnix>(addr);
	}
protected:
	int permission;

};
static void error(const std::string_view &addr, int errnr, const char *desc) {
	std::ostringstream bld;
	bld<<"Network error: " << strerror(errnr) << " - " << desc << " - " << addr;
	throw std::system_error(errnr, std::generic_category(), bld.str());
}

static void error(const INetAddr *addr, int errnr, const char *desc) {
	error(addr->toString(true), errnr, desc);
}


NetAddrList NetAddr::fromString(const std::string_view &addr_str, const std::string_view &default_svc) {
	std::string name;
	std::string svc;

	if (addr_str.empty()) error(addr_str, EINVAL, "Address can't be empty");
	if (addr_str[0]=='[' ) {
		auto pos = addr_str.find(']');
		if (pos == addr_str.npos) error(addr_str, EINVAL, "Parse error/invalid address");
		auto ipv6part = addr_str.substr(1,pos-1);
		if (pos+1 == addr_str.length()) {
			name = ipv6part;
			svc = default_svc;
		} else {
			if (addr_str[pos+1] != ':') error(addr_str, EINVAL, "Parse error/invalid address or port");
			name = ipv6part;
			svc = addr_str.substr(pos+1);
		}
	} else if (addr_str.substr(0,5) == "unix:") {
		NetAddrList lst;
		lst.push_back(NetAddr(std::make_unique<NetAddrUnix>(addr_str.substr(5))));
		return lst;
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
	const addrinfo req = {(name.empty()?AI_PASSIVE:0)|AI_ADDRCONFIG,AF_UNSPEC,SOCK_STREAM, IPPROTO_TCP};
	addrinfo *resp;

	auto res =getaddrinfo(name.empty()?nullptr:name.c_str(), svc.c_str(), &req, &resp);
	if (res) {
		error(addr_str, ENOENT, gai_strerror(res));
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
	case AF_UNIX: return NetAddr(std::make_unique<NetAddrUnix>(reinterpret_cast<const sockaddr_un &>(addr)));
	case AF_INET: return NetAddr(std::make_unique<NetAddrIPv4>(reinterpret_cast<const sockaddr_in &>(addr)));
	case AF_INET6: return NetAddr(std::make_unique<NetAddrIPv6>(reinterpret_cast<const sockaddr_in6 &>(addr)));
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


int NetAddrIPv4::listen() const {
	int sock = ::socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sock < 0) error(this, errno, "socket()");
	try {
		int flag = 1;
		if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&flag), sizeof(int))) error(this, errno, "setsockopt(SO_REUSEADDR)");
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, errno, "setsockopt(TCP_NODELAY)");
		if (::bind(sock,getAddr(), getAddrLen())) error(this, errno, "bind()");
		if (::listen(sock, SOMAXCONN)) error(this, errno, "listen()");
		return sock;
	} catch (...) {
		::close(sock);
		throw;
	}
}

int NetAddrIPv4::connect() const {
	int sock = ::socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sock < 0) error(this, errno, "socket()");
	try {
		int flag = 1;
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, errno, "setsockopt(TCP_NODELAY)");
		if (::connect(sock, getAddr(), getAddrLen())) {
			int err = errno;
			if (err != EINPROGRESS && err != EWOULDBLOCK) error(this, err, "connect()");
		}
		return sock;
	} catch (...) {
		::close(sock);
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
		for (auto c: addr.sin6_addr.__in6_u.__u6_addr16) {
			if (sep) out << ":";else sep = true;
			out << htons(c);
		}
		out << "]" << std::dec;
		out << ":";
		out << htons(addr.sin6_port);
		return out.str();
	}
}

int NetAddrIPv6::listen() const {
	int sock = ::socket(AF_INET6, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sock < 0) error(this, errno, "socket()");
	try {
		int flag = 1;
		if (::setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<char *>(&flag), sizeof(int))) error(this, errno, "setsockopt(SO_REUSEADDR)");
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, errno, "setsockopt(TCP_NODELAY)");
		if (::bind(sock,getAddr(), getAddrLen())) error(this, errno, "bind()");
		if (::listen(sock, SOMAXCONN)) error(this, errno, "listen()");
		return sock;
	} catch (...) {
		::close(sock);
		throw;
	}
}

int NetAddrIPv6::connect() const {
	int sock = ::socket(AF_INET, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, IPPROTO_TCP);
	if (sock < 0) error(this, errno, "socket()");
	try {
		int flag = 1;
		if (::setsockopt(sock,IPPROTO_TCP,TCP_NODELAY,reinterpret_cast<char *>(&flag),sizeof(int))) error(this, errno, "setsockopt(TCP_NODELAY)");
		if (::connect(sock, getAddr(), getAddrLen())) {
			int err = errno;
			if (err != EINPROGRESS && err != EWOULDBLOCK) error(this, err, "connect()");
		}
		return sock;
	} catch (...) {
		::close(sock);
		throw;
	}
}

static sockaddr_un createUnAddress(const std::string_view &addr) {
	sockaddr_un s;
	s.sun_family = AF_UNIX;
	if (addr.length() >= sizeof(s.sun_path)-1) error(addr, EINVAL, "Socket path is too long.");
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

std::string NetAddrUnix::toString(bool resolve) const {
	return std::string("unix:").append(addr.sun_path);
}

int NetAddrUnix::listen() const {
	if (access(addr.sun_path, 0)) {
		try {
			int s = connect();
			close(s);
			error(this, EBUSY, "listen()");
		} catch(...) {
			unlink(addr.sun_path);
		}
	}
	int sock = ::socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (sock < 0) error(this, errno, "socket()");
	try {
		if (::bind(sock,getAddr(), getAddrLen())) error(this, errno, "bind()");
		if (::listen(sock, SOMAXCONN)) error(this, errno, "listen()");
		if (permission) chmod(addr.sun_path, permission);
		return sock;
	} catch (...) {
		::close(sock);
		throw;
	}
}

int NetAddrUnix::connect() const {
	int sock = ::socket(AF_UNIX, SOCK_STREAM|SOCK_NONBLOCK|SOCK_CLOEXEC, 0);
	if (sock < 0) error(this, errno, "socket()");
	try {
		if (::connect(sock, getAddr(), getAddrLen())) {
			int err = errno;
			if (err != EINPROGRESS && err != EWOULDBLOCK) error(this, err, "connect()");
		}
		return sock;
	} catch (...) {
		::close(sock);
		throw;
	}

}


