/*
 * ssl.cpp
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#include <openssl/ssl.h>
#include <userver/ssl_exception.h>
#include "ssl.h"
#include "ssl_socket.h"

namespace userver {

#ifndef SSLv23_method
#define TLS_server_method TLSv1_2_server_method
#define TLS_client_method TLSv1_2_client_method
#endif

PSSLClientFactory createSSLClient() {
	return createSSLClient({});
}

class SSLClientFactory: public AbstractSSLClientFactory {
public:

	SSLClientFactory(const SSLConfig &cfg):ctx(SSL_CTX_new(TLS_client_method()), SSL_CTX_Free()) {
		if (cfg.certStorageDir.empty() && cfg.certStorageFile.empty()) {
			SSL_CTX_set_default_verify_paths(ctx.get());
		} else {
			if (SSL_CTX_load_verify_locations(ctx.get(),
					cfg.certStorageFile.empty()?nullptr:cfg.certStorageFile.c_str(),
					cfg.certStorageDir.empty()?nullptr:cfg.certStorageDir.c_str())<1) throw SSLError();

		}
		if (!cfg.certFile.empty()) {
		    if (SSL_CTX_use_certificate_file(ctx.get(), cfg.certFile.c_str(), SSL_FILETYPE_PEM) <= 0) {
			        throw SSLError();
		    }
		}
		if (!cfg.privKeyFile.empty()){
			if (SSL_CTX_use_PrivateKey_file(ctx.get(), cfg.privKeyFile.c_str(), SSL_FILETYPE_PEM) <= 0 ) {
				throw SSLError();
			}

		}

	}

	virtual PSocket makeSecure(Socket &sock, const std::string &host) override {
		auto sslsock = std::make_unique<SSLSocket>(std::move(sock), ctx, SSLSocket::Mode::connect);
		auto ssl = sslsock->getSSLSocket();
		if(!SSL_set_tlsext_host_name(ssl, host.c_str())) throw SSLError();
		if(!X509_VERIFY_PARAM_set1_host(SSL_get0_param(ssl), host.c_str(), 0)) throw SSLError();
	    return PSocket(sslsock.release());
	}


	~SSLClientFactory() {}

protected:
	PSSL_CTX ctx;

};

PSSLClientFactory createSSLClient(const SSLConfig &cfg) {
	return std::make_unique<SSLClientFactory>(cfg);
}


}
