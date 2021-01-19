/*
 * query_builder.h
 *
 *  Created on: 16. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_QUERY_BUILDER_H_
#define SRC_USERVER_QUERY_BUILDER_H_

#include <sstream>

namespace userver {

template<typename T>
struct BuildQueryItem {
public:
	void build(std::ostream &out, const T &data);
};

template<>
struct BuildQueryItem<std::pair<std::string_view, std::string_view> > {
public:
	static void build(std::ostringstream &out, const std::pair<std::string_view, std::string_view> &data) {
		send(out, data.first);
		out << '=';
		send(out, data.second);
	}

	static void send(std::ostringstream &out, const std::string_view &text) {
		for (unsigned char c: text) {
			if (std::isalnum(c) || c == '_' || c == '-') out << c;
			else {
				out << "%";
				out.width(2);
				out.fill('0');
				out << std::hex << static_cast<unsigned int>(c);
			}
		}
	}
};



template<typename T>
std::string buildQuery(const T &data) {
	auto iter = data.begin();
	auto iend = data.end();
	if (iter == iend) return std::string();
	std::ostringstream out;
	BuildQueryItem<typename std::remove_reference<decltype(*iter)>::type>::build(out, *iter);
	++iter;
	while (iter != iend) {
		out << '&';
		BuildQueryItem<typename std::remove_reference<decltype(*iter)>::type>::build(out, *iter);
		++iter;
	}
	return out.str();
}

template<typename T>
void BuildQueryItem<T>::build(std::ostream &out, const T &data) {
	auto iter = data.begin();
	auto iend = data.end();
	if (iter == iend) return;
	BuildQueryItem<std::pair<std::string_view, std::string_view> >::send(out, *iter);
	++iter;
	if (iter == iend) return;
	out << '=';
	BuildQueryItem<std::pair<std::string_view, std::string_view> >::send(out, *iter);
}

std::string buildQuery(const std::initializer_list<std::pair<std::string_view, std::string_view> > &data) {
	auto iter = data.begin();
	auto iend = data.end();
	if (iter == iend) return std::string();
	std::ostringstream out;
	BuildQueryItem<std::pair<std::string_view, std::string_view> >::build(out, *iter);
	++iter;
	while (iter != iend) {
		out << '&';
		BuildQueryItem<std::pair<std::string_view, std::string_view> >::build(out, *iter);
		++iter;
	}
	return out.str();
}

}

#endif /* SRC_USERVER_QUERY_BUILDER_H_ */
