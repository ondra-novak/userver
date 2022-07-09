/*
 * scheduler_impl.cpp
 *
 *  Created on: 19. 6. 2022
 *      Author: ondra
 */

#include <typeinfo>


#include "scheduler_impl.h"

namespace userver {

bool SchedulerDispatcher::waitAsync(IAsyncResource &&resource, IDispatcher::Callback &&cb,
        std::chrono::system_clock::time_point timeout) {
    if (typeid(resource) == typeid(SchedulerAsyncResource)) {
        SchedulerAsyncResource &res = static_cast<SchedulerAsyncResource &>(resource);
        std::unique_lock _(mx);
        queue.push(SchTask{res.id, timeout,std::move(cb)});
        intr = true;
        cond.notify_one();
        return true;
    } else {
        return false;
    }
}

IDispatcher::Task SchedulerDispatcher::getTask() {
    std::unique_lock _(mx);
    Task t = commit(std::chrono::system_clock::now());
    if (t.valid()) return t;
    intr = stopped;
    if (queue.empty()) {
        cond.wait(_,[&]{return intr;});
        return Task();
    } else {
        auto tp = queue.top().tp;
        cond.wait_until(_, tp, [&]{return intr;});
        return commit(std::chrono::system_clock::now());
    }
}

void SchedulerDispatcher::interrupt() {
    std::unique_lock _(mx);
    intr = true;
    cond.notify_one();

}

void SchedulerDispatcher::stop() {
    std::unique_lock _(mx);
    queue = Queue();
    intr = true;
    stopped = true;
    cond.notify_one();
}

SchedulerDispatcher::Callback SchedulerDispatcher::stopWait(IAsyncResource &&resource) {
    Callback to_call;
    if (typeid(resource) == typeid(SchedulerAsyncResource)) {
        SchedulerAsyncResource &res = static_cast<SchedulerAsyncResource &>(resource);
        std::unique_lock _(mx);
        Queue q (std::move(queue));
        while (!q.empty()) {
            SchTask &t = const_cast<SchTask &>(q.top());
            if (t.id != res.id) {
                queue.push(std::move(t));
            } else {
                to_call = std::move(t.cb);
            }
            q.pop();
        }
    }
    return to_call;
}

SchedulerDispatcher::Task SchedulerDispatcher::commit(const std::chrono::system_clock::time_point &now) {
    if (queue.empty() || queue.top().tp > now) return Task();
    SchTask &t = const_cast<SchTask &>(queue.top());
    Callback cb (std::move(t.cb));
    queue.pop();
    return Task(std::move(cb),true);
}

void installScheduler(AsyncProvider a) {
    a->addDispatcher(std::make_unique<SchedulerDispatcher>());
}

}
