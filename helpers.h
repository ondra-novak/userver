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
#include <ctime>
#include "callback.h"
#include <mutex>

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

template<typename Fn>
inline void httpDate(std::time_t tpoint, Fn &&fn) {
	 char buf[256];
	 struct tm tm;
#ifdef _WIN32
	 gmtime_s(&tm, &tpoint);
#else
	 gmtime_r(&tpoint, &tm);
#endif
	 auto sz = std::strftime(buf, sizeof buf, "%a, %d %b %Y %H:%M:%S %Z", &tm);
	 fn(std::string_view(buf,sz));
}


///Protects pending operation
/**
 * Small object connected with shared ptr. It is initialized by init() and then
 * split into two parts, where one part has caller and second part goes into pending operation
 *
 * Caller can anytime later cancel operation by cancel. If this happens, pending operation can
 * see this flag and stop processing the operation as soon as possible.
 *
 * Most used when there is asynchronous operation and caller objects is about destructed, it
 * can cancel pending operation before the operation can possible access the object in question.
 *
 *
 *
 *
 * @tparam Lock
 */

template<typename Lock>
class PendingOpT {

    struct Land {
        Lock mx;
        bool canceled = false;
    };

public:
    ///Init the instance (it is empty after construction)
    void init() {
        land = std::make_shared<Land>();
    }
    ///Clears instance
    void clear() {
        land = nullptr;
    }

    ///Determines, whether the instance has been initialized
    /**
     * @retval true is inited
     * @retval false is cleared
     */
    bool is_ready() const {
        return land != nullptr;
    }

    ///Mark pending operation canceled.
    void cancel() {
        if (land != nullptr) {
            std::lock_guard _(land->mx);
            land->canceled = true;
        }
    }

    ///Mark pending operation canceled and clears instance under lock of owning class
    /**
     * Solves problem with reversed locks. If this instance is protected by a lock
     * inside of class, calling the cancel directly can cause reverse locking and deadlock
     * as in finishing operation, first is PendingOp's lock acquired and then owner's lock,
     * but in this case, owner's lock is already acquired. So the function is able
     * to cancel pending operation while it releases owner's lock to give any pending
     * operation chance to complete before it is canceled
     *
     * @param lk owner's class lock to be unlocked during cancelation
     *
     * @note function also clears the instance
     */
    template<typename Lk>
    void cancel_clear(Lk &lk) {
        if (land != nullptr) {
            auto l = std::move(land);
            lk.unlock();
            std::lock_guard _(l->mx);
            l->canceled = true;
            lk.lock();
        }
    }

    ///Determines whether operation is canceled
    bool is_canceled() const {
        std::lock_guard _(land->mx);
        return land->canceled;
    }

    ///Finishes pending operation if was not canceled
    /***
     *
     * @param fn function to call to finish
     * @retval true operation successful
     * @retval false operation was not run, because it was canceled
     */
    template<typename Fn>
    bool finish_pending(Fn &&fn) const {
        std::lock_guard _(land->mx);
        if (!land->canceled) {
            fn();
            return true;
        } else {
            return false;
        }
    }

    template<typename Fn>
    bool operator>>(Fn &&fn) const {
        return finish_pending(std::forward<Fn>(fn));
    }


protected:
    std::shared_ptr<Land> land;

};

using PendingOp = PendingOpT<std::mutex>;

}

#endif /* SRC_MINISERVER_HELPERS_H_ */

