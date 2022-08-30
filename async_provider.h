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
#include "idispatcher.h"
#include <optional>

#ifndef SRC_MINISERVER_ASYNC_PROVIDER_H_
#define SRC_MINISERVER_ASYNC_PROVIDER_H_

namespace userver {


///Abstract asynchronous resource
/**
 * Resources are tied to dispatchers. Asynchronous provider can handle resource passing
 * it to the appropriate dispatcher. Types of resources are not directly visible on public
 * interface because they can be platform depend.
 *
 * This also allows to implement own dispatchers and their asynchronous resources
 */
class IAsyncResource {
public:
    ///just destructor, we use RTTI
    virtual ~IAsyncResource() {}
};




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


	using Action = CallbackT<void()>;
	///run asynchronously
	/**
	 * @param res asynchronous resource
	 * @param cb callback
	 * @param timeout timeout as absolute point in time. To set "no timeout", use system_clock::time_point::max
	 */
	virtual void runAsync(IAsyncResource &&res, Callback &&cb,const std::chrono::system_clock::time_point &timeout) = 0;
	///Run asynchronously
	/**
	 * @param cb callback to run - it is executed with argument true
	 *
	 * Useful to move execution to different thread
	 */
	virtual void runAsync(Action &&cb) = 0;


	///Run as worker
	/**
	 * Converts this thread to a worker which helps to manage dispatchers and tasks. Function
	 * returns after single task is carried on or after the asynchronous provider is stopped
	 *
	 * @retval true there is work to do, you can call the function again
	 * @retval false async provider has been stopped, do not call the function again
	 *
	 * @note function blocks current thread
	 */
	virtual bool worker() = 0;


	///Stops async provider - for example if you need to exit server
	virtual void stop() = 0;

	///Returns true, if async provider has been stopped
	virtual bool stopped() const = 0;


	///Add dispatcher to the provider
	/**
	 * @param dispatcher pointer to dispatcher
	 *
	 * @note You cannot remove dispatchers. If you need to change dispatchers,
	 * you need to recreate the whole provider
	 */
	virtual void addDispatcher(PDispatch &&dispatcher) = 0;

	///Retrieve current count of dispatchers.
	virtual std::size_t getDispatchersCount() const = 0;
    ///Stops asynchronous waiting
    /**
     * @param resource description of asynchronous resource to stop waiting. The type
     * and content of the description depends on type of the asynchronous resource. Not
     * all resources are supported
     * @param signal_timeout set true to execute associated callback and passing timeout signal
     * to it. Set this to false to drop waiting without calling the callback
     *
     * @retval true successfully done
     * @retval false resource was not found.
     *
     * @note The operation can be executed asynchronously. Then the return value true
     * indicates, that operation successfully started. There is currently no way to
     * determine, whether the operation is running synchronous or asynchronous.
     * In synchronous mode, the current thread can be used to execute the callback associated
     * with the resource
     *
     * @note The function returns false if the operation is not supported for the given
     * resource.
     */
    virtual bool stopWait(IAsyncResource &&resource, bool signal_timeout) = 0;

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
	bool worker() {
		return get()->worker();
	}

	///Yield execution until condition is met
	/**
	 * @param pred predicate to test condition
	 * @retval true success
	 * @retval false asynchronous provider has been stopped (probably is being destroyed)
	 */
	template<typename Pred>
	auto workUntil(Pred &&pred) -> decltype(!pred()) {
		while (!pred()) {
			if (!worker()) return false;
		}
		return true;
	}

	///Runs current thread as worker
	/**
	 * Function call worker() in cycle until the provider is stopped.
	 *
	 * @note Function can throw exception. Exception can be rethrow from other thread which is unable
	 * to throw exception
	 */
	void runAsWorker();

    ///Adds thread to the provider
    /**
     * Function exits immediately and new thread is started at background
     *
     * @note this thread doesn't handle exceptions. Exceptions are stored and pickup by thread
     * which is able to throw exception
     *
     * @note if you need to remove thread, you must do it from inside through runAsync(). See stopThread()
     */
	void addThread();

    ///Signals to this thread to exit.
    /**
     * You can signal to thread which was create by addThread(). Otherwise, function fails
     * and returns false as result
     *
     * @retval true signal accepted. Thread will be terminated after finish its work
     * @retval false failure, this thread cannot be signaled.
     */
	static bool stopThread();

	///Execute function when asynchronous resource becomes signaled. You can specify timeout
	/**
	 * @param res asynchronous resource monitored
	 * @param fn function
	 * @param timeout
	 */
	template<typename Fn>
	void runAsync(IAsyncResource &&res, Fn &&fn, const std::chrono::system_clock::time_point &timeout)  {
		get()->runAsync(std::move(res), IAsyncProvider::Callback(std::forward<Fn>(fn)), timeout);
	}

	///Execute function asynchronously in context of provider's thread
	template<typename Fn>
	void runAsync(Fn &&fn) {
		get()->runAsync(std::forward<Fn>(fn));
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

	///stop asynchronous provider
	void stop() {
		return get()->stop();
	}
};


class NoDispatcherForTheResourceException: public std::exception {
public:

    NoDispatcherForTheResourceException(const std::type_info &type):type(type) {}
    const std::type_info &getType() const {return type;}
    const char *what() const noexcept override;
    template<typename T>
    bool isType() const {return type == typeid(T);}
protected:
    mutable std::string message;
    const std::type_info &type;
};

class NoAsynProviderIsActiveException: public std::exception {
public:
    const char *what() const noexcept override;
};


///Configuration for asynchronous provider
struct AsyncProviderConfig {
    ///count of socket dispatchers
    unsigned int socket_dispatchers = 1;
    ///count of threads
    /** Default value is zero as in most cases, you want to have creation of threads under your
     * control. This means, you have to create own threads and each thread must call AsyncProvider::start_thread()
     *
     * Note that HttpServer expects this value filled in if the configuration is used to start the server.
     * In this case, the HttpServer uses the value only as parameter, but initializes
     * the dispatcher with zero threads and then creates own set of threads
     */
    unsigned int threads = 0;
    ///install scheduler
    bool scheduler = true;
    ///force to use poll (default is epoll), for Windows WSAPoll is always used
    bool use_poll = false;

};


///Create asynchronous provider with specified dispatchers
/**
 * @param cfg conifguration
 * @return asynchronous provider
 */
AsyncProvider createAsyncProvider(const AsyncProviderConfig &cfg);

void setCurrentAsyncProvider(AsyncProvider aprovider);

void setThreadAsyncProvider(AsyncProvider aprovider);

AsyncProvider getCurrentAsyncProvider();

std::optional<AsyncProvider> getCurrentAsyncProvider_NoException();




}
#endif /* SRC_MINISERVER_ASYNC_PROVIDER_H_ */




