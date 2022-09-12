/*
 * openapi.cpp
 *
 *  Created on: 27. 10. 2021
 *      Author: ondra
 */


#include "openapi.h"

#include <sstream>

namespace userver {

OpenAPIServer::PathInfo OpenAPIServer::PathInfo::GET(
		const std::string_view &tag, const std::string_view &summary,
		const std::string_view &desc,
		const std::initializer_list<ParameterObject> &params,
		const std::initializer_list<ResponseObject> &responses, bool security,
		bool deprecated) {
	owner.paths[pathIndex].GET = {std::string(tag), std::string(summary),
			std::string(desc), params, responses, {}, "", security, deprecated};
	return PathInfo(owner,pathIndex,static_cast<int>(Method::GET));
}

OpenAPIServer::PathInfo OpenAPIServer::PathInfo::PUT(
		const std::string_view &tag, const std::string_view &summary,
		const std::string_view &desc,
		const std::initializer_list<ParameterObject> &params,
		const std::string_view &body_desc,
		const std::initializer_list<MediaObject> &requests,
		const std::initializer_list<ResponseObject> &responses, bool security,
		bool deprecated) {
	owner.paths[pathIndex].PUT = {std::string(tag), std::string(summary),
			std::string(desc), params, responses, requests, std::string(body_desc), security, deprecated};
	return PathInfo(owner,pathIndex,static_cast<int>(Method::PUT));
}

OpenAPIServer::PathInfo OpenAPIServer::PathInfo::POST(
		const std::string_view &tag, const std::string_view &summary,
		const std::string_view &desc,
		const std::initializer_list<ParameterObject> &params,
		const std::string_view &body_desc,
		const std::initializer_list<MediaObject> &requests,
		const std::initializer_list<ResponseObject> &responses, bool security,
		bool deprecated) {
	owner.paths[pathIndex].POST = {std::string(tag), std::string(summary),
			std::string(desc), params, responses, requests, std::string(body_desc), security, deprecated};
	return PathInfo(owner,pathIndex,static_cast<int>(Method::POST));
}

OpenAPIServer::PathInfo OpenAPIServer::PathInfo::DELETE(
		const std::string_view &tag, const std::string_view &summary,
		const std::string_view &desc,
		const std::initializer_list<ParameterObject> &params,
		const std::string_view &body_desc,
		const std::initializer_list<MediaObject> &requests,
		const std::initializer_list<ResponseObject> &responses, bool security,
		bool deprecated) {
	owner.paths[pathIndex].DELETE = {std::string(tag), std::string(summary),
			std::string(desc), params, responses, requests, std::string(body_desc), security, deprecated};
	return PathInfo(owner,pathIndex,static_cast<int>(Method::DELETE));
}


void OpenAPIServer::setInfo(InfoObject &&info) {
	this->info = std::move(info);
}

void OpenAPIServer::addServer(ServerObject &&server) {
	servers.push_back(std::move(server));
}

OpenAPIServer::PathInfo OpenAPIServer::addPath( const std::string_view &path) {
	auto idx = paths.size();
	paths.push_back({std::string(path)});
	return PathInfo(*this, idx,-1);
}


bool OpenAPIServer::checkMethod(int pathIndex, PHttpServerRequest &req) {
	const PathReg reg = paths[pathIndex];
	std::string_view method = req->getMethod();
	if (method == "GET" && reg.GET.has_value()) return true;
	if (method == "PUT" && reg.PUT.has_value()) return true;
	if (method == "POST" && reg.POST.has_value()) return true;
	if (method == "DELETE" && reg.DELETE.has_value()) return true;
	std::string allowed;
	if (reg.GET.has_value()) allowed.append(", GET");
	if (reg.PUT.has_value()) allowed.append(", PUT");
	if (reg.POST.has_value()) allowed.append(", POST");
	if (reg.DELETE.has_value()) allowed.append(", DELETE");
	if (allowed.empty()) {
		req->sendErrorPage(500);
		return false;
	}
	req->set("Allow", std::string_view(allowed).substr(2));
	req->sendErrorPage(405);
	return false;
}


std::string OpenAPIServer::generateDef() {
	std::ostringstream out;
	generateDef(out);
	return out.str();

}

class Str {
public:
	Str(std::string_view txt):txt(txt) {}
	template<typename Stream>
	friend Stream &operator<<(Stream &stream, const Str &str) {
		stream.put('"');
		for (char c: str.txt) {
			switch (c) {
			case '\n': stream<<"\\n";break;
			case '\r': stream<<"\\r";break;
			case '\t': stream<<"\\t";break;
			case '\b': stream<<"\\b";break;
			case '\a': stream<<"\\a";break;
			case '\0': stream<<"\\u0000";break;
			case '\\':  stream<<"\\\\";break;
			case '/':  stream<<"\\/";break;
			case '"':  stream<<"\\\"";break;
			default: if (c >= 32) stream.put(c);break;
			}
		}
		stream.put('"');
		return stream;
	}
protected:
	std::string_view txt;
};

namespace _undefined {

class Arr;



class Obj {
public:
	Obj(std::ostream &out):out(out) {out << "{";}
	~Obj() {out << "}";}

