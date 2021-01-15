/*
 * async_provider.h
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */

#include <type_traits>
#include <chrono>
#include <functional>
#include <memory>
#include "helpers.h"

#ifndef SRC_MINISERVER_ASYNC_PROVIDER_H_
#define SRC_MINISERVER_ASYNC_PROVIDER_H_


class AsyncResource;

class IAsyncProvider {
public:

	using Callback = CallbackT<void(bool)>;

	///run asynchronously
	/**
	 * @param res asynchronous resource
	 * @param cb callback
	 * @param timeout timeout
	 */
	virtual void runAsync(const AsyncResource &res, Callback &&cb,const std::chrono::system_clock::time_point &timeout) = 0;
	///Run asynchronously
	/**
	 * @param cb callback to run - it is executed with argument true
	 *
	 * Useful to move execution to different thread
	 */
	virtual void runAsync(Callback &&cb) = 0;

	///yield execution in favor to process single asynchronou task and exit
	/**
	 * @retval true success
	 * @retval false asynchronous provider has been stopped (probably is being destroyed)
	 */
	virtual bool yield() = 0;

	virtual void stop() = 0;


	virtual ~IAsyncProvider() {}


};

class AsyncProvider: public std::shared_ptr<IAsyncProvider> {
public:

	using std::shared_ptr<IAsyncProvider>::shared_ptr;

	///yield execution in favor to process single asynchronou task and exit
	/**
	 * @retval true success
	 * @retval false asynchronous provider has been stopped (probably is being destroyed)
	 */
	bool yield() {
		return get()->yield();
	}

	///Yield execution until condition is met
	/**
	 * @param pred predicate to test condition
	 * @retval true success
	 * @retval false asynchronous provider has been stopped (probably is being destroyed)
	 */
	template<typename Pred>
	auto yield_until(Pred &&pred) -> typename std::is_invocable_r<bool, Pred, bool()>::value {
		while (!pred()) {
			if (!yield()) return false;
		}
		return true;
	}

	///Runs thread to execute asynchronous tasks, returns when asynchronous provider is stopped
	/** Note - any exception thrown during processing are thrown out of this function. You
	 * need to catch exceptions and process them and eventually call this function again
	 */
	void start_thread() {
		while (yield());
	}

	template<typename Fn>
	void runAsync(const AsyncResource &res, Fn &&fn, const std::chrono::system_clock::time_point &timeout)  {
		get()->runAsync(res, IAsyncProvider::Callback(std::forward<Fn>(fn)), timeout);
	}

	template<typename Fn>
	void runAsync(Fn &&fn) {
		get()->runAsync([fn = std::forward<Fn>(fn)](bool) mutable {fn();});
	}
};

///Create asynchronous provider with specified dispatchers
/**
 * @param dispatchers count of active dispatchers. You will need at least this count of threads.
 * @return asynchronous provider
 *
 * @note you need to create thread and call start_thread for each thread.
 */
AsyncProvider createAsyncProvider(unsigned int dispatchers = 1);

void setCurrentAsyncProvider(AsyncProvider aprovider);

void setThreadAsyncProvider(AsyncProvider aprovider);

AsyncProvider getCurrentAsyncProvider();

#endif /* SRC_MINISERVER_ASYNC_PROVIDER_H_ */



