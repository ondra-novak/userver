/*
 * static_webserver.cpp
 *
 *  Created on: 25. 2. 2021
 *      Author: ondra
 */

#include "query_parser.h"
#include "static_webserver.h"

namespace userver {

StaticWebserver::StaticWebserver(const Config &cfg):
		cfg(cfg),
		docRootNative(cfg.document_root.native()) {

}

bool StaticWebserver::operator ()(PHttpServerRequest &req, std::string_view vpath) {
	if (vpath.empty() && req->directoryRedir()) return true;
	if (vpath[0] != '/') return false;
	vpath = vpath.substr(1);
	std::string buff;

	auto qm = vpath.find('?');
	if (qm != vpath.npos) vpath = vpath.substr(0,qm);

	auto fspath = cfg.document_root;
	if (vpath.empty()) fspath = fspath / cfg.indexFile;
	else {
		while (!vpath.empty()) {
			auto p = splitAt("/", vpath);
			buff.clear();
			QueryParser::urlDecode(p, buff);
			if (buff == ".") continue;
			else if (buff == "..") fspath = fspath.parent_path();
			else if (buff.empty()) {
				if (vpath.empty()) fspath = fspath / cfg.indexFile;
				else continue;
			} else {
				fspath = fspath / buff;
			}
		}
	}

	if (cfg.cachePeriod > 0) {
		auto exp = std::min<unsigned int>(cfg.cachePeriod, 2*365*24*60*60);
		httpDate(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()+std::chrono::seconds(exp)),
				[&](std::string_view d) {
					req->set("Expires", d);
		});
	}
	auto np = fspath.native();
	if (np.substr(0,docRootNative.length()) != docRootNative) return false;

	if (std::filesystem::is_directory(fspath)) {
		if (req->directoryRedir()) return true;
	}

	return HttpServerRequest::sendFile(std::move(req), np);


}

}