	Obj &operator()(std::string_view k,const char * v) {
		return this->operator ()(k, std::string_view(v));
	}
	Obj &operator()(std::string_view k,std::string_view v) {
		if (sep) out << ",";
		sep = true;
		out << Str(k) << ":" << Str(v);
		return *this;
	}
	Obj &operator()(std::string_view k,double v) {
		if (sep) out << ",";
		sep = true;
		out << Str(k) << ":" << v;
		return *this;
	}
	Obj &operator()(std::string_view k,int v) {
		if (sep) out << ",";
		sep = true;
		out << Str(k) << ":" << v;
		return *this;
	}
	Obj &operator()(std::string_view k,bool v) {
		if (sep) out << ",";
		sep = true;
		out << Str(k) << ":" << (v?"true":"false");
		return *this;
	}
	Obj &operator()(std::string_view k,std::nullptr_t) {
		if (sep) out << ",";
		sep = true;
		out << Str(k) << ":" << "null";
		return *this;
	}
	Obj object(std::string_view k) {
		if (sep) out << ",";
		sep = true;
		out << Str(k) << ":";
		return Obj(out);
	}
	Arr array(std::string_view k);

protected:
	std::ostream &out;
	bool sep = false;
};

class Arr {
public:
	Arr(std::ostream &out):out(out) {out << "[";}
	~Arr() {out << "]";}

	Arr &operator <<(const std::string_view &str) {
		if (sep) out << ",";
		sep = true;
		out << Str(str);
		return *this;
	}
	Arr &operator <<(double v) {
		if (sep) out << ",";
		sep = true;
		out << v;
		return *this;
	}
	Arr &operator <<(int v) {
		if (sep) out << ",";
		sep = true;
		out << v;
		return *this;
	}
	Arr &operator <<(bool v) {
		if (sep) out << ",";
		sep = true;
		out << (v?"true":"false");
		return *this;
	}
	Arr &operator <<(std::nullptr_t) {
		if (sep) out << ",";
		sep = true;
		out << "null";
		return *this;
	}
	Obj object() {
		if (sep) out << ",";
		sep = true;
		return Obj(out);
	}

