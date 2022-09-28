/*
 * http_exception.h
 *
 *  Created on: 7. 8. 2022
 *      Author: ondra
 */

#ifndef _SRC_USERVER_HTTP_EXCEPTION_H_
#define _SRC_USERVER_HTTP_EXCEPTION_H_
#include <exception>
#include <string>

namespace userver {

class HttpStatusCodeException: public std::exception {
public:
    HttpStatusCodeException(int code, const std::string_view &msg)
        :code(code), message(msg) {}

    int get_code() const {
        return code;
    }

    std::string get_message() const {
        return message;
    }

    const char *what() const noexcept override {
        if (whatmsg.empty()) {
            whatmsg+=("Unexpected HTTP status: ");
            whatmsg+=std::to_string(code)+" "+message;
        }
        return whatmsg.c_str();
    }

protected:
    int code;
    std::string message;
    mutable std::string whatmsg;
};

}



#endif /* _SRC_USERVER_HTTP_EXCEPTION_H_ */
