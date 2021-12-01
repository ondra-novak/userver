/*
 * ssl.h
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_SSL_H_
#define SRC_USERVER_SSL_H_
#include <userver/socket.h>

namespace userver {




class AbstractSSLClientFactory {
public:
	using PSocket = std::unique_ptr<ISocket>;

	virtual PSocket makeSecure(Socket &sock, const std::string &host) = 0;

	virtual ~AbstractSSLClientFactory() {}

};

class PSSLClientFactory: public std::unique_ptr<AbstractSSLClientFactory> {
public:
	using PSocket = AbstractSSLClientFactory::PSocket;

	using std::unique_ptr<AbstractSSLClientFactory>::unique_ptr;

	///makes connected socket as secure
	/** after this, you need to call waitForConnect() to receive whether ssl has been successful */
	PSocket makeSecure(Socket &sock, const std::string &host) {return get()->makeSecure(sock,host);}
};

struct SSLConfig {
	std::string certStorageDir;
	std::string certStorageFile;
	std::string certFile;
	std::string privKeyFile;
};


PSSLClientFactory createSSLClient();
PSSLClientFactory createSSLClient(const SSLConfig &cfg);

static inline auto sslConnectFn(PSSLClientFactory &&ssl) {
	return[ssl = std::move(ssl)](const userver::NetAddr &addr, const std::string_view &host){
		userver::Socket sock = userver::Socket::connect(addr);
		return ssl->makeSecure(sock, std::string(host));
	};
}

static inline auto sslConnectFn() {
	return sslConnectFn(createSSLClient());
}

}




#endif /* SRC_USERVER_SSL_H_ */
