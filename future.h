#include <atomic>

#include "callback.h"
#include <condition_variable>
#include <mutex>

namespace userver {

///Informs observer about type of execution
enum class CallType {
	///Callback is executed in different context (asynchronously)
	async,
	///Callback is executed in current context (synchronously)
	sync
};

///Future variable
/**
 * Future variable has no value until it is set. Any thread can observe the future and get notified once the future
 * value is set. Future can be set only once, and already set value can't be changed.
 *
 * Future's value can consists from multiple items of different type like a tuple. All items must be set at
 * once. The observing thread then receive all items through the callback
 *
 * Future's instance is unmovable object. If you need to share or move the Future instance, you need to create the
 * instance through make_unique or make_shared (the apropriate smart pointer is the result)
 *
 * Future's instance not fully MT-safe. Please check note on each function whether need to be synchronized.
 *
 */
template<typename ... Types>
class Future: public AllocBase<Future<Types...> > {
public:

	///Definition of an type, which holds the value
	using Tuple = std::tuple<Types...>;

	///Inicialize empty future
	/** If you need to share the future, make it shared (std::make_shared) */
	Future();
	///Destroy future
	~Future();


	//Future cannot be moved or copied, You need to use uinque_ptr or shared_ptr to move reference
	Future(const Future &) = delete;
	Future(Future &&) = delete;
	void operator=(const Future &) = delete;
	void operator=(Future &&) = delete;

	///Register observer callback
	/**
	 * @param callback function. The callback function can have zero arguments or exact count of arguments specified
	 * by Future type. There can be one extra argument at first position, the argument of type CallType
	 *
	 * @code
	 * f>>[=]();
	 * f>>[=](CallType ct);
	 * f>>[=](const Types &... args);
	 * f>>[=](CallType ct, const Types &... args);
	 * @endcode
	 *
	 * @note MT-Safe - multiple threads can call this operator without additional synchronozation
	 */
	template<typename Fn>
	void operator>>(Fn &&fn) const;

	///Sets value of the future
	/**
	 * @param val all values are set at once. Function can be called only once, additional calls are ignored
	 *
	 * @note MT-Unsafe - only one thread can call this function
	 */
	void set(const Types & ... val);

	///Sets value of the future
	/**
	 * @param val all values are set at once. Function can be called only once, additional calls are ignored
	 *
	 * @note MT-Unsafe - only one thread can call this function
	 */
	void set(Types &&... val);

	///Returns status of future
	/**
	 * @retval true future has already value
	 * @retval false future was not resolved
	 *
	 * @note MT-Safe
	 */
	bool has_value() const;
	///Returns status of future
	/**
	 * @retval true future was not resolved
	 * @retval false future has already value
	 *
	 * @note MT-Safe
	 *
	 */
	bool operator!() const;

	///Retrieves future's value (as tuple)
	/**
	 * Function is synchronous. If the future is not ready yet, function blocks execution
	 *
	 * Function is optimized to be called on already resolved future.
	 *
	 *  @note MT-Safe
	 *
	 */
	const Tuple &get() const;



	CallbackT<void(Types...)> promise() ;


	///Creates shared future
	/**
	 * Shared future can be shared between threads. Last reference destroyes the future
	 */
	static std::shared_ptr<Future<Types...> > make_shared() {return std::make_shared<Future<Types...> >();}

	///Creates unique future
	/**
	 * Unique future can be moved through contextes (because stack base Future cannot),
	 */
	static std::unique_ptr<Future<Types...> > make_unique() {return std::make_unique<Future<Types...> >();}

	template<typename Fn>
	friend void operator >> (std::unique_ptr<Future<Types...> > &f, Fn &&fn) {
		Future<Types...> &x = *f;
		x >> [f = std::move(f), fn = std::move(fn)](Types ... t) mutable {
			(void)f; //mark referenced
			fn(t...);
		};
	}
	template<typename Fn>
	friend void operator >> (const std::shared_ptr<Future<Types...> > &f, Fn &&fn) {
		Future<Types...> &x = *f;
		x >> [f, fn = std::move(fn)](Types ... t) mutable {
			(void)f; //mark referenced
			fn(t...);
		};
	}

protected:


	template<typename Fn>
	static inline auto cbcall(Fn &&fn, CallType ct, const Tuple &t)
					  -> decltype( std::declval<Fn>() ()) {
		return fn();
	}
	template<typename Fn>
	static inline auto cbcall(Fn &&fn, CallType ct, const Tuple &)
					  -> decltype( std::declval<Fn>() (CallType::async)) {
		return fn(ct);
	}
	template<typename Fn>
	static inline auto cbcall(Fn &&fn, CallType ct, const Tuple &t)
						-> decltype( std::declval<Fn>() (std::declval<Types>()...)) {
		return std::apply(std::forward<Fn>(fn), t);
	}
	template<typename Fn>
	static inline auto cbcall(Fn &&fn, CallType ct, const Tuple &t)
						-> decltype( std::declval<Fn>() (CallType::async, std::declval<Types>()...)) {
		return std::apply([&](const Types & ... args){
			return fn(ct,args...);
		},t);
	}



