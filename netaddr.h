/*
 * netaddr.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_NETADDR_H_
#define SRC_MAIN_NETADDR_H_
#include <sys/socket.h>
#include <string>
#include <vector>

namespace userver {

///just wraps network address
class INetAddr {
public:

	virtual ~INetAddr() {}
	virtual std::size_t getAddrLen() const = 0;
	virtual const sockaddr *getAddr() const = 0;
	virtual std::string toString(bool resolve = false) const = 0;
	virtual int listen() const  = 0;
	virtual int connect() const  = 0;
	virtual std::unique_ptr<INetAddr> clone() const = 0;


	static std::string unknownToString(const sockaddr *sockaddr, std::size_t slen);
};

template<typename T>
class NetAddrBase: public INetAddr {
public:

	explicit NetAddrBase(const T &item):addr(item) {}
	virtual std::size_t getAddrLen() const override {return sizeof(addr);}
	virtual const sockaddr *getAddr() const override  {return reinterpret_cast<const struct sockaddr *>(&addr);}
	virtual int listen() const  override;
	virtual int connect() const  override;
	virtual std::string toString(bool resolve = false) const {
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

	std::size_t getAddrLen() const {return addr->getAddrLen();}
	const sockaddr *getAddr() const {return addr->getAddr();}
	std::string toString(bool resolve) const {return addr->toString(resolve);}
	int listen() const  {return addr->listen();}
	int connect() const  {return addr->connect();}

protected:
	PNetAddr addr;

};

template<typename T>
inline int NetAddrBase<T>::listen() const {
	error(this,EINVAL, "Unsupported address");return 0;
}

template<typename T>
inline int NetAddrBase<T>::connect() const {
	error(this,EINVAL, "Unsupported address");return 0;
}

}

#endif /* SRC_MAIN_NETADDR_H_ */