	Arr array() {
		if (sep) out << ",";
		sep = true;
		return Arr(out);
	}

protected:
	std::ostream &out;
	bool sep = false;
};

Arr Obj::array(std::string_view k) {
	if (sep) out << ",";
	sep = true;
	out << Str(k) << ":";
	return Arr(out);

}

}

static std::string generateOpID(std::string path, std::string method) {
	std::string ret;
	ret.reserve(path.length()+method.length()+3);
	for (char c:path) if (isalnum(c)) ret.push_back(c); else ret.push_back('_');
	ret.push_back('_');
	for (char c:method) if (isalnum(c)) ret.push_back(c); else ret.push_back('_');
	return ret;

}

void OpenAPIServer::generateDef(std::ostream &out) {
	using namespace _undefined;
	Obj root(out);
	root("openapi","3.0.3");
	{
		Obj jinfo (root.object("info"));
		jinfo("description",info.description);
		jinfo("version",info.version);
		jinfo("title",info.title);
		jinfo("termsOfService",info.termsOfService);
		{
			Obj contact(jinfo.object("contact"));
			if (!info.contact_email.empty()) contact("email", info.contact_email);
			if (!info.contact_name.empty()) contact("name", info.contact_name);
			if (!info.contact_url.empty()) contact("url", info.contact_url);
		}

	}
	{
		Obj paths(root.object("paths"));
		for (const auto &p: this->paths) {
			Obj curpath( paths.object(p.path));
			if (p.GET.has_value()) serialize(curpath.object("get"), *p.GET, generateOpID(p.path, "get"));
			if (p.PUT.has_value()) serialize(curpath.object("put"), *p.PUT, generateOpID(p.path, "put"));
			if (p.POST.has_value()) serialize(curpath.object("post"), *p.POST, generateOpID(p.path, "post"));
			if (p.DELETE.has_value()) serialize(curpath.object("delete"), *p.DELETE, generateOpID(p.path, "delete"));
		}
	}
}

template<typename Sch>
void OpenAPIServer::serializeSchema(_undefined::Obj &&obj, const Sch &param) {
	using namespace _undefined;
	std::string_view type = param.type;
	obj("description", param.description);
	obj("title", param.name);
	if (type == "assoc") {
		obj("type","object");
		Obj prop (obj.object("additionalProperties"));
		Arr anyOf (prop.array("anyOf"));
		for (const auto &c: param.properties) {
			serializeSchema(anyOf.object(), c);
		}
		return;
	} else if (type == "anyOf" || type == "allOf" || type == "oneOf") {
		Arr a (obj.array(type));
		auto iter = std::find_if(param.properties.begin(), param.properties.end(),[](const SchemaItem &p){
			return p.type == "null";
		});
		if (iter != param.properties.end()) {
			obj("nullable",true);
			for (const auto &c: param.properties) {
				if (&c != &(*iter)) {
					serializeSchema(a.object(), c);
				}
			}
		} else {
			for (const auto &c: param.properties) {
				serializeSchema(a.object(), c);
			}
		}
		return;
	} else if (type == "int32") {
		obj("type","integer");
		obj("format","int32");
	} else if (type == "int64" || type == "integer") {
		obj("type","integer");
		obj("format","int64");
	} else if (type == "number" || type == "double") {
		obj("type","number");
		obj("format","double");
	} else if (type == "float") {
		obj("type","number");
		obj("format","float");
	} else if (type == "boolean") {
		obj("type","boolean");
	} else if (type == "string") {
		obj("type","string");
	} else if (type == "base64") {
		obj("type","string");
		obj("format","base64");
	} else if (type == "date") {
		obj("type","string");
		obj("format","date");
	} else if (type == "date-time") {
		obj("type","string");
		obj("format","date-time");
	} else if (type == "binary") {
		obj("type","string");
		obj("format","binary");
	} else if (type == "object") {
		obj("type","object");
		if (!param.properties.empty()) {
			Obj prop(obj.object("properties"));
			for (auto c: param.properties) {
				Obj n(prop.object(c.name));
				serializeSchema(std::move(n), c);
			}
		}
	} else if (type == "enum") {
		obj("type","string");
		{
			Arr arr (obj.array("enum"));
			for (auto c: param.properties) {
				arr << c.name;
			}
		}
	} else if (type == "array") {
		obj("type","array");
		Obj items(obj.object("items"));
		if (!param.properties.empty()) {
			if (param.properties.size()==1) {
				serializeSchema(std::move(items), param.properties[0]);
			} else {
				Arr anyOf(items.array("anyOf"));
				for (auto c: param.properties) {
					serializeSchema(anyOf.object(), c);
				}
			}
		}
	}
}


void OpenAPIServer::serialize(_undefined::Obj &&obj, const ParameterObject &param) {
	obj("name", param.name)
	   ("in", param.in)
	   ("required", param.required)
	   ("description", param.description);
	serializeSchema(obj.object("schema"), param);
}

void OpenAPIServer::serialize(_undefined::Obj &&obj, const OperationStruct &op, const std::string &opid) {
	using namespace _undefined;
	obj.array("tags") << op.tag;
	obj("summary", op.summary);
	obj("operationId", opid);
	if (!op.params.empty())
	{
		Arr params(obj.array("parameters"));
		for (const auto &p: op.params){
			serialize(params.object(),p);
		}
	}
	if (!op.requests.empty())
	{
		Obj rq(obj.object("requestBody"));
		rq("description", op.body_desc);
		rq("required",true);
		Obj ctn(rq.object("content"));
		for (const MediaObject &m: op.requests) {
			Obj n(ctn.object(m.content_type));
			serializeSchema(n.object("schema"), m);
		}
	}
	{
		Obj rsp(obj.object("responses"));
		for (const ResponseObject &r: op.responses) {
			Obj n(rsp.object(std::to_string(r.status_code)));
			n("description", r.description);
			if (!r.headers.empty()) {
				Obj hlist(n.object("headers"));
				for (const ParameterObject &p: r.headers) {
					Obj h(hlist.object(p.name));
					h("description", p.description);
					serializeSchema(h.object("schema"), p);
				}
			}
			if (!r.response.empty()) {
				Obj content(n.object("content"));
				for(const MediaObject &m: r.response) {
					Obj n(content.object(m.content_type));
					serializeSchema(n.object("schema"), m);
				}
			}
		}
	}
	if (!op.security) {
		obj.array("security");
	}
}

void OpenAPIServer::addSwagFilePath(const std::string &path) {
	addPath(path, [&](PHttpServerRequest &req, const std::string_view &) {

		req->setContentType("application/json");
		req->send(generateDef());
		return true;

	});
}

void OpenAPIServer::addSwagBrowser(const std::string &path) {	;
	addPath(path, [&](PHttpServerRequest &req, const std::string_view &p) {
		if (p.empty()) {
			return req->directoryRedir();
		}
		if (p != "/") return false;
		req->setContentType("text/html;charset=utf-8");
		req->send(R"html(<!-- HTML for static distribution bundle build -->
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="UTF-8">
    <title>Swagger UI</title>
    <link rel="stylesheet" type="text/css" href="https://petstore.swagger.io/swagger-ui.css" />
    <link rel="icon" type="image/png" href="https://petstore.swagger.io/favicon-32x32.png" sizes="32x32" />
    <link rel="icon" type="image/png" href="https://petstore.swagger.io/favicon-16x16.png" sizes="16x16" />
    <style>
      html
      {
        box-sizing: border-box;
        overflow: -moz-scrollbars-vertical;
        overflow-y: scroll;
      }

      *,
      *:before,
      *:after
      {
        box-sizing: inherit;
      }

      body
      {
        margin:0;
        background: #fafafa;
      }
    </style>
  </head>

  <body>
    <div id="swagger-ui"></div>

    <script src="https://petstore.swagger.io/swagger-ui-bundle.js" charset="UTF-8"> </script>
    <script src="https://petstore.swagger.io/swagger-ui-standalone-preset.js" charset="UTF-8"> </script>
    <script>
    window.onload = function() {
      // Begin Swagger UI call region
      const ui = SwaggerUIBundle({
        url: "swagger.json",
        dom_id: '#swagger-ui',
        deepLinking: true,
        presets: [
          SwaggerUIBundle.presets.apis,
          SwaggerUIStandalonePreset
        ],
        plugins: [
          SwaggerUIBundle.plugins.DownloadUrl
        ],
        layout: "StandaloneLayout"
      });
      // End Swagger UI call region

      window.ui = ui;
    };
  </script>
  </body>
</html>)html");
		return true;

	});
	addSwagFilePath(path+"/swagger.json");
}

bool OpenAPIServer::PathTreeItem::addHandler(Method m, std::string_view vpath, Handler &&h) {
				if (vpath.empty() || vpath[0] != '/' || vpath == "/") {
					has_handler = true;
					this->h[static_cast<int>(m)] = std::move(h);
					return true;
				} else {
					auto item = extractPath(vpath);
					if (!item.empty() && item[0] == '{' && item.back() == '}') {
						auto varname = item.substr(1,item.size()-2);
						auto iter = std::find_if(variables.begin(), variables.end(), [&](const auto &p){
							return p.first == varname;
						});
						if (iter != variables.end()) return iter->second.addHandler(m, vpath, std::move(h));
						else {
							auto cnt = variables.size();
							variables.push_back(std::pair(std::string(varname), PathTreeItem()));
							return variables[cnt].second.addHandler(m, vpath, std::move(h));
						}
					} else {
						auto iter = branches.find(item);
						if (iter != branches.end()) {
							return iter->second.addHandler(m, vpath, std::move(h));
						} else {
							return branches[std::string(item)].addHandler(m, vpath, std::move(h));
						}
					}
				}
			}

static std::string_view methodList[] = {
		"GET","PUT","POST","DELETE"
};

class OpenAPIServer::QueryParserWithVars: public QueryParser {
public:
	QueryParserWithVars(const VarList &vlist, std::string_view query);

protected:
	std::string buffer;


};

bool OpenAPIServer::execHandler(userver::PHttpServerRequest &req, const std::string_view &vpath) {
	VarList vars;
	std::string_view path, query;
	auto splt = vpath.find('?');
	if (splt == vpath.npos) {
		path = vpath;
	} else {
		path = vpath.substr(0,splt);
		query = vpath.substr(splt+1);
	}
	return root.findPath(path, vars, [&](const PathTreeItem &itm, const VarList &vars) {

		auto m = req->getMethod();
		auto iter = std::find(std::begin(methodList), std::end(methodList), m);
		if (iter != std::end(methodList)) {
			int idx = std::distance(std::begin(methodList),iter);
			if (itm.h[idx] != nullptr) {
				return itm.h[idx](req, QueryParserWithVars(vars, query));
			}
		}
		std::string methods;
		for (int i = 0; i < static_cast<int>(Method::count); i++) {
			if (itm.h[i] != nullptr) {
				methods.append(", ");
				methods.append(methodList[i]);
			}
		}
		if (!methods.empty()) {
			req->set("Allow", std::string_view(methods).substr(2));
		}
		req->sendErrorPage(405);
		return true;
	}) || HttpServer::execHandler(req, vpath);

}

inline OpenAPIServer::QueryParserWithVars::QueryParserWithVars(const VarList &vlist, std::string_view query) {
	parse(query, true);
	std::vector<std::pair<std::pair<int, int>,std::pair<int, int> > > varindex;
	for (const auto &v: vlist) {
		int l1 = buffer.length();
		buffer.append(v.first);
		int l2 = buffer.length();
		buffer.push_back(0);
		int l3 = buffer.length();
		this->urlDecode(v.second, buffer);
		int l4 = buffer.length();
		buffer.push_back(0);
		varindex.push_back({{l1,l2-l1},{l3,l4-l3}});
	}
	std::string_view bufview(buffer);
	if (!varindex.empty()) {
		for (const auto &d: varindex) {
			pmap.push_back({bufview.substr(d.first.first, d.first.second), bufview.substr(d.second.first, d.second.second)});
		}
		std::sort(pmap.begin(),pmap.end(),&orderItems);
	}
}

OpenAPIServer::PathInfo OpenAPIServer::PathInfo::handler(Handler &&handler) {
	if (methodIndex < 0) {
		throw std::runtime_error("OpenAPI annotation, handler without method: " + owner.paths[pathIndex].path);
	}
	else {
		owner.root.addHandler(static_cast<Method>(methodIndex), owner.paths[pathIndex].path, std::move(handler));
	}
	return *this;
}

OpenAPIServer::PathInfo userver::OpenAPIServer::PathInfo::operator >>(Handler &&h) {
	return handler(std::move(h));
}


}

