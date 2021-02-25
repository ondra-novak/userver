#include "query_parser.h"
#include "query_builder.h"

#include <algorithm>

namespace userver {


QueryParser::QueryParser(std::string_view vpath) {
	parse(vpath, false);
}

QueryParser::ParamMap::const_iterator QueryParser::begin() const {
	return pmap.begin();
}

QueryParser::ParamMap::const_iterator QueryParser::end() const {
	return pmap.end();
}

HeaderValue QueryParser::operator [](std::string_view key) const {
	Item srch;
	srch.first = key;
	auto itr = std::lower_bound(pmap.begin(), pmap.end(), srch, &orderItems);
	if (itr == pmap.end() || itr->first != key) return HeaderValue();
	else return HeaderValue(itr->second);
}

void QueryParser::clear() {
}

typedef std::pair<std::size_t, std::size_t> Part;
typedef std::pair<Part, Part> KeyValuePart;

static inline int fromHexDigit(char c) {
	return c>='A' && c <='F'?c-'A'+10
			:c>='a' && c <='f'?c-'a'+10
			:c>='0' && c <= '9'?c-'0'
			:0;
}

void QueryParser::parse(std::string_view vpath, bool postBody) {

	enum State {
		readingPath,
		readingKey,
		readingValue,
		readingSpecChar1,
		readingSpecChar2
	};


	clear();
	std::vector<KeyValuePart> parts;
	std::vector<char> data;

	Part pathPart;
	Part keyPart;
	State state =postBody?readingKey:readingPath;
	State nxstate = state;


	auto iter = vpath.begin();
	auto e = vpath.end();
	std::size_t mark = data.size();
	int specCharBuff = 0;

	auto wrfn = [&](char c) {
		data.push_back(c);
	};

	auto commitData = [&] {
		Part p(mark, data.size() - mark);
		data.push_back((char)0);
		mark = data.size();
		return p;
	};


	while (iter != e) {
		char c = *iter++;

		if (c == '+') {
			wrfn(' ');
		} else if (c == '%' && state != readingPath) {
			nxstate = state;
			state = readingSpecChar1;
		} else {
			switch (state) {
			case readingPath:
				if (c == '?') {
					pathPart = commitData();
					state = readingKey;
				} else{
					wrfn(c);
				}
				break;
			case readingKey:
				if (c == '&') {
					keyPart = commitData();
					parts.push_back(KeyValuePart(keyPart,Part(0,0)));
				} else if (c == '=') {
					keyPart = commitData();
					state = readingValue;
				} else {
					wrfn(c);
				}
				break;
			case readingValue:
				if (c == '&') {
					Part v = commitData();
					parts.push_back(KeyValuePart(keyPart, v));
					state = readingKey;
				} else {
					wrfn(c);
				}
				break;
			case readingSpecChar1:
				specCharBuff = fromHexDigit(c) * 16;
				state = readingSpecChar2;
				break;
			case readingSpecChar2:
				specCharBuff |= fromHexDigit(c) ;
				wrfn((char)specCharBuff);
				state = nxstate;
				break;
			}

		}
	}

	if (state == readingSpecChar1 || state == readingSpecChar2) {
		state = nxstate;
	}

	switch (state) {
	case readingPath:
		pathPart = commitData();
		break;
	case readingKey:
		keyPart = commitData();
		parts.push_back(KeyValuePart(keyPart,Part(0,0)));
		break;
	case readingValue: {
			Part v = commitData();
			parts.push_back(KeyValuePart(keyPart, v));
		}
		break;

	default:
		break;
	}

	pmap.clear();
	pmap.reserve(parts.size());
	for (auto &&x: parts) {
		std::string_view key (data.data()+x.first.first, x.first.second);
		std::string_view value (data.data()+x.second.first, x.second.second);
		pmap.push_back(Item(key,value));
	}
	path = std::string_view(data.data()+pathPart.first,pathPart.second);
	std::sort(pmap.begin(),pmap.end(),&orderItems);
	std::swap(this->data, data);
}

std::string_view QueryParser::getPath() const {
	return path;
}

bool QueryParser::orderItems(const Item& a, const Item& b) {
	return a.first < b.first;
}

void QueryParser::urlDecode(const std::string_view &src, std::string &out) {
	int state = 0;
	int numb = 0;
	for (char c: src) {
		switch (state) {
		case 0: if (c == '+') out.push_back(' ');
				else if (c == '%') {state = 1; numb = 0;}
				else out.push_back(c);
				break;
		case 1: numb = fromHexDigit(c) * 16;
				state = 2;
				break;
		case 2: numb = fromHexDigit(c) + numb;
				out.push_back(static_cast<char>(numb));
				state = 0;
		}
	}
}

}

