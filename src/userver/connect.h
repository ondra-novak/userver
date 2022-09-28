/*
 * connect.h
 *
 *  Created on: 5. 8. 2022
 *      Author: ondra
 */

#ifndef _SRC_USERVER_CONNECT_H_2139eu290ud23908
#define _SRC_USERVER_CONNECT_H_2139eu290ud23908
#include <userver/scheduler.h>
#include "socket.h"
#include "netaddr.h"
#include "stream.h"
#include <cerrno>
#include <memory>


namespace userver {

class Connect {
public:

    Connect(const NetAddrList &lst, std::size_t timeout_in_ms = 30000):_lst(lst),_timeout(timeout_in_ms) {}
    Connect(NetAddrList &&lst, std::size_t timeout_in_ms = 30000):_lst(std::move(lst)),_timeout(timeout_in_ms) {}

    template<typename Fn>
    auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<std::optional<Stream> &&>()), std::declval<Connect &>()) {
        connect_stream(std::forward<Fn>(fn));
        return *this;
    }

    template<typename Fn>
    auto operator>>(Fn &&fn) -> decltype(std::declval<Fn>()(std::declval<std::optional<Socket> &&>()), std::declval<Connect &>()) {
        connect_socket(std::forward<Fn>(fn));
        return *this;
    }



protected:

    template<typename Fn>
    void connect_socket(Fn &&fn);
    template<typename Fn>
    void connect_stream(Fn &&fn);
    template<typename Fn, typename Trns>
    void connect_and_transform(Fn &&fn, Trns &&trns);


    NetAddrList _lst;
    std::size_t _timeout;


    template<typename Fn, typename Trns>
    struct SharedState {
        Fn fn;
        Trns trns;
        std::atomic<std::size_t> count = 0;
        std::atomic<bool> connected = false;

        SharedState(Fn &&fn, Trns &&trns, std::size_t count)
            :fn(std::forward<Fn>(fn))
            ,trns(std::forward<Trns>(trns))
            ,count(count) {}

    };

};


template<typename Fn, typename Trns>
void Connect::connect_and_transform(Fn &&fn, Trns &&trns) {
    if (_lst.empty()) {
        try {
            throw std::system_error(ENOENT, std::system_category());
        } catch (...) {
            fn({});
        }
    } else {
        auto shared = std::make_shared<SharedState<Fn, Trns> >(
            std::forward<Fn>(fn),
            std::forward<Trns>(trns),
            _lst.size()
        );
        std::size_t delay = 0;
        std::size_t timeout = _timeout;
        bool support_timer = true;
        try {
            After(std::chrono::seconds(0)) >> []{};
        } catch (const NoDispatcherForTheResourceException &) {
            support_timer = false;
        }

        for (const auto &x: _lst) {
            if (shared->connected) break;
            auto s = std::make_unique<Socket>(Socket::connect(x));
            auto sock = s.get();
            CallbackT<void(bool)> ccb ( [=, s = std::move(s)](bool ok) mutable {
                auto r = --shared->count;
                bool z = false;;
                if (ok) {
                    if (shared->connected.compare_exchange_strong(z, true)) {
                        shared->trns(shared->fn,s);
                    }
                } else {
                    if (r == 0 && shared->connected.compare_exchange_strong(z, true)) {
                        try {
                            throw std::system_error(ECONNREFUSED, std::system_category());
                        } catch (...) {
                            shared->fn({});
                        }
                    }
                }
            });
            if (support_timer) {
                After(std::chrono::seconds(delay)) >> [=, ccb = std::move(ccb)]() mutable {
                    if (!shared->connected) {
                        sock->waitConnect(timeout,std::move(ccb));
                    }
                };
                ++delay;
            } else {
                sock->waitConnect(timeout,std::move(ccb));
            }
        }
    }
}
template<typename Fn>
inline void Connect::connect_socket(Fn &&fn) {
    connect_and_transform(std::forward<Fn>(fn), [](Fn &fn, std::unique_ptr<Socket> &s){
          fn(std::move(*s));
    });
}

template<typename Fn>
inline void Connect::connect_stream(Fn &&fn) {
    connect_and_transform(std::forward<Fn>(fn), [](Fn &fn, std::unique_ptr<Socket> &s){
       fn(createSocketStream(std::move(*s)));
    });
}

Connect connect(const NetAddrList &lst, std::size_t timeout_in_ms = 30000) {
    return Connect(lst, timeout_in_ms);
}
Connect connect(NetAddrList &&lst, std::size_t timeout_in_ms = 30000) {
    return Connect(std::move(lst), timeout_in_ms);
}


}


#endif /* _SRC_USERVER_CONNECT_H_2139eu290ud23908 */
