/*
 * message_stream.h
 *
 *  Created on: 8. 8. 2022
 *      Author: ondra
 */

#ifndef _SRC_USERVER_MESSAGE_STREAM_H_
#define _SRC_USERVER_MESSAGE_STREAM_H_

#include "stream_instance.h"

namespace userver {

//not debugged

/// Message oriented stream
/**
 * It is built on buffered stream and allows to send and receive messages though the
 * stream. Read functions are able to receive whole message, so you don't need to
 * struggle with partially received data
 *
 * Note that internally the message begins with size of the message, which is transparent
 * for the API, but it must be taken into account, so if other size doesn't understand
 * this format, you can't use this stream
 *
 * Format of message:
 * +------------------+------------+
 * |  size (variable) |  payload   |
 * +------------------+------------+
 *
 * The size itself is variable stored in MSB first where only 7 bits of each octed
 * carries the information and MSB byte is set to 1 if there is additional byte, and 0
 * if it is last byte
 *
 * @code
 * 0x40     = 0x40
 * 0x7F     = 0x7F
 * 0x8A     = 0x81 0x0A
 * 0x123456 = 0xC8 0xE8 0x56
 * @endcode
 *
 * Maximum length of size is 8 bytes (max message size = 2^56-1)
 *
 * Similar to buffered stream, it is possible to write multiple messages from different
 * threads without waiting for completion. Messages are stored in a buffer until they
 * are send
 *
 * You can send empty messages, but other side can have a problem to distinguish between
 * an empty message and the end of stream as the both situations are reported as an empty string
 * (with timeouted() = false). This can be handled by function is_empty_message() which
 * returns true if the returned empty string is actually an empty message (and not the end of stream)
 */
template<typename T>
class MessageStream: public BufferedStreamInstance<T> {
public:
    using BufferedStreamInstance<T>::BufferedStreamInstance;


    virtual std::string_view read_sync() override;
    virtual std::string_view read_sync_nb() override;
    virtual void read_async(Callback<void(std::string_view)> &&callback) override;
    virtual void put_back(const std::string_view &buffer) override;
    virtual bool write_sync(const std::string_view &buffer) override;
    virtual bool write_async(const std::string_view &buffer, Callback<void(bool)> &&callback) override;

protected:
    enum class ReadStage {
        size,
        content
    };

    ReadStage _read_stage = ReadStage::size;
    std::size_t _input_remain = 0;
    std::string_view _put_back_msg;
    std::vector<char> _input_buffer;

    std::string _place_for_size;

