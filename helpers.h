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
		protected:
			mutable std::remove_reference_t<Fn> fn;
		};
		ptr = std::make_unique<Impl>(std::forward<Fn>(fn));
	}
	CallbackT():ptr(nullptr) {}
	CallbackT(nullptr_t):ptr(nullptr) {}
	bool operator==(nullptr_t) const {return ptr == nullptr;}
	bool operator!=(nullptr_t) const {return ptr != nullptr;}
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


#endif /* SRC_MINISERVER_HELPERS_H_ */
