/*
 * openapi.h
 *
 *  Created on: 27. 10. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_OPENAPI_H_
#define SRC_USERVER_OPENAPI_H_

#include "http_server.h"
#include "query_parser.h"

namespace userver {

namespace _undefined {
	class Obj;
}

class OpenAPIServer: public HttpServer {
public:

	using Handler = CallbackT<bool(PHttpServerRequest &, const RequestParams &)>;


	struct InfoObject {
		std::string title;
		std::string version;
		std::string description;
		std::string termsOfService;
		std::string contact_name;
		std::string contact_url;
		std::string contact_email;
	};

	struct ServerVariable {
		std::string name;
		std::string description;
		std::string def_value;
		std::vector<std::string> enum_values;
	};

	struct ServerObject {
		std::string url;
		std::string description;
		std::vector<ServerVariable> variables;
	};

	struct SchemaItem;

	struct ParameterObject {
		std::string name;
		std::string in;
		std::string type;
		std::string description;
		std::vector<SchemaItem> properties;
		bool required=true;
	};

	struct MediaObject {
		std::string content_type;
		std::string name;
		std::string type;
		std::string description;
		std::vector<SchemaItem> properties;
	};

	struct ResponseObject {
		int status_code; //status code or default=0
		std::string description;
		std::vector<MediaObject> response;
		std::vector<ParameterObject> headers;
	};

	struct SchemaItem {
		/** Name of the item, optional for non-object properties (appears in summary) */
		std::string name;
		/** Type of the item. This can be one of standard type or metatype
		 *
		 * Types: string, number, integer, float, double, boolean, array, object, assoc, null
		 * Meta types: oneOf, allOf, anyOf,
		 *
		 *
		 *
		 */
		std::string type;
		/** Description of this item */
		std::string description;
		/** Properties
		 *
		 * Properties are required for container types
		 * array - defines item type
		 * object - defines member items - includes their names
		 * assoc - defines format of value
		 *
		 * oneOf - defines variants
		 * allOf - defines variants
		 * anyOf - defines variants
		 *
		 */
		std::vector<SchemaItem> properties;

		bool optional = false;

	};


	class PathInfo {
	public:
		PathInfo(OpenAPIServer &owner, int pathIndex, int methodIndex):owner(owner),pathIndex(pathIndex), methodIndex(methodIndex) {}

		PathInfo handler(Handler &&handler);
		template<typename T, typename Q> PathInfo method(T ptr, bool (Q::*fn)(PHttpServerRequest &, const RequestParams &));
		PathInfo operator>>(Handler &&handler);
		PathInfo GET(const std::string_view &tag = std::string_view(),
					const std::string_view &summary = std::string_view(),
					const std::string_view &desc = std::string_view(),
					const std::initializer_list<ParameterObject> &params = {},
					const std::initializer_list<ResponseObject> &responses = {},
					bool security=true,
					bool deprecated=false);
		PathInfo PUT(const std::string_view &tag = std::string_view(),
					const std::string_view &summary = std::string_view(),
					const std::string_view &desc = std::string_view(),
					const std::initializer_list<ParameterObject> &params = {},
					const std::string_view &body_desc  = std::string_view(),
					const std::initializer_list<MediaObject> &requests = {},
					const std::initializer_list<ResponseObject> &responses = {},
					bool security=true,
					bool deprecated=false);
		PathInfo POST(const std::string_view &tag  = std::string_view(),
					const std::string_view &summary  = std::string_view(),
					const std::string_view &desc = std::string_view(),
					const std::initializer_list<ParameterObject> &params ={},
					const std::string_view &body_desc = std::string_view(),
					const std::initializer_list<MediaObject> &requests ={},
					const std::initializer_list<ResponseObject> &responses={},
					bool security=true,
					bool deprecated=false);
		PathInfo DELETE(const std::string_view &tag = std::string_view(),
					const std::string_view &summary = std::string_view(),
					const std::string_view &desc = std::string_view(),
					const std::initializer_list<ParameterObject> &params = {},
					const std::string_view &body_desc = std::string_view(),
					const std::initializer_list<MediaObject> &requests = {},
					const std::initializer_list<ResponseObject> &responses = {},
					bool security=true,
					bool deprecated=false);



	protected:
		OpenAPIServer &owner;
		int pathIndex;
		int methodIndex;
	};


	void setInfo(InfoObject &&info);
	void addServer(ServerObject &&server);
	PathInfo addPath(const std::string_view &path);
	using HttpServer::addPath;

	std::string generateDef(const std::string_view &root_path);

	void addSwagFilePath(const std::string &path);
	void addSwagBrowser(const std::string &path);

