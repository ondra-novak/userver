#include "limited_stream.h"

namespace userver {

LimitedStream::LimitedStream(AbstractStreamInstance &stream,
        std::size_t read_limit, std::size_t write_limit)
:_ref(stream)
 ,read_limit(read_limit)
 ,write_limit(write_limit)
{}

LimitedStream::LimitedStream(LimitedStream &&other)
:_ref(other._ref)
,read_limit(other.read_limit)
,write_limit(other.write_limit.load())
,fill_char(other.fill_char)
,put_back_buff(other.put_back_buff)
{
    other.read_limit = 0;
    other.write_limit = 0;
    other.put_back_buff = std::string_view();
}

LimitedStream::~LimitedStream() {
    LimitedStream::close_input();
    LimitedStream::close_output();
}


void LimitedStream::put_back(const std::string_view &buffer) {
    put_back_buff = buffer;
}

void LimitedStream::timeout_async_write() {
    _ref.timeout_async_read();
}

void LimitedStream::read_async(Callback<void(std::string_view)> &&callback) {
    if (!put_back_buff.empty()) {
        auto c = put_back_buff;
        put_back_buff = std::string_view();
        callback(c);
    } else if (read_limit) {
        _ref.read_async([this, cb = std::move(callback)](std::string_view data) {
           auto c = data.substr(0,read_limit);
           auto d = data.substr(c.length());
           read_limit -= c.length();
           _ref.put_back(d);
           cb(c);
        });
    } else {
        callback(std::string_view());
    }
}

void LimitedStream::write_async(const std::string_view &buffer,bool copy_content, Callback<void(bool)> &&callback) {
    if ((write_limit-=buffer.size()) <0) {
        write_limit+=buffer.size();
        try {
            throw_write_limit_error();
        } catch (...) {
            callback(false);
        }
    } else {
        _ref.write_async(buffer, copy_content, std::move(callback));
    }
}

int LimitedStream::get_read_timeout() const {
    return _ref.get_read_timeout();
}

std::string_view LimitedStream::read_sync() {
    if (!put_back_buff.empty()) {
        auto c = put_back_buff;
        put_back_buff = std::string_view();
        return c;
    } else if (read_limit) {
        auto data = _ref.read_sync();
        auto c = data.substr(0,read_limit);
        auto d = data.substr(c.length());
        read_limit -= c.length();
        _ref.put_back(d);
        return c;
    } else {
        return std::string_view();
    }
}

void LimitedStream::flush_async(Callback<void(bool)> &&cb) {
    auto sz = _ref.get_put_size();
    if ((write_limit -= sz) < 0) {
        write_limit += sz;
        try {
            _ref.discard_put_buffer();
            throw_write_limit_error();
        } catch (...) {
            cb(false);
        }
    } else {
        _ref.flush_async(std::move(cb));
    }
}

void LimitedStream::clear_timeout() {
    _ref.clear_timeout();
}

int LimitedStream::get_write_timeout() const {
    return _ref.get_write_timeout();
}

void LimitedStream::close_input() {
    while (read_limit) {
        LimitedStream::read_sync();
    }
}

bool LimitedStream::timeouted() {
    return _ref.timeouted();
}

void LimitedStream::close_output() {
    flush_sync();
    if (write_limit) {
        char buff[4096];
        for (char &c: buff) c = fill_char;
        while (write_limit>sizeof(buff)) {
            LimitedStream::write_sync(std::string_view(buff, sizeof(buff)));
        }
        LimitedStream::write_sync(std::string_view(buff, write_limit));
    }
}

void LimitedStream::set_rw_timeout(int tm_in_ms) {
    _ref.set_rw_timeout(tm_in_ms);
}

void LimitedStream::set_read_timeout(int tm_in_ms) {
    _ref.set_read_timeout(tm_in_ms);
}

bool LimitedStream::flush_sync() {
    auto sz = _ref.get_put_size();
    if ((write_limit -= sz) < 0) {
        write_limit += sz;
        _ref.discard_put_buffer();
       throw_write_limit_error();
    }
    return _ref.flush_sync();
}

void LimitedStream::set_write_timeout(int tm_in_ms) {
    _ref.set_write_timeout(tm_in_ms);
}

bool LimitedStream::put(char c) {
    return _ref.put(c);
}

bool LimitedStream::put(const std::string_view &block) {
    return _ref.put(block);
}

bool LimitedStream::write_sync(const std::string_view &buffer) {
    if ((write_limit+=buffer.size()) < 0) {
        write_limit += buffer.size();
        _ref.discard_put_buffer();
       throw_write_limit_error();
    }
    return _ref.write_sync(buffer);
}


void LimitedStream::timeout_async_read() {
    return _ref.timeout_async_read();
}

std::size_t LimitedStream::get_put_size() const {
    return _ref.get_put_size();
}

std::vector<char> LimitedStream::discard_put_buffer() {
    return _ref.discard_put_buffer();
}

void LimitedStream::throw_write_limit_error() {
    throw std::runtime_error("LimitedStream write beyond of limit");
}

}
