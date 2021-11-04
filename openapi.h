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
		PathInfo(OpenAPIServer &owner, int pathIndex):owner(owner),pathIndex(pathIndex) {}

		PathInfo handler(Handler &&handler);
		PathInfo GET(const std::string_view &tag,
					const std::string_view &summary,
					const std::string_view &desc,
					const std::initializer_list<ParameterObject> &params,
					const std::initializer_list<ResponseObject> &responses,
					bool security=true,
					bool deprecated=false);
		PathInfo PUT(const std::string_view &tag,
					const std::string_view &summary,
					const std::string_view &desc,
					const std::initializer_list<ParameterObject> &params,
					const std::string_view &body_desc,
					const std::initializer_list<MediaObject> &requests,
					const std::initializer_list<ResponseObject> &responses,
					bool security=true,
					bool deprecated=false);
		PathInfo POST(const std::string_view &tag,
					const std::string_view &summary,
					const std::string_view &desc,
					const std::initializer_list<ParameterObject> &params,
					const std::string_view &body_desc,
					const std::initializer_list<MediaObject> &requests,
					const std::initializer_list<ResponseObject> &responses,
					bool security=true,
					bool deprecated=false);
		PathInfo DELETE(const std::string_view &tag,
					const std::string_view &summary,
					const std::string_view &desc,
					const std::initializer_list<ParameterObject> &params,
					const std::string_view &body_desc,
					const std::initializer_list<MediaObject> &requests,
					const std::initializer_list<ResponseObject> &responses,
					bool security=true,
					bool deprecated=false);



	protected:
		OpenAPIServer &owner;
		int pathIndex;
	};


	void setInfo(InfoObject &&info);
	void addServer(ServerObject &&server);
	PathInfo addPath(const std::string_view &path);
	using HttpServer::addPath;

	std::string generateDef();

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
		std::optional<OperationStruct> GET,PUT,POST,DELETE;
	};

	InfoObject info;
	std::vector<PathReg> paths;
	std::vector<ServerObject> servers;

	bool checkMethod(int pathIndex, PHttpServerRequest &req);

	void generateDef(std::ostream &out);

	static void serialize(_undefined::Obj &&obj, const OperationStruct &op, const std::string &opid);
	static void serialize(_undefined::Obj &&obj, const ParameterObject &param);
	template<typename Sch>
	static void serializeSchema(_undefined::Obj &&obj, const Sch &param);
};


}



#endif /* SRC_USERVER_OPENAPI_H_ */
