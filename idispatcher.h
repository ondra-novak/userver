/*
 * idispatcher.h
 *
 *  Created on: 27. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_IDISPATCHER_H_
#define SRC_USERVER_IDISPATCHER_H_

#include "helpers.h"

namespace userver {

class IAsyncResource;


///Interface defines abstract dispatcher
/**
 * Dispatcher is single thread class, which monitors collection of asynchronous resources. You can
 * also define own type of asynchronous resource.
 *
 * The main goal is to monitor the collection, and when the particular resource is signaled, it
 * should return a task - structure which contains callback function and result of the monitoring.
 *
 * The task is later called.
 *
 * The monitoring is blocking, so during the operation, the thread is blocked
 *
 * @see getTask
 *
 */
class IDispatcher {
public:

    using Callback = CallbackT<void(bool)>;
    ///Definition of the task
    struct Task {
        Callback cb;
        bool success;

        ///Construct a task using the callback function and the argument
        /**
         * @param cb callback function associated with monitored resource
         * @param success true if resource was signaled, false in case of timeout
         */
        Task(Callback &&cb, bool success):cb(std::move(cb)), success(success) {}
        ///Construct not-executable task
        /**
         * The dispatcher can use this task to return from getTask() without including any
         * callback. In this case, the dispatcher is moved to other thread. You
         * also need to return this task in case of interrupt() or stop()
         */
        Task():success(false) {}

        ///determines, whether task is executable
        /**
         * @retval true executable
         * @retval false not executable
         */
        bool valid() const {return cb != nullptr;}
    };

    ///Wait for specified resource
    /**
     * @param resource asynchronous resource to wait
     * @param cb callback function, which expects boolean, - true=success, false=timeout
     * @param timeout time to wait
     * @retval true resource accepted
     * @retval false resource was not accepted, try different dispatcher
     */
    virtual bool waitAsync(IAsyncResource &&resource,  Callback &&cb, std::chrono::system_clock::time_point timeout) = 0;
    ///get next task, block until the any monitored resource is signaled or a timeout is reached
    /**
     * @return returns task build from associated callback function and success information (see Task). It
     * can also return not-executable task which is need to get work the functions interrupt() and stop().
     *
     * @note it is not error to return not-executable task even if it was not requested. This causes
     * that dispatcher will be assigned to other thread. However the dispatcher should block
     * execution for a while, this not the way how to implement pooling, because there
     * is an extra work to reassign the dispatcher to the other thread
     */
    virtual Task getTask() = 0;

    ///Interrupt the blocking operation
    /**
     * Called from other thread. The dispatcher should leave the function getTask as soon as possible.
     * if the function is called when the dispatcher is not blocked, the information should be remembered
     * and dispatcher should leave getTask() immediatelly as it enters to it
     */
    virtual void interrupt() = 0;
    ///Stops the dispatcher
    /**
     * Function is called in advance before destruction to safety destruct all monitored resources.
     * The dispatcher should stop monitoring and leave getTask by returning not-executable task.
     * Function works similare as interrupt, however it should also remove all monitored resources
     * and all registered callbacks.
     */
	virtual void stop() = 0;

	virtual ~IDispatcher() {}
};

using PDispatch = std::unique_ptr<IDispatcher>;



}



#endif /* SRC_USERVER_IDISPATCHER_H_ */
