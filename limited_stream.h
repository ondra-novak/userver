/*
 * limited_stream.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_LIMITED_STREAM_H_
#define SRC_LIBS_USERVER_LIMITED_STREAM_H_

#include "stream.h"

namespace userver {

///Limited stream allows to limit read or written data
/**
 * You specify limit at constructor.
 *
 * Once the limit is reached, no more data is returned. Writing outside of the limit causes exception.
 * Writing less data then specified also causes exception, however there is function which
 * can fill rest of data with specified byte
 *
 *
 */
class LimitedStream: public AbstractStreamInstance {
public:

    LimitedStream(AbstractStreamInstance &stream, std::size_t read_limit, std::size_t write_limit);
    LimitedStream(const LimitedStream &other) = delete;
    LimitedStream(LimitedStream &&other);
    LimitedStream &operator=(const LimitedStream &other) = delete;
    LimitedStream &operator=(LimitedStream &&other) = delete;
    ~LimitedStream();

    virtual void put_back(const std::string_view &buffer) override;
    virtual void timeout_async_write() override;
    virtual void read_async(Callback<void(std::string_view)> &&callback) override;
    virtual void write_async(const std::string_view &buffer, bool copy_content,
                Callback<void(bool)> &&callback) override;
    virtual int get_read_timeout() const override;
    virtual std::string_view read_sync_nb() override;
    virtual std::string_view read_sync() override;
    virtual void clear_timeout() override;
    virtual int get_write_timeout() const override;
    virtual void close_input() override;
    virtual bool timeouted() override;
    virtual void close_output() override;
    virtual void set_rw_timeout(int tm_in_ms) override;
    virtual void set_read_timeout(int tm_in_ms) override;
    virtual void set_write_timeout(int tm_in_ms) override;
    virtual bool write_sync(const std::string_view &buffer) override;
    virtual void timeout_async_read() override;
    virtual std::size_t get_pending_write_size() const override {
    	return _ref.get_pending_write_size();
    }


protected:
    AbstractStreamInstance &_ref;
    std::size_t read_limit;
    std::atomic<long> write_limit;
    char fill_char = 0;
    std::string_view put_back_buff;

    static void throw_write_limit_error();
};



}



#endif /* SRC_LIBS_USERVER_LIMITED_STREAM_H_ */
