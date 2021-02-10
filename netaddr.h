/*
 * netaddr.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_NETADDR_H_
#define SRC_MAIN_NETADDR_H_
#include <string>
#include <vector>
#include <memory>
#include "platform.h"

namespace userver {

///just wraps network address
class INetAddr {
public:

	virtual ~INetAddr() {}
	virtual socklen_t getAddrLen() const = 0;
	virtual const sockaddr *getAddr() const = 0;
	virtual std::string toString(bool resolve = false) const = 0;
	virtual SocketHandle listen() const  = 0;
	virtual SocketHandle connect() const  = 0;
	virtual SocketHandle bindUDP() const = 0;
	virtual std::unique_ptr<INetAddr> clone() const = 0;


	static std::string unknownToString(const sockaddr *sockaddr, std::size_t slen);
	static void error(const INetAddr *addr, int errnr, const char *desc);
	static void error(const std::string_view &addr, int errnr, const char *desc);
};

template<typename T>
class NetAddrBase: public INetAddr {
public:

	explicit NetAddrBase(const T &item):addr(item) {}
	virtual socklen_t getAddrLen() const override {return sizeof(addr);}
	virtual const sockaddr *getAddr() const override  {return reinterpret_cast<const struct sockaddr *>(&addr);}
	virtual SocketHandle listen() const  override;
	virtual SocketHandle connect() const  override;
	virtual SocketHandle bindUDP() const  override;
	virtual std::string toString(bool = false) const override {
		return unknownToString(getAddr(), getAddrLen());
	}
	virtual std::unique_ptr<INetAddr> clone() const override {
		return std::make_unique<NetAddrBase>(addr);
	}

protected:
	T addr;
};


class NetAddr;

using NetAddrList = std::vector<NetAddr>;

class NetAddr {
public:

	using PNetAddr = std::unique_ptr<INetAddr>;

	explicit NetAddr(PNetAddr &&addr):addr(std::move(addr)) {}
	NetAddr(const NetAddr &other);
	NetAddr(NetAddr &&other);
	NetAddr &operator=(const NetAddr &other);
	NetAddr &operator=(NetAddr &&other);

	static NetAddrList fromString(const std::string_view &addr_str, const std::string_view &default_svc = std::string_view());
	static NetAddr fromSockAddr(const sockaddr &addr);

	socklen_t getAddrLen() const {return addr->getAddrLen();}
	const sockaddr *getAddr() const {return addr->getAddr();}
	std::string toString(bool resolve) const {return addr->toString(resolve);}
	SocketHandle listen() const  {return addr->listen();}
	SocketHandle connect() const  {return addr->connect();}
	SocketHandle bindUDP() const  {return addr->bindUDP();}

protected:
	PNetAddr addr;

};

template<typename T>
inline SocketHandle NetAddrBase<T>::listen() const {
	error(this,EINVAL, "Unsupported address");return 0;
}

template<typename T>
inline SocketHandle NetAddrBase<T>::connect() const {
	error(this,EINVAL, "Unsupported address");return 0;
}

template<typename T>
inline SocketHandle NetAddrBase<T>::bindUDP() const {
	error(this,EINVAL, "Unsupported address");return 0;
}

}


#endif /* SRC_MAIN_NETADDR_H_ */
