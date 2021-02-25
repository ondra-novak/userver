#pragma once

#include <string_view>
#include <utility>
#include <vector>

#include "header_value.h"

namespace userver {

class QueryParser{

public:
	QueryParser() {}
	explicit QueryParser(std::string_view vpath);

	typedef std::pair<std::string_view, std::string_view> Item;
	typedef std::vector<Item> ParamMap;

	///Begin of the headers
	ParamMap::const_iterator begin() const;
	///End of the headers
	ParamMap::const_iterator end() const;

	///Retrieves header value
	HeaderValue operator[](std::string_view key) const;

	void clear();

	bool empty() const {return pmap.empty();}

	void parse(std::string_view vpath, bool postBody);

	std::string_view getPath() const;

	static void urlDecode(const std::string_view &src, std::string &out);

protected:
	ParamMap pmap;
	std::string_view path;
	std::vector<char> data;

	static bool orderItems(const Item &a, const Item &b);

};

}