    bool process_input(std::string_view &data, std::string_view &msg);
    std::string_view gen_size(std::size_t sz);
};

static bool is_empty_message(const std::string_view &msg) {
    return msg.empty() && msg.data() != 0;
}




template<typename T>
inline std::string_view MessageStream<T>::read_sync() {
    if (!_put_back_msg.empty()) return read_sync_nb();
    std::string_view data;
    std::string_view msg;
    do {
        data = BufferedStreamInstance<T>::read_sync();
        if (data.empty()) return data;
    } while (!process_input(data, msg));
    BufferedStreamInstance<T>::put_back(data);
    return msg;
}

template<typename T>
inline std::string_view MessageStream<T>::read_sync_nb() {
    auto r = _put_back_msg;
    _put_back_msg = std::string_view();
    return r;
}

template<typename T>
inline void MessageStream<T>::read_async(
        Callback<void(std::string_view)> &&callback) {
    BufferedStreamInstance<T>::read_async([this,cb = std::move(callback)](std::string_view data) {
        if (data.empty()) {
            cb(data);
        } else {
            std::string_view msg;
            if (process_input(data, msg)) {
                BufferedStreamInstance<T>::put_back(data);
                cb(msg);
            } else {
                MessageStream<T>::read_async(std::move(cb));
            }
        }
    });
}

template<typename T>
inline void MessageStream<T>::put_back(const std::string_view &buffer) {
    _put_back_msg = buffer;
}

template<typename T>
inline bool MessageStream<T>::write_sync(const std::string_view &buffer) {
    std::lock_guard _(this->_mx);
    std::string_view sz = gen_size(buffer.size());
    return BufferedStreamInstance<T>::write_sync(sz) && BufferedStreamInstance<T>::write_sync(buffer);
}

template<typename T>
inline bool MessageStream<T>::write_async(
        const std::string_view &buffer, Callback<void(bool)> &&callback) {
    std::unique_lock _(this->_mx);
    std::string_view sz = gen_size(buffer.size());
    bool st = BufferedStreamInstance<T>::write_lk(sz, nullptr) &&
            BufferedStreamInstance<T>::write_lk(buffer, std::move(callback));
    if (!st) {
        _.unlock();
        callback(false);
        return false;
    } else {
        return true;
    }
}

template<typename T>
inline bool MessageStream<T>::process_input(std::string_view &data, std::string_view &msg) {
    while (!data.empty()) {
        switch(_read_stage) {
            case ReadStage::size:
                _input_remain  = (_input_remain << 7) | (data[0] & 0x7f);
                data = data.substr(1);
                if ((data[0] & 0x80) == 0) {
                    if (_input_remain == 0) {
                        msg = std::string_view(data.data(), 0);
                        return true;
                    } else {
                        _read_stage = ReadStage::content;
                        _input_buffer.clear();
                    }
                }
                break;
            case ReadStage::content:
                msg = data.substr(0, _input_remain);
                _input_remain -= msg.size();
                data = data.substr(msg.size());
                if (_input_remain || !_input_buffer.empty()) {
                    _input_buffer.insert(_input_buffer.end(), msg.begin(), msg.end());
                    if (!_input_remain) {
                        msg = std::string_view(_input_buffer.data(), _input_buffer.size());
                        _read_stage = ReadStage::size;
                        return true;
                    }
                } else { //_input_remain == 0 && _input_buffer.empty()
                    //msg contains whole message
                    _read_stage = ReadStage::size;
                    return true;
                }
                break;
        }
    }
    return false;
}

template<typename T>
inline std::string_view MessageStream<T>::gen_size(std::size_t sz) {
    _place_for_size.clear();
    char c = static_cast<char>(sz & 0x7F);
    sz >>= 7;
    if (sz) {
        char c = static_cast<char>((sz & 0x7F) | 0x80);
        sz >>= 7;
        if (sz) {
            char c = static_cast<char>((sz & 0x7F) | 0x80);
            sz >>= 7;
            if (sz) {
                char c = static_cast<char>((sz & 0x7F) | 0x80);
                sz >>= 7;
                if (sz) {
                    char c = static_cast<char>((sz & 0x7F) | 0x80);
                    sz >>= 7;
                    if (sz) {
                        char c = static_cast<char>((sz & 0x7F) | 0x80);
                        sz >>= 7;
                        if (sz) {
                            char c = static_cast<char>((sz & 0x7F) | 0x80);
                            sz >>= 7;
                            if (sz) {
                                char c = static_cast<char>((sz & 0x7F) | 0x80);
                                sz >>= 7;
                                if (sz) {
                                    //max message size = 72057594037927935 (should be enough)
                                    //56 bits (7*8)
                                      throw std::runtime_error("Message is too large");
                                }
                                _place_for_size.push_back(c);
                            }
                            _place_for_size.push_back(c);
                        }
                        _place_for_size.push_back(c);
                    }
                    _place_for_size.push_back(c);
                }
                _place_for_size.push_back(c);
            }
            _place_for_size.push_back(c);
        }
        _place_for_size.push_back(c);
    }
    _place_for_size.push_back(c);
    return _place_for_size;
}


}

#endif /* _SRC_USERVER_MESSAGE_STREAM_H_ */
