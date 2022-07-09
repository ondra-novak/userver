/*
 * stream2.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_STREAM2_H_
#define SRC_LIBS_USERVER_STREAM2_H_


#include "platform_def.h"
#include "isocket.h"

#include <vector>
#include <memory>

namespace userver {


class AbstractStreamInstance {
public:
    virtual ~AbstractStreamInstance() = default;



    //reading interface

     /// Read stream synchronously.
     /** @return buffer containing read data. The buffer is allocated internally and it
     * is valid until next call. Function always returns at least 1 byte, otherwise it
     * blocks. However, in case of timeout or error, the empty buffer is returned. To
     * determine whether it was because timeout, check function timeouted(). In case of
     * the timeout, you can clear timeout and repeat the request
     * @exception any Function can throw exception in case of network error
     *
     * @note function is not MT Safe for reading side, so only one thread can reat at
     * time - applies both sync or async
     */
    virtual std::string_view read_sync() = 0;

    ///Read stream asynchronously
    /**
     * @param callback a callback function which is called when at least one byte is ready to
     * read. If the empty buffer is passed, then error or timeout happened. To
     * determine whether it was because timeout, check function timeouted(). In case of
     * the timeout, you can clear timeout and repeat the request
     *
     * @note function is not MT Safe for reading side, so only one thread can reat at
     * time - applies both sync or async
     *
     * @note to determine, which error caused loosing the connection, you can check
     * for current exception. The exception pointer is set when there was an error. If
     * the exception pointer is not set, there were timeout out or EOF
     *
     */
    virtual void read_async(Callback<void(std::string_view)> &&callback) = 0;

    ///Puts back unprocessed input
    /**
     * You cannot control, how many bytes can be received, however, you can put back
     * unprocessed data. These data can be later read by read_sync() or read_async().
     *
     * @param buffer buffer contains unprocessed input. The buffer should be substring of
     * a buffer passed or returned by the read function. However it is not error to
     * pass an other buffer, you only need to ensure, that the content of the buffer is
     * valid until it is retrieved and processed.
     *
     * @note function is not MT Safe for reading side, so only one thread can reat at
     * time - applies both sync or async
     */
    virtual void put_back(const std::string_view &buffer) = 0;

    ///Closes the input
    /**
     * Function closes the input part if the stream disallowing any further reading.
     * The function can be called from other thread. Any pending reading is interrupted
     * and reported as an error (connection close)
     */
    virtual void close_input() = 0;


    ///Shorten reading timeout to zero
    /**
     * Function shortens read timeout to zero even if the reading is currently
     * pending. This causes that callback is immediately called with argument set to false.
     *
     * Note the function also sets read timeout to zero. To restart reading, you
     * need to set timeout a meaningful value
     *
     * @note function is MT Safe for reading side, but it is not MT Safe for setting
     * the timeout.
     */
    virtual void timeout_async_read() = 0;

    //block write interface
    ///Writes to the output synchronously
    /**
     * @param buffer buffer to write. Function blocks until write is done or timeout
     * whichever comes first
     *
     * @retval true successfully written
     * @retval false error or timeout. You cannot distinguish between error or timeout, both
     * states are considered as lost connection.
     * @exception any Function can throw exception in case of network error
     *
     *
     * @note You should avoid to combine write_sync and write_async. It is okay only when
     * async. queue is empty. Once there is pending async. writing, invoking a sync writing
     * in this state causes undefined behavior
     */
    virtual bool write_sync(const std::string_view &buffer) = 0;

    ///Writes to the output asynchronously
    /**
     * @param buffer buffer to write. Note if the buffer is empty, the operation is still queued
     * @param copy_content se true to copy content, which allows to discard buffer during the pending operation.
     *  However copying is extra operation, so if you are to sure that buffer will not disappear
     *  during the operation, you can set this parameter to false
     * @param callback callback function which is called when operation is done. The value
     * 'true' is passed when data has been send, or false, when error happened (or timeout).
     * This parametr can be nullptr, in this case, no callback is called
     *
     * @note Do not use this function to send single characters, as the function
     * is to complex to be less efficient in this case. Use put_char() for this purpose
     *
     * @note Multiple threads can use this function, all write requests are queued and
     * processed serialized
     *
     * @note To determine, which error caused loosing the connection, you can check
     * for current exception. The exception pointer is set when there was an error. If
     * the exception pointer is not set, there were timeout out or EOF
     */
    virtual void write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) = 0;
    ///Closes the output
    /**
     * Operation closes output, so other side receives EOF. No futher writes
     * are allowed.
     *
     * If there are pending writes, the call is enqueued and the output is closed
     * once all requests are processed
     */
    virtual void close_output() = 0;


    ///Shorten writing timeout to zero
    /**
     * Function shortens write timeout to zero even if the writing is currently
     * pending. This causes that callback is called with argument set to false.
     *
     * Note the function also sets read timeout to zero. To restart reading, you
     * need to set timeout a meaningful value
     *
     * @note function is MT Safe for writing side, but it is not MT Safe for setting
     * the timeout.
     *
     */
    virtual void timeout_async_write() = 0;


    //character write interface
    ///Puts character to an internal buffer
    /**
     * @param c character to put
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual void put_char(char c) = 0;
    ///Puts block to an internal buffer
    /**
     * @param block block to put
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual void put_block(const std::string_view &block) = 0;
    ///Flushes internal buffer to the stream
    /**
     * @retval true data are sent
     * @retval false error detected
     *
     * @note this operation is at same level as write_sync and write_async. Internally it
     * is implemented as passing internal buffer to the function write_sync. Read
     * the description of this function for further informations
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual bool flush_sync() = 0;
    ///Flush internal buffer asynchronously
    /**
     * @param cb a callback function invoked when operation is complete. The
     * passed argument contains 'true' as success and 'false' as error
     * This parametr can be nullptr, in this case, no callback is called
     *
     * @note this operation is at same level as write_sync and write_async. Internally it
     * is implemented as passing internal buffer to the function write_async. Read
     * the description of this function for further informations
     *
     * @note Operation is not MT safe in this group, so only one thread can use
     * character interface at time
     */
    virtual void flush_async(Callback<void(bool)> &&cb) = 0;

    //misc
    ///Determines, whether read operation timeouted
    /**
     * @retval true operation timeouted, you can call clear_timeout() to continue
     * @retval false operation was not timeouted, probably error or eof, calling clear_timeout()
     * will not help
     */
    virtual bool timeouted() = 0;
    virtual void clear_timeout() = 0;


    ///Changes current read timeout
    /**
     * @param tm_in_ms new timeout duration in milliseconds. Set -1 to infinite
     */
    virtual void set_read_timeout(int tm_in_ms) = 0;
    ///Changes current write timeout
    /**
     * @param tm_in_ms new timeout duration in milliseconds. Set -1 to infinite
     */
    virtual void set_write_timeout(int tm_in_ms) = 0;

    virtual void set_rw_timeout(int tm_in_ms) = 0;


    virtual int get_read_timeout() const = 0;

    virtual int get_write_timeout() const = 0;

};

