/*
 * scheduler_impl.h
 *
 *  Created on: 19. 6. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_SCHEDULER_IMPL_H_
#define SRC_LIBS_USERVER_SCHEDULER_IMPL_H_

#include <vector>
#include <condition_variable>
#include <mutex>
#include <queue>
#include "idispatcher.h"
#include "scheduler.h"
#include "async_provider.h"

namespace userver {

class SchedulerDispatcher: public IDispatcher {
public:

    virtual bool waitAsync(IAsyncResource &&resource,
            IDispatcher::Callback &&cb, std::chrono::system_clock::time_point timeout) override;
    virtual IDispatcher::Task getTask() override;
    virtual void interrupt() override;
    virtual void stop() override;


protected:

    Task commit(const std::chrono::system_clock::time_point &now);

    struct SchTask {
        std::chrono::system_clock::time_point tp;
        IDispatcher::Callback cb;
    };

    struct SchTaskOrder {
    public:
        bool operator()(const SchTask &a, const SchTask &b) const {
            return a.tp > b.tp;
        }
    };

    using Queue = std::priority_queue<SchTask, std::vector<SchTask>, SchTaskOrder>;

    std::mutex mx;
    std::condition_variable cond;
    Queue queue;
    bool intr = false;
    bool stopped = false;

};


}



#endif /* SRC_LIBS_USERVER_SCHEDULER_IMPL_H_ */
