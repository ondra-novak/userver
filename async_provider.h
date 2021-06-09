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

namespace userver {

class AsyncResource;

class IAsyncProvider {
public:

	///Specify callback form
	/**
	 * The function sets argument to true, if the asynchronous operation completed successfuly, or false, if the operation failed. You
	 * can use std::current_exception to capture the error. If there is no exception, it failed because timeout.
	 *
	 *
	 */
	using Callback = CallbackT<void(bool)>;

	///run asynchronously
	/**
	 * @param res asynchronous resource
	 * @param cb callback
	 * @param timeout timeout as absolute point in time. To set "no timeout", use system_clock::time_point::max
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

	///Stops async provider - for example if you need to exit server
	virtual void stop() = 0;

	///Returns true, if async provider has been stopped
	virtual bool stopped() const = 0;

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
	auto yield_until(Pred &&pred) -> decltype(!pred()) {
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

	bool stopped() const {
		return get()->stopped();
	}
	///Installs a signal handler
	/** This allows to stop asynchronous provider on signal SIGTERM and SIGINT
	* Only one asynchronous provider can has this functionality. If this function
	* called on different provider, it replaces current registration
	*/
	void stopOnSignal();

	void stop() {
		return get()->stop();
	}
};

enum class AsyncProviderType {
	poll,
	epoll
};

///Create asynchronous provider with specified dispatchers
/**
 * @param dispatchers count of active dispatchers. You will need at least this count of threads.
 * @return asynchronous provider
 *
 * @note you need to create thread and call start_thread for each thread.
 */
AsyncProvider createAsyncProvider(unsigned int dispatchers = 1, AsyncProviderType type = AsyncProviderType::epoll);

void setCurrentAsyncProvider(AsyncProvider aprovider);

void setThreadAsyncProvider(AsyncProvider aprovider);

AsyncProvider getCurrentAsyncProvider();

std::optional<AsyncProvider> getCurrentAsyncProvider_NoException();

///Store current exception - can be rethrow later
/** useful to store exceptions caught in destructor */
void storeException();

///Rethrows stored exception
/** rethrows any stored exception */
void rethrowStoredException();

}

#endif /* SRC_MINISERVER_ASYNC_PROVIDER_H_ */




