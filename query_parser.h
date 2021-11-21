#pragma once

#include <string_view>
#include <utility>
#include <vector>

#include "header_value.h"

namespace userver {

class RequestParams {
public:
	typedef std::pair<std::string_view, std::string_view> Item;
	typedef std::vector<Item> ParamMap;

	///Begin of the headers
	ParamMap::const_iterator begin() const;
	///End of the headers
	ParamMap::const_iterator end() const;

	///Retrieves header value
	HeaderValue operator[](std::string_view key) const;

	bool empty() const {return pmap.empty();}

	std::string_view getPath() const;

	static void urlDecode(const std::string_view &src, std::string &out);

protected:

	RequestParams();
	ParamMap pmap;
	std::string_view path;
	static bool orderItems(const Item &a, const Item &b);

};

class QueryParser: public RequestParams{

public:
	QueryParser() {}
	explicit QueryParser(std::string_view vpath);

	void clear();


	void parse(std::string_view vpath, bool postBody);


protected:
	std::vector<char> data;

};

///Parses query and items in the path
/**
 *
 *  /path/{item1}/{item2}
 *
 *  stores as item1 and item2
 */
class PathAndQueryParser:public QueryParser {
public:
	PathAndQueryParser(std::string_view vpath, const std::string_view &pathPattern);
	bool path_valid;
protected:
	std::vector<char> pathdata;

	bool parsePath(const std::string_view &pattern);

};

}





