/*
 * ssl_exception.h
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_SSL_EXCEPTION_H_
#define SRC_USERVER_SSL_EXCEPTION_H_
#include <stdexcept>
#include <string>

namespace userver {

class SSLError: public std::runtime_error{
public:
	SSLError();
};



}



#endif /* SRC_USERVER_SSL_EXCEPTION_H_ */
