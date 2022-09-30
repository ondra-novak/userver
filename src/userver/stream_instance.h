/*
 * stream_instance.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_STREAM_INSTANCE_H_
#define SRC_LIBS_USERVER_STREAM_INSTANCE_H_

#include <shared/trailer.h>
#include "stream.h"
#include "async_provider.h"

#include <future>

namespace userver {

class AbstractBufferedFactory {
public:

    virtual ~AbstractBufferedFactory() = default;
    virtual std::unique_ptr<AbstractStreamInstance> create_buffered()  = 0;

};


template<typename T>
class StreamInstance: public AbstractStreamInstance, public AbstractBufferedFactory {
public:

    template<typename ... Args>
    StreamInstance(Args &&... args):_target(std::forward<Args>(args)...) {}

    StreamInstance(const StreamInstance &other) = delete;
    StreamInstance &operator=(const StreamInstance &other) = delete;


    T *operator->() {return &_target;}
    const T *operator->() const {return &_target;}



protected:  //content

    static constexpr int exit_range = std::numeric_limits<int>::max()/2;
    

    T _target;

protected:  //read part
    std::vector<char> _read_buffer;
    bool _read_buffer_need_expand = true;
    std::string_view _put_back;

#ifndef NDEBUG
    std::atomic<int> _reading = 0;
#endif

    void expand_read_buffer() {
        auto sz = _read_buffer.size();
        _read_buffer.clear();
        sz = std::max<std::size_t>(1000, sz * 3 / 2);
        _read_buffer.resize(sz, 0);
    }

    virtual ReadData read_sync() override {
        if (_put_back.empty()) {
            if (_read_buffer_need_expand) {
                expand_read_buffer();
                _read_buffer_need_expand = false;
            }
#ifndef NDEBUG
            assert(_reading.fetch_add(1, std::memory_order_relaxed) == 0); //multiple pending reading
#endif
            std::size_t r = _target.read(_read_buffer.data(), _read_buffer.size());
#ifndef NDEBUG
            _reading.fetch_sub(1, std::memory_order_relaxed);
#endif
            _read_buffer_need_expand = r == _read_buffer.size();
            if (r == 0 && _target.timeouted()) return ReadData(ReadData::timeout);
            return ReadData(_read_buffer.data(), r);
        } else {
            return StreamInstance::read_sync_nb();
        }
    }

    virtual std::string_view read_sync_nb() override {
        auto s = _put_back;
        _put_back = std::string_view();
        return s;
    }



    virtual void read_async(Callback<void(const ReadData &)> &&callback) override {
        if (!_put_back.empty()) {
            getCurrentAsyncProvider().runAsyncAsSync(std::move(callback),StreamInstance::read_sync_nb());
            return;
        }
        if (_read_buffer_need_expand) {
            expand_read_buffer();
            _read_buffer_need_expand = false;
        }
        _target.read(_read_buffer.data(), _read_buffer.size(),
                [this, cb = std::move(callback)] (int r){
        
            if (r == 0 && _target.timeouted()) {
                cb(ReadData(ReadData::timeout));
            } else {
                _read_buffer_need_expand = static_cast<std::size_t>(r) == _read_buffer.size();
                cb(ReadData(_read_buffer.data(), r));
            }
        });
        
    }

    virtual void put_back(const std::string_view &buffer) override {
        _put_back = buffer;
    }
    virtual void close_input() override {
        _target.closeInput();
    }

    virtual void timeout_async_read() override {
        _target.setRdTimeout(0);
        _target.cancelAsyncRead(true);
    }


protected:  //write part

    std::atomic<bool> _write_error = false;
#ifndef NDEBUG
    std::atomic<int> _writing= 0;
#endif
    virtual bool write_sync(const std::string_view &buffer) override {
        std::string_view b(buffer);
        if (_write_error.load(std::memory_order_relaxed)) return false;
#ifndef NDEBUG
            assert(_writing.fetch_add(1, std::memory_order_relaxed) == 0); //multiple pending reading
#endif
        while (!b.empty()) {
            std::size_t r = _target.write(buffer.data(), buffer.size());
            if (r < 1) {
                _write_error.store(true, std::memory_order_relaxed);
                return false;
            }
            b = b.substr(r);
        }

#ifndef NDEBUG
       _writing.fetch_sub(1, std::memory_order_relaxed);
#endif
        return true;
    }

    virtual bool write_async(const std::string_view &buffer, Callback<void(bool)> &&callback) override {        
        if (_write_error.load(std::memory_order_release)) {
            callback(false);
            return false;
        }
        if (buffer.empty()) {
            callback(true);
            return !_write_error.load(std::memory_order_relaxed);
        }


        _target.write(buffer.data(), buffer.size(),
                 [this, buffer = std::string_view(buffer), callback = std::move(callback)](int r) mutable {
            if (r>0) {
                std::size_t len = r;
                if (len < buffer.size()) {
                    StreamInstance<T>::write_async(buffer.substr(len), std::move(callback));
                } else {
                    callback(true);
                }
            } else {
                _write_error.store(true,std::memory_order_relaxed);
                callback(false);
            }
        });
        return true;
    }


    virtual void close_output() override {
        if (_write_error.load(std::memory_order_relaxed)) return;
#ifndef NDEBUG
            assert(_writing.fetch_add(1, std::memory_order_relaxed) == 0); //multiple pending reading
#endif
        _target.closeOutput();
#ifndef NDEBUG
            _writing.fetch_sub(1, std::memory_order_relaxed);
#endif
    }

    virtual void timeout_async_write() override {
        _target.setWrTimeout(0);
        _target.cancelAsyncWrite(true);
    }



protected:  //misc part    
    virtual void clear_timeout() override {_target.clearTimeout();}
    virtual void set_read_timeout(int tm_in_ms) override {_target.setRdTimeout(tm_in_ms);}
    virtual void set_write_timeout(int tm_in_ms) override {_target.setWrTimeout(tm_in_ms);}
    virtual void set_rw_timeout(int tm_in_ms) override {_target.setIOTimeout(tm_in_ms);}
    virtual int get_read_timeout() const override {return _target.getRdTimeout();}
    virtual int get_write_timeout() const override {return _target.getWrTimeout();}
    virtual std::unique_ptr<AbstractStreamInstance> create_buffered() override;


    void cleanup_pending();
};


class StreamSocketWrapper: public std::unique_ptr<ISocket> {
public:

    StreamSocketWrapper(std::unique_ptr<ISocket> &&other):std::unique_ptr<ISocket>(std::move(other)) {}

    using std::unique_ptr<ISocket>::unique_ptr;

    int read(void *buffer, std::size_t size) {return (*this)->read(buffer, size);}
    int write(const void *buffer, std::size_t size) {return (*this)->write(buffer, size);}
    void read(void *buffer, std::size_t size, CallbackT<void(int)> &&fn) {(*this)->read(buffer, size, std::move(fn));}
    void write(const void *buffer, std::size_t size, CallbackT<void(int)> &&fn) {(*this)->write(buffer, size, std::move(fn));}
    bool cancelAsyncRead(bool set_timeouted = true) {return (*this)->cancelAsyncRead(set_timeouted);}
    bool cancelAsyncWrite(bool set_timeouted = true) {return (*this)->cancelAsyncWrite(set_timeouted);}
    void closeOutput() {(*this)->closeOutput();}
    void closeInput() {(*this)->closeInput();}
    void setRdTimeout(int tm) {(*this)->setRdTimeout(tm);}
    void setWrTimeout(int tm) {(*this)->setWrTimeout(tm);}
    void setIOTimeout(int tm) {(*this)->setIOTimeout(tm);}
    int getRdTimeout() const {return (*this)->getRdTimeout();}
    int getWrTimeout() const {return (*this)->getWrTimeout();}
    bool timeouted() const {return (*this)->timeouted();}
    void clearTimeout() {return (*this)->clearTimeout();}
};


class StreamReferenceWrapper: public AbstractStreamInstance {
public:
    StreamReferenceWrapper(AbstractStreamInstance &ref):ref(ref) {}
    StreamReferenceWrapper(const StreamReferenceWrapper &) = default;
    StreamReferenceWrapper &operator=(const StreamReferenceWrapper &) = delete;

    virtual void put_back(const std::string_view &buffer) override {ref.put_back(buffer);}
    virtual void timeout_async_write() override {ref.timeout_async_write();}
    virtual void read_async(
            userver::Callback<void(const ReadData &)> &&callback)
                    override {ref.read_async(std::move(callback));}
    virtual bool write_async(const std::string_view &buffer,
            userver::Callback<void(bool)> &&callback) override {
        return ref.write_async(buffer, std::move(callback));
    }
    virtual int get_read_timeout() const override {
        return ref.get_read_timeout();
    }
    virtual ReadData read_sync() override {
        return ref.read_sync();
    }
    virtual std::string_view read_sync_nb() override {
        return ref.read_sync_nb();
    }
    virtual void clear_timeout() override {
        ref.clear_timeout();
    }
    virtual int get_write_timeout() const override {
        return ref.get_write_timeout();
    }
    virtual void close_input() override {
        ref.close_input();
    }
    virtual void close_output() override {
        ref.close_output();
    }
    virtual void set_rw_timeout(int tm_in_ms) override {
        ref.set_rw_timeout(tm_in_ms);
    }
    virtual void set_read_timeout(int tm_in_ms) override{
        ref.set_read_timeout(tm_in_ms);
    }
    virtual void set_write_timeout(int tm_in_ms) override {
        return ref.set_write_timeout(tm_in_ms);
    }
    virtual bool write_sync(const std::string_view &buffer) override {
        return ref.write_sync(buffer);
    }
    virtual void timeout_async_read() override {
        return ref.timeout_async_read();
    }
protected:
    AbstractStreamInstance &ref;
};





template<typename T>
class BufferedStreamInstance: public StreamInstance<T> {
public:
    using StreamInstance<T>::StreamInstance;

    virtual bool write_sync(const std::string_view &buffer) {
        std::unique_lock _(_mx);
        if (this->_write_error || _close_on_flush) return false;
        if (_pending_write) {
            std::promise<bool> p;
            write_async(buffer, [&](bool ok){
               p.set_value(ok);
            });
            _.unlock();
            return p.get_future().get();
        } else {
            bool ok = StreamInstance<T>::write_sync(buffer);
            this->_write_error = this->_write_error || !ok;
            return ok;
        }
    }
    virtual bool write_async(const std::string_view &buffer, Callback<void(bool)> &&callback) {
        std::unique_lock _(_mx);
        if (!write_lk(buffer, std::move(callback))) {
            _.unlock();
            if (callback != nullptr) callback(false);
            return false;
        } else {
            return true;
        }
    }

    virtual void close_output() {
        std::unique_lock _(_mx);
        if (this->_write_error || _close_on_flush) return;
        _close_on_flush = true;
        if (!_pending_write) {
            StreamInstance<T>::close_output();
        }
    }

    virtual std::size_t get_buffered_amount() const {
        std::unique_lock _(_mx);
        return _buffer.size();
    }

    ~BufferedStreamInstance() {
        std::condition_variable_any waiter;
        std::unique_lock _(_mx);
        if (_pending_write) {
            _exit_wait = &waiter;
            this->timeout_async_write();
            waiter.wait(_, [&]{return !_pending_write ;});
        }
    }

protected:

    using FlushList = std::vector<Callback<void(bool)> >;


    mutable std::recursive_mutex _mx;
    std::condition_variable_any *_exit_wait = nullptr;
    std::vector<char> _buffer;
    FlushList _flush_list;
    bool _pending_write = false;
    bool _close_on_flush = false;

    void finish_write(bool ok, std::vector<char> &&buffer) {
        FlushList tmp;
        {
            std::lock_guard _(_mx);
            std::swap(tmp,_flush_list);
            buffer.clear();
            std::swap(buffer, _buffer);
            if (!ok) this->_write_error = true;
            if (ok && !buffer.empty()) {
                std::string_view ss(buffer.data(), buffer.size());
                StreamInstance<T>::write_async(ss,
                               [=, this, b = std::move(buffer), tmp = std::move(tmp)](bool ok) mutable{
                    for (const auto &cb : tmp) {
                        cb(ok);
                    }
                    return finish_write(ok, std::move(b));
                });
            } else {
                if (buffer.capacity() > 0 && buffer.capacity() < _buffer.capacity()) {
                    std::swap(buffer, _buffer);
                }
                _pending_write = false;
                if (_exit_wait) _exit_wait->notify_one();
                if (_close_on_flush) StreamInstance<T>::close_output();
            }
        }
    }

    bool write_lk(const std::string_view &data, Callback<void(bool)> &&callback)  {
        if (this->_write_error || _close_on_flush) return false;
        _buffer.insert(_buffer.end(), data.begin(), data.end());
        if (_pending_write) {
            if (callback != nullptr) _flush_list.push_back(std::move(callback));
        } else {
            if (_buffer.empty()) {
                if (callback != nullptr) callback(true);
            } else {
                _pending_write = true;
                std::string_view ss(_buffer.data(), _buffer.size());
                StreamInstance<T>::write_async(ss,
                        [=, this, b = std::move(_buffer), callback = std::move(callback)](bool ok) mutable{
                    if (callback != nullptr) callback(ok);
                    finish_write(ok, std::move(b));
                });
            }
        }
        return true;
    }


};

template<typename T>
inline std::unique_ptr<AbstractStreamInstance> StreamInstance<T>::create_buffered(){
    return std::make_unique<BufferedStreamInstance<T> >(std::move(_target));
}


}

#endif /* SRC_LIBS_USERVER_STREAM_INSTANCE_H_ */