	class AbstractAction {
	public:
		virtual void resolved(CallType ct, const std::tuple<Types...> &val) noexcept = 0;
		virtual ~AbstractAction() {}

		AbstractAction *next;

	};




	//union is not initialized until the value is resolved
	//cleanup is handled by destructor
	union {
		Tuple value;
	};
	std::atomic<bool> resolved;
	mutable std::atomic<AbstractAction *> callbacks;

	class Promise: public ICallbackT<void(Types ...)> {
	public:

		void *operator new(std::size_t);  //not defined, should not be used
		void *operator new(std::size_t, void *p) {return p;}
		void operator delete(void *x, void *p) {}
		void operator delete(void *ptr, std::size_t) {} //empty, when unique PTR tries to call delete
		Promise(Future &owner):owner(owner) {}
		virtual void invoke(Types ... args) const {
			owner.set(args ...);
		}
		Future &owner;
	};

	char promiseReserved[sizeof(Promise)];

	void flushCallbacks(AbstractAction *me) const;

	template<typename Fn>
	class CB: public AbstractAction, public AllocBase<Future<Types...>::CB<Fn>> {
	public:
		CB(Fn &&fn):fn(std::forward<Fn>(fn)) {}
		virtual void resolved(CallType ct, const Tuple &val) noexcept {
			cbcall(std::forward<Fn>(fn), ct, val);
		}
		std::remove_reference_t<Fn> fn;
	};


};



template<typename ... Types>
inline Future<Types...>::Future()
:resolved(false),callbacks(nullptr)
{
}

template<typename ...Types>
inline Future<Types...>::~Future() {
	AbstractAction *z = nullptr;
	z = callbacks.exchange(z);
	while (z) {
		AbstractAction *c = z;
		z = z->next;
		delete c;
	}
	if (resolved.load()) {
		value.~Tuple();
	}
}

template<typename ... Types>
template<typename Fn>
inline void Future<Types...>::operator >>(Fn &&fn) const {
	if (resolved.load()) {
		cbcall(std::forward<Fn>(fn), CallType::sync, value);
	} else {

		CB<Fn> *x = new CB<Fn>(std::forward<Fn>(fn));
		AbstractAction *nx = nullptr;
		do {
			x->next = nx;
		} while (!callbacks.compare_exchange_weak(nx,x));
		if (resolved.load()) {
			flushCallbacks(x);
		}
	}
}

template<typename ... Types>
inline void Future<Types...>::set(Types && ... val) {
	if (!resolved.load()) {
		new(&value) Tuple(std::make_tuple<Types ...>(std::move(val)...));
		resolved.store(true);
		flushCallbacks(nullptr);
	}
}

template<typename ... Types>
inline bool Future<Types ...>::has_value() const {
	return resolved.load();
}

template<typename ... Types>
inline bool Future<Types ...>::operator !() const {
	return !resolved.load();
}

template<typename ... Types>
CallbackT<void(Types...)> Future<Types...>::promise() {
	Promise *p = new(promiseReserved) Promise(*this);
	return CallbackT<void(Types...)>(std::unique_ptr<ICallbackT<void(Types...)> >(p));
}
template<typename ... Types>
const typename Future<Types...>::Tuple& Future<Types...>::get() const {
	if (resolved.load()) return value;
	std::mutex mx;
	std::condition_variable cond;
	bool res = false;
	(*this)>>[&]{
		std::unique_lock _(mx);
		res = true;
		cond.notify_all();
	};
	std::unique_lock _(mx);
	cond.wait(_,[&]{return res;});
	return value;
}



template<typename ... Types>
void Future<Types...>::flushCallbacks(AbstractAction *me) const {
	AbstractAction *z = nullptr;
	const Tuple &val = value;
	z = callbacks.exchange(z);
	while (z) {
		auto c = z;
		z = z->next;
		c->resolved(c != me?CallType::async:CallType::sync, val);
		delete c;
	}
}

template<typename ... Types>
inline void Future<Types...>::set(const Types & ... val) {
	if (!resolved.load()) {
		new(&value) Tuple(std::make_tuple<const Types &...>(val...));
		resolved.store(true);
		flushCallbacks(nullptr);
	}
}

}

