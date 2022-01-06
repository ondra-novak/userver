/*
 * callback.h
 *
 *  Created on: 6. 11. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_CALLBACK_H_
#define SRC_USERVER_CALLBACK_H_
#include <cassert>
#include <typeinfo>

using std::type_info;

namespace userver {

///Callback function - replaces std::function, allows to move clousure without need to have copy constructor

template<typename Fn> class CallbackT;
template<typename Fn> class ICallbackT;

///Callback function - replaces std::function, allows to move clousure without need to have copy constructor
template<typename Prototype>
using Callback = CallbackT<Prototype>;


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

	///Creates callback function which allows to defined reaction for situation when  callback is not called
	/**
	 * @param fn function to call
	 * @param cfn function called when callback is destroyed without calling
	 */
	template<typename Fn, typename CancelCallback,
				typename = decltype(std::declval<Fn>()(std::declval<Args>()...)),
				typename = decltype(std::declval<CancelCallback>()())>
	CallbackT(Fn &&fn, CancelCallback &&cfn) {
		class Impl: public CBIfc {
		public:
			Impl(Fn &&fn, CancelCallback &&cfn):fn(std::forward<Fn>(fn)),cfn(std::forward<CancelCallback>(cfn)) {}
			virtual Ret invoke(Args ... args) const override {
				called = true;
				return fn(std::forward<Args>(args)...);
			}
			~Impl() {
				if (!called) {
					try {
						cfn();
					} catch (...) {

					}
				}

			}
		protected:
			mutable std::remove_reference_t<Fn> fn;
			mutable std::remove_reference_t<CancelCallback> cfn;
			mutable bool called = false;
		};
		ptr = std::make_unique<Impl>(std::forward<Fn>(fn), std::forward<CancelCallback>(cfn));
	}

	CallbackT():ptr(nullptr) {}
	CallbackT(std::nullptr_t):ptr(nullptr) {}
	explicit CallbackT(std::unique_ptr<CBIfc> &&ptr):ptr(std::move(ptr)) {}
	bool operator==(std::nullptr_t) const {return ptr == nullptr;}
	bool operator!=(std::nullptr_t) const {return ptr != nullptr;}
	CallbackT(CallbackT &&other):ptr(std::move(other.ptr)) {}
	CallbackT &operator=(CallbackT &&other) {
		ptr = std::move(other.ptr);return *this;
	}
	Ret operator()(Args ... args) const  {
		if (ptr == nullptr) throw std::runtime_error(std::string("Callback is not callable: ").append(typeid(CBIfc).name()));
		return ptr->invoke(std::forward<Args>(args)...);
	}
	void reset() {
		ptr = nullptr;
	}

protected:
	std::unique_ptr<CBIfc> ptr;

};

///Used to throw exception in callback, if the callback is called as response to an error
/**
 * @code
 * stream.read() >> [=](std::string_view data) {
 * 	try {
 * 		cb_rethrow(); // throw any pending error
 * 		//... continue processing data
 * 	} catch (std::exception &e) {
 * 		//handle error here
 * 	}
 * }
 * @endcode
 */
static inline void cb_rethrow() {
	auto exp = std::current_exception();
	if (exp != nullptr) {
		std::rethrow_exception(exp);
	}
}


}



#endif /* SRC_USERVER_CALLBACK_H_ */
