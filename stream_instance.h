/*
 * stream_instance.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_STREAM_INSTANCE_H_
#define SRC_LIBS_USERVER_STREAM_INSTANCE_H_

#include <shared/trailer.h>
#include <userver/stream.h>

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



    virtual ~StreamInstance();
protected:  //content


    T _target;

protected:  //read part
    std::vector<char> _read_buffer;
    bool _read_buffer_need_expand = true;
    std::string_view _put_back;
    std::atomic<int> _pending_read = 0;  //<non-zero means there is pending read active
    std::promise<void> *_async_read_join = nullptr; //<used during destruction to join reads
    static constexpr int half_range = std::numeric_limits<int>::max()/2;

#ifndef NDEBUG
    std::atomic<int> _reading = 0;
#endif

    void expand_read_buffer() {
        auto sz = _read_buffer.size();
        _read_buffer.clear();
        sz = std::max<std::size_t>(1000, sz * 3 / 2);
        _read_buffer.resize(sz, 0);
    }

    virtual std::string_view read_sync() override {
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
            return std::string_view(_read_buffer.data(), r);
        } else {
            return StreamInstance::read_sync_nb();
        }
    }

    virtual std::string_view read_sync_nb() override {
        auto s = _put_back;
        _put_back = std::string_view();
        return s;
    }



    virtual void read_async(Callback<void(std::string_view)> &&callback) override {
    	//initialize trailer - handles decreasing pending reads and join the reading
    	//trailer is executed when this function exits, but
    	//can be carried to the async call
    	auto t = ondra_shared::trailer([this]{
        	if ((_pending_read.fetch_sub(1, std::memory_order_relaxed))-1 == half_range) {
        	    _pending_read.fetch_add(0, std::memory_order_seq_cst);
        		_async_read_join->set_value();
        	}
    	});
    	if (_pending_read.fetch_add(1, std::memory_order_relaxed) >= half_range) { //this means, that reading is no more possible
    		clear_timeout(); //we are going to report eof, so timeout must be cleared
        	callback(std::string_view());
    	} else if (_put_back.empty()) {
				if (_read_buffer_need_expand) {
					expand_read_buffer();
					_read_buffer_need_expand = false;
				}
#ifndef NDEBUG
            assert(_reading.fetch_add(1, std::memory_order_relaxed) == 0); //multiple pending reading
#endif
				_target.read(_read_buffer.data(), _read_buffer.size(),
						[this, cb = std::move(callback), t= std::move(t)] (int r){
#ifndef NDEBUG
            _reading.fetch_sub(1, std::memory_order_relaxed);
#endif
					_read_buffer_need_expand = static_cast<std::size_t>(r) == _read_buffer.size();
					cb(std::string_view(_read_buffer.data(), r));
				});
		} else {
			callback(StreamInstance::read_sync_nb());
		}
    }

    virtual void put_back(const std::string_view &buffer) override {
        _put_back = buffer;
    }
    virtual void close_input() override {
        _target.closeInput();
    }

    virtual void timeout_async_read() {
        _target.setRdTimeout(0);
        _target.cancelAsyncRead(true);
    }


protected:  //write part


    std::atomic<int> _pending_write = 0;
    std::promise<void> *_async_write_join = nullptr; //<used during destruction to join writes
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
        if (_write_error.load(std::memory_order_relaxed)) {
            callback(false);
            return false;
        }
        if (buffer.empty()) {
            callback(true);
            return !_write_error.load(std::memory_order_relaxed);
        }

        auto t = ondra_shared::trailer([this]{
            if ((_pending_write.fetch_sub(1, std::memory_order_relaxed))-1 == half_range) {
                _pending_write.fetch_add(0, std::memory_order_seq_cst);
                _async_write_join->set_value();
            }
        });

        if (_pending_write.fetch_add(1, std::memory_order_relaxed) >= half_range) {
            callback(false);
            return false;
        }

#ifndef NDEBUG
            assert(_writing.fetch_add(1, std::memory_order_relaxed) == 0); //multiple pending reading
#endif
        _target.write(buffer.data(), buffer.size(),
                 [this, t = std::move(t), buffer = std::string_view(buffer), callback = std::move(callback)](int r) mutable {
#ifndef NDEBUG
            _writing.fetch_sub(1, std::memory_order_relaxed);
#endif
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


    virtual void close_output() {
        if (_write_error.load(std::memory_order_relaxed)) return;
#ifndef NDEBUG
            assert(_writing.fetch_add(1, std::memory_order_relaxed) == 0); //multiple pending reading
#endif
        _target.closeOutput();
#ifndef NDEBUG
            _writing.fetch_sub(1, std::memory_order_relaxed);
#endif
    }

    virtual void timeout_async_write() {
        _target.setWrTimeout(0);
        _target.cancelAsyncWrite(true);
    }



protected:  //misc part
    virtual bool timeouted() {return _target.timeouted();}
    virtual void clear_timeout() {_target.clearTimeout();}
    virtual void set_read_timeout(int tm_in_ms) override {_target.setRdTimeout(tm_in_ms);}
    virtual void set_write_timeout(int tm_in_ms) override {_target.setWrTimeout(tm_in_ms);}
    virtual void set_rw_timeout(int tm_in_ms) override {_target.setIOTimeout(tm_in_ms);}
    virtual int get_read_timeout() const override {return _target.getRdTimeout();}
    virtual int get_write_timeout() const override {return _target.getWrTimeout();}
    virtual std::unique_ptr<AbstractStreamInstance> create_buffered();


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
            userver::Callback<void(std::basic_string_view<char>)> &&callback)
                    override {ref.read_async(std::move(callback));}
    virtual bool write_async(const std::string_view &buffer,
            userver::Callback<void(bool)> &&callback) override {
        return ref.write_async(buffer, std::move(callback));
    }
    virtual int get_read_timeout() const override {
        return ref.get_read_timeout();
    }
    virtual std::string_view read_sync() override {
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
    virtual bool timeouted() override {
        return ref.timeouted();
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
inline userver::StreamInstance<T>::~StreamInstance() {

	//disable read and signal pending read to not trigger promise yet
	// however if there is no pending read (half_range + 1), no
	//action is needed
	//setting atomic to this value prevents further reading
	if (_pending_read.fetch_add(half_range+1, std::memory_order_relaxed) > 0) {
		//there is pending read, we must explicitly join
		std::promise<void> join_read;
		//set promise ptr
		_async_read_join = &join_read;
		//decrease 1 from pending read, and if there is some reading...
		if (_pending_read.fetch_sub(1, std::memory_order_seq_cst)-1 > half_range) {
			//interrupt it
			StreamInstance<T>::timeout_async_read();
			//wait until promise is resolved
			join_read.get_future().wait();
		}
	}

    //disable write and signal pending write to not trigger promise yet
    // however if there is no pending write (half_range + 1), no
    //action is needed
    //setting atomic to this value prevents further writing
    if (_pending_write.fetch_add(half_range+1, std::memory_order_relaxed) > 0) {
        //there is pending read, we must explicitly join
        std::promise<void> join_write;
        //set promise ptr
        _async_write_join = &join_write;
        //decrease 1 from pending read, and if there is some reading...
        if (_pending_write.fetch_sub(1, std::memory_order_seq_cst)-1 > half_range) {
            //interrupt it
            StreamInstance<T>::timeout_async_write();
            //wait until promise is resolved
            join_write.get_future().wait();
        }
    }
}


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
        if (this->_write_error || _close_on_flush) return false;
        return write_lk(buffer, std::move(callback));
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

protected:

    using FlushList = std::vector<Callback<void(bool)> >;


    mutable std::recursive_mutex _mx;
    std::vector<char> _buffer;
    FlushList _flush_list;
    bool _pending_write = false;
    bool _close_on_flush = false;

    void finish_write(bool ok) {
        FlushList tmp;
        {
            std::lock_guard _(_mx);
            std::swap(tmp,_flush_list);
            if (!ok) this->_write_error = true;
            if (ok && !_buffer.empty()) {
                std::string_view ss(_buffer.data(), _buffer.size());
                StreamInstance<T>::write_async(ss,
                               [=, b = std::move(_buffer), tmp = std::move(tmp)](bool ok) {
                    for (const auto &cb : tmp) {
                        cb(ok);
                    }
                    return finish_write(ok);
                });
            } else {
                _pending_write = false;
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
            _pending_write = true;
            std::string_view ss(_buffer.data(), _buffer.size());
            StreamInstance<T>::write_async(ss,
                    [=, b = std::move(_buffer), callback = std::move(callback)](bool ok){
                if (callback != nullptr) callback(ok);
                finish_write(ok);
            });
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