protected:

	struct OperationStruct{
		std::string tag;
		std::string summary;
		std::string desc;
		std::vector<ParameterObject> params;
		std::vector<ResponseObject> responses;
		std::vector<MediaObject> requests;
		std::string body_desc;
		bool security;
		bool deprecated;
	};


	struct PathReg {
		std::string path;
		std::optional<OperationStruct> GET={},PUT={},POST={},DELETE={};
	};

	InfoObject info;
	std::vector<PathReg> paths;
	std::vector<ServerObject> servers;

	bool checkMethod(int pathIndex, PHttpServerRequest &req);

	void generateDef(std::ostream &out, const std::string_view &root_path);

	static void serialize(_undefined::Obj &&obj, const OperationStruct &op, const std::string &opid);
	static void serialize(_undefined::Obj &&obj, const ParameterObject &param);
	template<typename Sch>
	static void serializeSchema(_undefined::Obj &&obj, const Sch &param);

	using VarList = std::vector<std::pair<std::string_view, std::string_view> >;

	enum class Method {
		GET=0,
		PUT=1,
		POST=2,
		DELETE=3,
		count=4
	};

	struct PathTreeItem {
		bool has_handler = false;
		///handle responsible to execute on this node
		///can be null, if no handler
		Handler h[static_cast<int>(Method::count)];
		///map for all branches from this path
		std::map<std::string, PathTreeItem, std::less<> > branches;
		///map of braches with variables
		std::vector<std::pair<std::string, PathTreeItem> > variables;

		static std::string_view extractPath(std::string_view &vpath) {
			auto nps = vpath.find('/',1);
			std::string_view item;
			if (nps == vpath.npos) {
				item = vpath.substr(1);
				vpath = "";
			} else {
				item = vpath.substr(1,nps-1);
				vpath = vpath.substr(nps);
			}
			return item;

		}

		///Walk structure and search handler
		/**
		 * @param vpath to search
		 * @param vars current list extracted variables - in order of variables in path
		 * @param cb callback
		 *
		 * @note the callback receives (handler, vars)
		 */
		template<typename CB>
		bool findPath(std::string_view vpath, VarList &vars,CB &&cb) const {
			if (vpath.empty() || vpath[0] != '/' || vpath == "/") {
				if (!has_handler) return false;
				return cb(*this, vars);
			} else {
				auto item = extractPath(vpath);
				auto iter = branches.find(item);
				if (iter != branches.end() && iter->second.findPath(vpath, vars, std::forward<CB>(cb))) return true;
				for (const auto &v: variables) {
					vars.push_back(std::pair<std::string_view,std::string_view>(v.first, item));
					if (v.second.findPath(vpath, vars, cb)) return true;
				}
				return false;
			}
		}
		bool addHandler(Method m, std::string_view vpath, Handler &&h);
	};

	PathTreeItem root;

	virtual bool execHandler(userver::PHttpServerRequest &req, const std::string_view &vpath);

	class QueryParserWithVars;


};




template<typename T, typename Q>
inline OpenAPIServer::PathInfo userver::OpenAPIServer::PathInfo::method(
		T ptr, bool (Q::*fn)(PHttpServerRequest&, const RequestParams&)) {
	return handler([=](PHttpServerRequest& a, const RequestParams& b){
		return ((*ptr).*fn)(a,b);
	});
}

}
#endif /* SRC_USERVER_OPENAPI_H_ */

