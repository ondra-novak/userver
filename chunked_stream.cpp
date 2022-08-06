/*
 * chunked_stream.cpp
 *
 *  Created on: 10. 7. 2022
 *      Author: ondra
 */

#include "chunked_stream.h"

#include <future>


namespace userver {

ChunkedStream::ChunkedStream(AbstractStreamInstance &stream, bool writing, bool reading)
:_ref(&stream)
,write_closed(!writing)
,read_closed(!reading)
{
}

ChunkedStream::ChunkedStream(ChunkedStream &&other)
:_ref(std::move(other._ref))
,write_closed(other.write_closed)
,read_closed(other.read_closed)
,chunk_size(other.chunk_size)
,putback_buff(other.putback_buff)
{
    other.write_closed = true;
    other.read_closed = true;
    other.chunk_size = 0;
    other.putback_buff = std::string_view();
}

ChunkedStream::~ChunkedStream() {
    ChunkedStream::close_input();
    ChunkedStream::close_output();
}

void ChunkedStream::put_back(const std::string_view &buffer) {
    putback_buff = buffer;
}

void ChunkedStream::timeout_async_write() {
    _ref->timeout_async_write();
}

std::size_t ChunkedStream::parseChunkLine(const std::string &ln) {
    char *term = nullptr;
    auto sz = std::strtoul(ln.c_str(), &term, 16);
    if (*term != '\r') throw std::runtime_error("Invalid chunk");
    return sz;
}

void ChunkedStream::read_async(Callback<void(std::string_view)> &&callback) {
    if (!putback_buff.empty()) {
        callback(ChunkedStream::read_sync_nb());
        return;
    }

    if (chunk_size) {
        _ref->read_async([=, cb = std::move(callback)](std::string_view data) mutable {
           if (data.empty()) {
               cb(data);
           } else {
               auto s = data.substr(0, chunk_size);
               auto t = data.substr(s.size());
               _ref.put_back(t);
               chunk_size -= s.size();
               cb(s);
           }
        });
    } else if (read_closed) {
        callback(std::string_view());
    } else {
        _ref.get_line_async("\r\n", [=, cb = std::move(callback)](bool ok, std::string &ln) mutable {
            if (ok) {
                if (ln.empty()) read_async(std::move(cb));
                else {
                    try {
                        chunk_size = parseChunkLine(ln);
                        if (chunk_size == 0) {
                            read_closed = true;
                            cb(std::string_view());
                        } else {
                            read_async(std::move(cb));
                        }
                    } catch (...) {
                        read_closed = true;
                        cb(std::string_view());
                    }
                }
            } else {
                read_closed = true;
                cb(std::string_view());
            }
        });
    }
}

template<typename Fn>
static void toHex(std::size_t v, int z, Fn &&fn) {
    if (v || z>0) {
        toHex(v>>4, z-1, std::forward<Fn>(fn));
        auto x = v & 0xF;
        fn(x>9?'A'+(x-10):'0'+x);
    }
}


std::vector<char> ChunkedStream::createChunk(const std::string_view &data) {
    std::vector<char> buff;
    buff.reserve(data.size()+20);
    std::size_t sz = data.size();
    toHex(sz,1,[&](char c){buff.push_back(c);});
    buff.push_back('\r');
    buff.push_back('\n');
    std::copy(data.begin(), data.end(), std::back_inserter(buff));
    buff.push_back('\r');
    buff.push_back('\n');
    return buff;

}

void ChunkedStream::write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) {
    if (write_closed) {
        callback(false);
    }else if (buffer.empty()) {
        //generate empty write cycle to achieve invoking the callback
        _ref->write_async(buffer, false, std::move(callback));
    } else {
        auto chunk = createChunk(buffer);
        //pack data to chunk, do not copy content, we move chunk into callback
        _ref->write_async(std::string_view(chunk.data(), chunk.size()), false,
             [chunk = std::move(chunk), cb = std::move(callback)](bool ok) mutable {
                cb(ok);
        });
    }
}

int ChunkedStream::get_read_timeout() const {
    return _ref->get_read_timeout();
}


std::string_view ChunkedStream::read_sync_nb() {
    std::string_view s = putback_buff;
    putback_buff = std::string_view();
    return s;

}

std::string_view ChunkedStream::read_sync() {
    if (!putback_buff.empty()) return ChunkedStream::read_sync_nb();

    if (chunk_size) {
        auto data = _ref->read_sync();
        if (data.empty()) {
            return data;
        } else {
            auto s = data.substr(0, chunk_size);
            auto t = data.substr(s.size());
            _ref.put_back(t);
            chunk_size -= s.size();
            return s;
        }
    } else if (read_closed) {
        return std::string_view();
    } else {
        std::string buffer;
        while(true) {
            if (!_ref.get_line(buffer, "\r\n")) {
                std::string_view();
                read_closed = true;
            }
            if (!buffer.empty()) {
                chunk_size = parseChunkLine(buffer);
                if (chunk_size == 0) {
                    read_closed = true;
                    return std::string_view();
                } else {
                    return read_sync();
                }
            }
        }
    }
}

void ChunkedStream::flush_async(Callback<void(bool)> &&cb) {
    if (write_closed) {
        cb(false);
    } else {
        auto buff = _ref->discard_put_buffer();
        if (buff.empty()) cb(true); else write_async(std::string_view(buff.data(), buff.size()), true, std::move(cb));
    }
}

void ChunkedStream::clear_timeout() {
    _ref->clear_timeout();
}

int ChunkedStream::get_write_timeout() const {
    return _ref->get_write_timeout();
}

void ChunkedStream::close_input() {
    while (chunk_size) {
        read_sync();
    }
    read_closed = true;
}

bool ChunkedStream::timeouted() {
    return _ref->timeouted();
}

void ChunkedStream::close_output() {
    if (!write_closed) {
        flush_sync();
        std::promise<bool> p;
        _ref->write_async("0\r\n\r\n", false, [&](bool){
            p.set_value(true);
        });
        p.get_future().wait();
        write_closed = true;
    }
}

void ChunkedStream::set_rw_timeout(int tm_in_ms) {
    _ref->set_rw_timeout(tm_in_ms);
}

void ChunkedStream::set_read_timeout(int tm_in_ms) {
    _ref->set_read_timeout(tm_in_ms);
}

bool ChunkedStream::flush_sync() {
    if (write_closed) {
        return false;
    } else {
        auto v = _ref.discard_put_buffer();
        return write_sync(std::string_view(v.data(), v.size()));
    }
}

void ChunkedStream::set_write_timeout(int tm_in_ms) {
    _ref->set_write_timeout(tm_in_ms);
}

bool ChunkedStream::put(char c) {
    return _ref->put(c);
}

bool ChunkedStream::put(const std::string_view &block) {
    return _ref->put(block);
}

bool ChunkedStream::write_sync(const std::string_view &buffer) {
    if (write_closed) return false;
    else if (buffer.empty()) return true;
    else {
        auto chunk = createChunk(buffer);
        return _ref.write_sync(std::string_view(chunk.data(), chunk.size()));
    }
}

void ChunkedStream::timeout_async_read() {
    _ref->timeout_async_read();
}

std::size_t ChunkedStream::get_put_size() const {
    return _ref->get_put_size();
}

std::vector<char> ChunkedStream::discard_put_buffer() {
    return _ref->discard_put_buffer();
}

}
