/*
 * staticWebserver.h
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_STATIC_WEBSERVER_H_
#define SRC_USERVER_STATIC_WEBSERVER_H_

#include <filesystem>
#include "http_server.h"

namespace userver {

class StaticWebserver {
public:

	struct Config {
		std::filesystem::path document_root;
		std::string indexFile;
		std::string version_prefix;
		std::string version_nr;
		unsigned int cachePeriod = 0;
	};

	StaticWebserver(const Config &cfg);

	bool operator()(PHttpServerRequest &req, std::string_view vpath);

protected:
	Config cfg;
	std::string docRootNative;
	std::string whole_ver;

};


}



#endif /* SRC_USERVER_STATIC_WEBSERVER_H_ */
