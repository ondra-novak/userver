/*
 * scheduler.h
 *
 *  Created on: 19. 6. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_SCHEDULER_H_
#define SRC_LIBS_USERVER_SCHEDULER_H_
#include "async_provider.h"

namespace userver {

using ScheduledTaskID = std::size_t;

class SchedulerAsyncResource: public IAsyncResource {
public:
    SchedulerAsyncResource(ScheduledTaskID id):id(id) {}


    ScheduledTaskID id;
};


///Implements scheduler through the asynchronous provider (as an asynchronous call)
/**
 * At the first, scheduler is standard dispatcher, which needs extra thread to run. If you
 * have exact count of thread as other dispatchers, the scheduler may stuck while waiting to
 * available thread.  So always increase count of threads by one when you use scheduler
 *
 * The scheduler is used through At class or After class
 *
 * 'At' class schedules a callback to run at specified time point (usng system_clock)
 * 'After' class schedules a callback to run after specified duration
 *
 * @code
 *
 * At(tp) >> []{ ... code ...};
 * After(std::chrono::seconds(10)) >> []{ ... code ...};
 *
 * @endcode
 *
 * @note Dispatcher must be enabled or installed manually
 */
class At {
public:
    At(const std::chrono::system_clock::time_point &tp):tp(tp),id(++glob_id) {}
    At(const std::chrono::system_clock::time_point &tp, ScheduledTaskID id):tp(tp),id(id) {}
    template<typename Fn>
    At &operator>>(Fn &&fn) {
        AsyncProvider a = getCurrentAsyncProvider();
        a->runAsync(SchedulerAsyncResource(id), [fn = std::forward<Fn>(fn)](bool){
            fn();
        }, tp);
        return *this;
    }
    ScheduledTaskID get_id() const {return id;}

    void cancel() {
        AsyncProvider a = getCurrentAsyncProvider();
        a->stopWait(SchedulerAsyncResource(id), false);
    }

protected:
    std::chrono::system_clock::time_point tp;
    ScheduledTaskID id;
    static std::atomic<ScheduledTaskID> glob_id;

    void installScheduler(AsyncProvider &a);
};

///See At
class After:public At {
public:
    template<typename A, typename B>
    After(const std::chrono::duration<A,B> &dur):At(std::chrono::system_clock::now()+dur) {}
};


///Install scheduler manually
/**
 * Function doesn't check, whether there is already scheduler installed. It is valid operation
 * to have multiple schedulers installed especially for a lot of tasks. Also note, you will need
 * also add a thread for the scheduler, if you don't have already spare threads available.
 *
 * @param a asynchronous provider
 */
void installScheduler(AsyncProvider a);

}




#endif /* SRC_LIBS_USERVER_SCHEDULER_H_ */
