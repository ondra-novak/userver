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

#include "dispatcher.h"
#include "dispatcher_epoll.h"
#include "async_provider.h"

namespace userver {



class AsyncProviderImpl: public IAsyncProvider {
public:


	AsyncProviderImpl();
	virtual void stop() override;
	virtual void runAsync(IAsyncResource &&res,
			IAsyncProvider::Callback &&cb,
			const std::chrono::system_clock::time_point &timeout) override;
	virtual bool yield() override;
	virtual void runAsync(IAsyncProvider::Action &&cb) override;
	virtual bool stopped() const override {return _stopped;}


	virtual void addDispatcher(PDispatch &&dispatcher) override;
    virtual std::size_t getDispatchersCount() const override;

protected:
	std::queue<PDispatch> dispatchers;
	std::queue<IDispatcher *> dispqueue;
	mutable std::mutex lock;
	std::condition_variable wt;
	bool _stopped = false;
	std::queue<Action> actions;



};


AsyncProvider createAsyncProvider(unsigned int dispatchers) {
    auto prov = std::make_shared<AsyncProviderImpl>();
    for (unsigned int i = 0; i < dispatchers; i++) {
#ifdef _WIN32
        prov->addDispatcher(std::make_unique<Dispatcher>());
#else
        prov->addDispatcher(std::make_unique<Dispatcher_EPoll>());
#endif
    }
    return prov;

}


inline void AsyncProviderImpl::stop() {
	std::unique_lock _(lock);
	_stopped = true;
	for (std::size_t i = 0; i < dispatchers.size(); i++) {
		PDispatch d (std::move(dispatchers.front()));
		d->stop();
		dispatchers.pop();
		dispatchers.push(std::move(d));
	}
}

inline void AsyncProviderImpl::runAsync(IAsyncResource &&res,
		IAsyncProvider::Callback &&cb,
		const std::chrono::system_clock::time_point &timeout) {

	std::unique_lock _(lock);
	auto cnt = dispatchers.size();
	for (decltype(cnt) i = 0; i < cnt; i++) {
        PDispatch d (std::move(dispatchers.front()));
        dispatchers.pop();
        bool ok = d->waitAsync(std::move(res), std::move(cb), timeout);
        dispatchers.push(std::move(d));
        if (ok) return;

	}
	throw std::runtime_error(std::string("No dispatcher for the resource:").append(typeid(res).name()));
}

inline bool AsyncProviderImpl::yield() {
	IDispatcher *selDisp;
	std::unique_lock _(lock);
	if (_stopped) return false;
	if (actions.empty()) {
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
                task.cb(task.success);
            }
            return true;
        } catch (...) {
            _.lock();
            dispqueue.push(selDisp);
            wt.notify_one();
            throw;
        }
	} else {
	    Action a(std::move(actions.front()));
	    actions.pop();
	    _.unlock();
	    a();
	    return true;
	}
}


inline AsyncProviderImpl::AsyncProviderImpl() {}

inline void AsyncProviderImpl::runAsync(IAsyncProvider::Action &&cb) {
	std::unique_lock _(lock);
	actions.push(std::move(cb));
	dispatchers.front()->interrupt();
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

std::optional<AsyncProvider> getCurrentAsyncProvider_NoException() {
	if (curThreadAsyncProvider == nullptr) {
		std::lock_guard _(asyncLock);
		curThreadAsyncProvider = curAsyncProvider;
	}
	if (curThreadAsyncProvider == nullptr) return std::optional<AsyncProvider>();
	else return curThreadAsyncProvider;
}

AsyncProvider getCurrentAsyncProvider() {

	auto x = getCurrentAsyncProvider_NoException();
	if (!x.has_value()) throw std::runtime_error("No asynchronous provider is active");
	return *x;
}

static AsyncProvider stopOnSignalAsyncProvider;

#ifdef _WIN32

}

#include "win_category.h"

namespace userver {


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

static void stopServer(int) {

	AsyncProvider p = std::move(stopOnSignalAsyncProvider);
	if (p != nullptr) {
		p->stop();
		p.reset();
	}

}

void AsyncProvider::stopOnSignal() {
	bool needInstall = stopOnSignalAsyncProvider == nullptr;
	stopOnSignalAsyncProvider = *this;
	if (needInstall) {

		signal(SIGINT, &stopServer);
		signal(SIGTERM, &stopServer);
	}
}




#endif

void AsyncProviderImpl::addDispatcher(PDispatch &&dispatcher) {
    std::unique_lock _(lock);
    IDispatcher *p = dispatcher.get();
    dispatchers.push(std::move(dispatcher));
    dispqueue.push(p);
}

std::size_t AsyncProviderImpl::getDispatchersCount() const {
    std::unique_lock _(lock);
    return dispatchers.size();
}

}
