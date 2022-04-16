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
#include <atomic>

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

#ifdef USERVER_CALLBACK_FASTALLOC

template<std::size_t sz> class Allocator {
public:

	Allocator():root(0),closed(false) {}
	~Allocator() {
		closed.store(true);
		Block *b = root.exchange(nullptr);
		while (b) {
			Block *c = b->next;
			delete b;
			b = c;
		}
	}

	static Allocator &getInstance() {
		static Allocator inst;
		return inst;
	}

	struct Block {
		Block *next;
		char data[sz];
	};

	void *alloc(std::size_t asz) {
		if (asz > sz) return ::operator new(asz);
		Block *t = root.exchange(nullptr);
		if (t == nullptr) {
			Block *b = new Block;
			return b->data;
		} else {
			Block *nx = t->next;
			Block *rest = root.exchange(nx);
			while (rest) {
				Block *z = rest->next;
				insert(rest);
				rest = z;
			}
			return t->data;
		}
	}

	void insert(Block *b) {
		if (closed.load()) delete b;
		else {
			Block *z = root;
			do {
				b->next = z;
			} while (!root.compare_exchange_strong(z, b));
		}
	}

	void free(void *data, std::size_t asz) {
		if (asz > sz) {
			::operator delete(data);
		} else {
			auto c = reinterpret_cast<char *>(data);
			c-=offsetof(Block,data);
			Block *b = reinterpret_cast<Block *>(c);
			insert(b);
		}
	}
private:
	std::atomic<Block *> root;
	std::atomic<bool> closed;
};

template<typename X> using AllocatorT = Allocator<((sizeof(X)+2*sizeof(int)-1)/(2*sizeof(int)))*2*sizeof(int)>;



template<typename T>
class AllocBase {
public:



	void *operator new(std::size_t sz) {
		return AllocatorT<T>::getInstance().alloc(sz);

	}
	void operator delete(void *ptr, std::size_t sz) {
		return AllocatorT<T>::getInstance().free(ptr, sz);
	}

};
#else
template<typename T> class AllocBase {}; //empty - no allocator involved
#endif

template<typename Ret, typename ... Args> class CallbackT<Ret(Args...)> {
public:
	using CBIfc = ICallbackT<Ret(Args...)>;

	template<typename Fn>
	class Call1: public CBIfc, public AllocBase<CallbackT<Ret(Args...)>::Call1<Fn> > {
	public:
		Call1(Fn &&fn):fn(std::forward<Fn>(fn)) {}
		virtual Ret invoke(Args ... args) const override {
			return fn(std::forward<Args>(args)...);
		}
		~Call1() {

		}
	protected:
		mutable std::remove_reference_t<Fn> fn;

	};

	template<typename Fn, typename CancelCallback>
	class Call2: public CBIfc, public AllocBase<CallbackT<Ret(Args...)>::Call2<Fn,CancelCallback> > {
	public:
		Call2(Fn &&fn, CancelCallback &&cfn):fn(std::forward<Fn>(fn)),cfn(std::forward<CancelCallback>(cfn)) {}
		virtual Ret invoke(Args ... args) const override {
			called = true;
			return fn(std::forward<Args>(args)...);
		}
		~Call2() {
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

	template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<Args>()...))>
	CallbackT(Fn &&fn) {
		ptr = std::make_unique<Call1<Fn> >(std::forward<Fn>(fn));
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
		ptr = std::make_unique<Call2<Fn, CancelCallback> >(std::forward<Fn>(fn), std::forward<CancelCallback>(cfn));
	}

	CallbackT():ptr(nullptr) {}
	CallbackT(std::nullptr_t):ptr(nullptr) {}
	explicit CallbackT(std::unique_ptr<CBIfc> &&ptr):ptr(std::move(ptr)) {}
	bool operator==(std::nullptr_t) const {return ptr == nullptr;}
	bool operator!=(std::nullptr_t) const {return ptr != nullptr;}
	CallbackT(CallbackT &&other):ptr(std::move(other.ptr)) {}
	CallbackT &operator=(CallbackT &&other) {
		if (&other == this) return *this;
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