template<template<class> class _Ptr>
class Stream2_t: public _Ptr<AbstractStreamInstance> {
public:

    using _Ptr<AbstractStreamInstance>::_Ptr;

    std::string_view read_sync() {return (*this)->read_sync();}
    void read_async(Callback<void(std::string_view)> &&callback) {(*this)->read_async(std::move(callback));}
    void put_back(const std::string_view &buffer) {(*this)->put_back(buffer);}
    void close_input() {(*this)->close_input();}
    void timeout_async_read() {(*this)->timeout_async_read();}
    bool write_sync(const std::string_view &buffer) {return (*this)->write_sync(buffer);}
    void write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) {(*this)->write_async(buffer, copy_content, std::move(callback));}
    void timeout_async_write() {(*this)->timeout_async_write();}
    void close_output() {(*this)->close_output();}
    void put_char(char c) {(*this)->put_char(c);}
    void put_block(const std::string_view &block) {(*this)->put_block(block);}
    bool flush_sync() {return (*this)->flush_sync();}
    void flush_async(Callback<void(bool)> &&cb) {(*this)->flush_async(std::move(cb));}
    bool timeouted() {return (*this)->timeouted();}
    void clear_timeout() {(*this)->clear_timeout();}
    void set_read_timeout(int tm_in_ms) {(*this)->set_read_timeout(tm_in_ms);}
    void set_write_timeout(int tm_in_ms) {(*this)->set_write_timeout(tm_in_ms);}
    void set_io_timeout(int tm_in_ms) {(*this)->set_rw_timeout(tm_in_ms);}
    int get_read_timeout() const {return (*this)->get_read_timeout();}
    int get_write_timeout() const {return (*this)->get_write_timeout();}

};

using Stream2 = Stream2_t<std::unique_ptr>;
using SharedStream2 = Stream2_t<std::shared_ptr>;


Stream2 createSocketStream(SocketHandle socket);
Stream2 createSocketStream(std::unique_ptr<ISocket> &&socket);
}



#endif /* SRC_LIBS_USERVER_STREAM2_H_ */
