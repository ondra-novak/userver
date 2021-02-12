/*
 * async_provider.cpp
 *
 *  Created on: 12. 1. 2021
 *      Author: ondra
 */

#include "platform.h"

#include <condition_variable>
#include <mutex>
#include <queue>

#include "async_resource.h"
#include "dispatcher.h"
#include "dispatcher_epoll.h"
#include "async_provider.h"
#include "win_category.h"

namespace userver {

class AsyncProviderImpl: public IAsyncProvider {
public:

	using Factory = std::unique_ptr<IDispatcher> (*)();

	AsyncProviderImpl(unsigned int dispcnt, Factory factory);
	virtual void stop();
	virtual void runAsync(const AsyncResource &res,
			IAsyncProvider::Callback &&cb,
			const std::chrono::system_clock::time_point &timeout);
	virtual bool yield();
	virtual void runAsync(IAsyncProvider::Callback &&cb);


protected:
	using PDispatch = std::unique_ptr<IDispatcher>;
	std::queue<PDispatch> dispatchers;
	std::queue<IDispatcher *> dispqueue;
	std::mutex lock;
	std::condition_variable wt;



};

static std::unique_ptr<IDispatcher> createDispatcher() { return std::make_unique<Dispatcher>(); };


#ifdef _WIN32
AsyncProvider createAsyncProvider(unsigned int dispatchers, AsyncProviderType type) {
	AsyncProviderImpl::Factory f = &createDispatcher; 
	return std::make_shared<AsyncProviderImpl>(dispatchers, f);
}
#else

static std::unique_ptr<IDispatcher> createDispatcher_epoll() { return std::make_unique<Dispatcher_EPoll>(); };

AsyncProvider createAsyncProvider(unsigned int dispatchers, AsyncProviderType type) {
	AsyncProviderImpl::Factory f;
	switch (type) {
	default:
	case AsyncProviderType::poll: f = &createDispatcher;break;
	case AsyncProviderType::epoll: f = &createDispatcher_epoll;break;
	}
	return std::make_shared<AsyncProviderImpl>(dispatchers, f);
}
#endif

inline void AsyncProviderImpl::stop() {
	std::unique_lock _(lock);
	for (std::size_t i = 0; i < dispatchers.size(); i++) {
		PDispatch d (std::move(dispatchers.front()));
		d->stop();
		dispatchers.pop();
		dispatchers.push(std::move(d));
	}
}

inline void AsyncProviderImpl::runAsync(const AsyncResource &res,
		IAsyncProvider::Callback &&cb,
		const std::chrono::system_clock::time_point &timeout) {

	std::unique_lock _(lock);
	PDispatch d (std::move(dispatchers.front()));
	switch (res.op) {
		case AsyncResource::read: d->waitRead(res.socket, std::move(cb), timeout);break;
		case AsyncResource::write: d->waitWrite(res.socket, std::move(cb), timeout);break;
	}
	dispatchers.pop();
	dispatchers.push(std::move(d));
}

inline bool AsyncProviderImpl::yield() {
	IDispatcher *selDisp;
	std::unique_lock _(lock);
	wt.wait(_,[&]{
		return !dispqueue.empty();
	});
	selDisp = dispqueue.front();
	dispqueue.pop();
	_.unlock();
	try {
		auto task = selDisp->getTask();
		std::unique_lock _(lock);
		dispqueue.push(selDisp);
		wt.notify_one();
		_.unlock();
		if (task.valid()) {
			task.cb(task.timeouted);
			return true;
		} else {
			return false;
		}
	} catch (...) {
		_.lock();
		dispqueue.push(selDisp);
		wt.notify_one();
		throw;
	}
}

inline AsyncProviderImpl::AsyncProviderImpl(unsigned int dispcnt, Factory factory) {
	for (decltype(dispcnt) i = 0; i < dispcnt; i++) {
		auto d = factory();
		dispqueue.push(d.get());
		dispatchers.push(std::move(d));
	}
}

inline void AsyncProviderImpl::runAsync(IAsyncProvider::Callback &&cb) {
	std::unique_lock _(lock);
	PDispatch d (std::move(dispatchers.front()));
	d->execAsync(std::move(cb));
	dispatchers.pop();
	dispatchers.push(std::move(d));
}

static std::mutex asyncLock;
static AsyncProvider curAsyncProvider;
thread_local AsyncProvider curThreadAsyncProvider;

void setCurrentAsyncProvider(AsyncProvider aprovider) {
	std::lock_guard _(asyncLock);
	curAsyncProvider = aprovider;
}

void setThreadAsyncProvider(AsyncProvider aprovider) {
	curThreadAsyncProvider = aprovider;
}

AsyncProvider getCurrentAsyncProvider() {
	if (curThreadAsyncProvider == nullptr) {
		std::lock_guard _(asyncLock);
		curThreadAsyncProvider = curAsyncProvider;
	}
	if (curThreadAsyncProvider == nullptr) throw std::runtime_error("No asynchronous provider is active");
	return curThreadAsyncProvider;
}

#ifdef _WIN32

static AsyncProvider stopOnSignalAsyncProvider;

static BOOL WINAPI CtrlHandler(DWORD fdwCtrlType) {

	AsyncProvider p = std::move(stopOnSignalAsyncProvider);
	if (p != nullptr) {
		p->stop();
		p.reset();
		std::this_thread::sleep_for(std::chrono::seconds(30));
	}
	return TRUE;
}

void AsyncProvider::stopOnSignal() {
	bool needInstall = stopOnSignalAsyncProvider == nullptr;
	stopOnSignalAsyncProvider = *this;
	if (needInstall) {

		if (!SetConsoleCtrlHandler(CtrlHandler, TRUE)) {
			throw std::system_error(GetLastError(), win32_error_category());
		}
	}		
}

#else



#endif
}
