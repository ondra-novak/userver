/*
 * ssl_exception.cpp
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#include <openssl/err.h>
#include "ssl_exception.h"

namespace userver {

static std::string createSSLError() {
	std::string errors;
	ERR_print_errors_cb([](const char *str, size_t len, void *u){
		std::string *s = reinterpret_cast<std::string *>(u);
		s->append(str,len);
		s->append("\n");
		return 1;
	}, &errors);
	return errors;
}


SSLError::SSLError():std::runtime_error(createSSLError()) {
}


}


