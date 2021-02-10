/*
 * helpers.h
 *
 *  Created on: 13. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MINISERVER_HELPERS_H_
#define SRC_MINISERVER_HELPERS_H_
#include <type_traits>
#include <algorithm>
#include <memory>
#include <string_view>
#include <stdexcept>
#include <cctype>

namespace userver {

template<typename U, typename T>
T splitAt(const U &search, T &object) {
	auto s = T(search);
	auto l = object.size();
	auto sl = s.size();
	auto k = object.find(s);
	if (k > l) {
		T ret = object;
		object = object.substr(l);
		return ret;
	} else {
		T ret = object.substr(0,k);
		object = object.substr(k+sl);
		return ret;
	}
}


template<typename T>
T splitAtIndex(std::size_t index, T &object) {
	auto l = object.size();
	if (index>=l) {
		T ret = object;
		object = object.substr(index);
		return ret;
	} else {
		T ret = object.substr(0,index);
		object = object.substr(index);
		return ret;
	}
}

template<typename Fn> class ICallbackT;
template<typename Fn> class CallbackT;

template<typename Ret, typename ... Args> class ICallbackT<Ret(Args...)> {
public:
	virtual Ret invoke(Args ... args) const = 0;
	virtual ~ICallbackT() {}
};

template<typename Ret, typename ... Args> class CallbackT<Ret(Args...)> {
public:
	using CBIfc = ICallbackT<Ret(Args...)>;


	template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<Args>()...))>
	CallbackT(Fn &&fn) {
		class Impl: public CBIfc {
		public:
			Impl(Fn &&fn):fn(std::forward<Fn>(fn)) {}
			virtual Ret invoke(Args ... args) const override {
				return fn(std::forward<Args>(args)...);
			}
			~Impl() {

			}
		protected:
			mutable std::remove_reference_t<Fn> fn;
		};
		ptr = std::make_unique<Impl>(std::forward<Fn>(fn));
	}
	CallbackT():ptr(nullptr) {}
	CallbackT(std::nullptr_t):ptr(nullptr) {}
	bool operator==(std::nullptr_t) const {return ptr == nullptr;}
	bool operator!=(std::nullptr_t) const {return ptr != nullptr;}
	CallbackT(CallbackT &&other):ptr(std::move(other.ptr)) {}
	CallbackT &operator=(CallbackT &&other) {
		ptr = std::move(other.ptr);return *this;
	}
	Ret operator()(Args ... args) const  {
		return ptr->invoke(std::forward<Args>(args)...);
	}

protected:
	std::unique_ptr<CBIfc> ptr;
};

template<typename T, std::size_t n>
class SmallVector {
public:
	SmallVector ():_size(0) {}
	bool empty() const {return _size == 0;}
	std::size_t size() const {return _size;}
	void push_back(T &&item) {
		if (_size >= n) throw std::runtime_error("SmallVector: No room to store item");
		new(getItemPtr(_size)) T(std::move(item));
		_size++;
	}
	T *begin() {return getItemPtr(0);}
	T *end() {return getItemPtr(_size);}
	const T *begin()  const {return getItemPtr(0);}
	const T *end() const {return getItemPtr(_size);}
	void erase(const T *where) {
		std::size_t idx = where - getItemPtr(0);
		getItemPtr(idx)->~T();
		auto idx1 = idx+1;
		while (idx1 < _size) {
			T *nxt = getItemPtr(idx1);
			new (getItemPtr(idx)) T(std::move(*nxt));
			nxt->~T();
			idx = idx1;
			idx1 = idx+1;
		}
		_size--;
	}

protected:
	std::size_t _size;
	unsigned char _data[sizeof(T)*n];
	T *getItemPtr(std::size_t pos) {return reinterpret_cast<T *>(_data)+pos;}
	const T *getItemPtr(std::size_t pos) const {return reinterpret_cast<const T *>(_data)+pos;}

};

inline void trim(std::string_view &x) {
	while (!x.empty() && std::isspace(x[0])) x = x.substr(1);
	while (!x.empty() && std::isspace(x[x.length()-1])) x = x.substr(0, x.length()-1);
}


}

#endif /* SRC_MINISERVER_HELPERS_H_ */

