/*
 * header_value.h
 *
 *  Created on: 16. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_HEADER_VALUE_H_
#define SRC_USERVER_HEADER_VALUE_H_

namespace userver {

class HeaderValue: public std::string_view {
public:
	HeaderValue(const std::string_view &s):std::string_view(s) {}
	HeaderValue():defined(false) {};
	const bool defined = true;
	std::size_t getUInt() const;



	template<typename Fn>
	void enumValues(Fn &&fn, const std::string_view &sep = ",") {
		std::string_view me = *this;
		while (!me.empty()) {
			std::string_view p = splitAt(sep, me);
			trim(p);
			fn(p);
		}
	}
	static std::string_view splitAt(const std::string_view &at, std::string_view &where);
	static void trim(std::string_view &what);


	static bool lessHeader(const std::pair<std::string_view, std::string_view> &a,
			const std::pair<std::string_view, std::string_view> &b);
	static bool iequal(const std::string_view &a, const std::string_view &b);
};


}
#endif /* SRC_USERVER_HEADER_VALUE_H_ */
