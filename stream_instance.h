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

namespace _details {

struct BufChainAlloc {
	std::size_t _sz;
	char *_ptr;
};

///helper class to store chain of pending writes
/** each instance holds reference or copy of the buffer
 * and pointer to next. There is also instance marked as static allocated
 * which cannot be released
 *
 */
class BufChain {
public:
	BufChain(BufChainAlloc &al, Callback<void(bool)> &&cb)
		:data(al._ptr, al._sz)
		,cb(std::move(cb))
		,next(nullptr)
		,static_alloc(false) {}

	BufChain():next(nullptr),static_alloc(true) {}
	BufChain(const std::string_view &data, Callback<void(bool)> &&cb):
		data(data),
		cb(std::move(cb)),
		next(nullptr),
		static_alloc(false) {}
	BufChain(const BufChain &other) = delete;
	BufChain &operator=(const BufChain &other) = delete;

	void update(const std::string_view &data, Callback<void(bool)> &&cb) {
		this->data = data;
		this->cb = std::move(cb);
	}

	void call_cb(bool res) {
		auto cb = std::move(this->cb);
		if (cb != nullptr) cb(res);
	}

	struct Deleter {
		void operator()(BufChain *x) {
			if (x) x->release();
		}
	};

	std::string_view data;
    Callback<void(bool)> cb;
	BufChain *next;
	bool static_alloc;

	void release() {
		if (next) {
			next->release();
			next = nullptr;
		}
		if (!static_alloc)
			delete this;
	}

	void *operator new(std::size_t sz) {
		return ::operator new(sz);
	}

	void *operator new(std::size_t sz, BufChainAlloc &al) {
		char *ptr = static_cast<char *>(::operator new(sz+al._sz));
		al._ptr = ptr+sz;
		return ptr;
	}
	void operator delete(void *ptr, BufChainAlloc &al ) {
		::operator delete(ptr);
	}
	void operator delete(void *ptr, std::size_t sz) {
		::operator delete(ptr);
	}

	static BufChain *lockPtr() {
		static BufChain lockInst;
		return &lockInst;
	}

	static bool advance(std::unique_ptr<BufChain, Deleter> &ptr) {
		auto n = ptr->next;
		ptr->next = nullptr;
		ptr = std::unique_ptr<BufChain, Deleter>(n);
		return ptr != nullptr;
	}

	static std::unique_ptr<BufChain, Deleter> allocCopy(const std::string_view &data, Callback<void(bool)> &&cb) {
		BufChainAlloc r{data.size()};
		BufChain *ret = new(r) BufChain(r, std::move(cb));
		std::copy(data.begin(), data.end(), r._ptr);
		return std::unique_ptr<BufChain, Deleter>(ret);
	}

	static std::unique_ptr<BufChain, Deleter> allocRef(const std::string_view &data, Callback<void(bool)> &&cb) {
		return std::unique_ptr<BufChain, Deleter>(new BufChain(data, std::move(cb)));
	}

};

using PBufChain = std::unique_ptr<BufChain, BufChain::Deleter>;


}


template<typename T>
class StreamInstance: public AbstractStreamInstance {
public:

    template<typename ... Args>
    StreamInstance(Args &&... args) {
        new(&_buffer) T(std::forward<Args>(args)...);
        //special flag stops deleting on statically allocated object
        _first_line.next = &_first_line;
    }

    StreamInstance(const StreamInstance &other) = delete;
    StreamInstance &operator=(const StreamInstance &other) = delete;


    T *operator->() {return getContent();}
    const T *operator->() const {return getContent();}



    virtual ~StreamInstance();
protected:  //content

    T *getContent() {return reinterpret_cast<T *>(_buffer);}
    const T *getContent() const {return reinterpret_cast<const T *>(_buffer);}


    char _buffer[sizeof(T)];

protected:  //read part
    std::vector<char> _read_buffer;
    bool _read_buffer_need_expand = true;
    std::string_view _put_back;
    std::atomic<int> _pending_read = 0;  //<non-zero means there is pending read active
    std::promise<bool> *_async_read_join = nullptr; //<used during destruction to join reads
    static constexpr int half_range = std::numeric_limits<int>::max()/2;

    void expand_read_buffer() {
        auto sz = _read_buffer.size();
        _read_buffer.clear();
        sz = std::max<std::size_t>(1000, sz * 3 / 2);
        _read_buffer.resize(sz, 0);
    }

    std::string_view pop_put_back() {
        auto s = _put_back;
        _put_back = std::string_view();
        return s;
    }

    virtual std::string_view read_sync() override {
        if (_put_back.empty()) {
            if (_read_buffer_need_expand) {
                expand_read_buffer();
                _read_buffer_need_expand = false;
            }
            std::size_t r = getContent()->read(_read_buffer.data(), _read_buffer.size());
            _read_buffer_need_expand = r == _read_buffer.size();
            return std::string_view(_read_buffer.data(), r);
        } else {
            return pop_put_back();
        }
    }



    virtual void read_async(Callback<void(std::string_view)> &&callback) override {
    	//initialize trailer - handles decreasing pending reads and join the reading
    	//trailer is executed when this function exits, but
    	//can be carried to the async call
    	auto t = ondra_shared::trailer([this]{
        	if (!(--_pending_read & ~half_range)) {
        		_async_read_join->set_value(true);
        	}
    	});
    	if (++_pending_read > half_range) { //this means, that reading is no more possible
    		clear_timeout(); //we are going to report eof, so timeout must be cleared
        	callback(std::string_view());
    	} else if (_put_back.empty()) {
				if (_read_buffer_need_expand) {
					expand_read_buffer();
					_read_buffer_need_expand = false;
				}
				getContent()->read(_read_buffer.data(), _read_buffer.size(),
						[this, cb = std::move(callback), t= std::move(t)] (int r){
					_read_buffer_need_expand = static_cast<std::size_t>(r) == _read_buffer.size();
					cb(std::string_view(_read_buffer.data(), r));
				});
		} else {
			callback(pop_put_back());
		}
    }

    virtual void put_back(const std::string_view &buffer) override {
        _put_back = buffer;
    }
    virtual void close_input() override {
        getContent()->closeInput();
    }

    virtual void timeout_async_read() {
        getContent()->setRdTimeout(0);
        getContent()->cancelAsyncRead(true);
    }


protected:  //write part


    using Line = _details::BufChain;
    using PLine = _details::PBufChain;

    std::atomic<Line *> _cur_line = std::atomic<Line *>(nullptr);
    std::atomic<std::size_t> _pending_writes;
    std::atomic<bool> _async_write_join = false;
    Line _first_line;

    virtual bool write_sync(const std::string_view &buffer) override {
        if (buffer.empty()) return true;
        std::size_t r = getContent()->write(buffer.data(), buffer.size());
        return r?write_sync(buffer.substr(r)):false;
    }

    virtual void write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) override {
    	if (_async_write_join.load()) {
    			callback(false);
    	} else {
    		write_async2(buffer,copy_content,std::move(callback));
    	}
    }
    void write_async2(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback)  {
    	_pending_writes+=buffer.size();
    	//if not copy_content - we can prepare buffer without allocation and copying
    	if (!copy_content) {
    		//try to lock for first line - cur queue must be empty
    		Line *top = nullptr;
    		if (_cur_line.compare_exchange_strong(top, &_first_line)) {
    			//we locked, we are using _first_line
    			//update first line
    			_first_line.update(buffer, std::move(callback));
    			//send queue
    			write_async_send_queue(true);
    			//return here
    			return;
    		}
    	}
    	//allocate depend on copy_content
		auto b = copy_content
				?Line::allocCopy(buffer, std::move(callback))
				:Line::allocRef(buffer, std::move(callback));
		//enqueue the next item
		auto r = b->next = _cur_line;
		while (!_cur_line.compare_exchange_weak(b->next, b.get())) {
			r = b->next;
		}
		b.release();
		//if it is first item, start operation
		if (r == nullptr) {
			write_async_send_queue(true);
		} //otherwise someone else will handle
    }


    void write_async_send_queue(bool status) {
    	auto lk = Line::lockPtr();
    	//first lock queue and reorder
    	auto top = _cur_line.exchange(lk);
    	Line *send_queue = nullptr;
    	//stop on null or lockPtr
    	while (top != nullptr && top != lk) {
    		//reverse order of queue
    		auto n = top->next;
    		top->next= send_queue;
    		send_queue = top;
    		top = n;
    	}
    	//flush this queue
    	write_async_send_queue_next(PLine(send_queue), status);
    }

    void write_async_send_queue_next(PLine &&send_queue, bool status) noexcept {
    	//send_queue shouldn't be null there, however ...
    	if (send_queue != nullptr) {
    		//if status is not good, or data are empty - handle callback
    		if (!status || send_queue->data.empty() || _async_write_join.load()) {
    			_pending_writes-= send_queue->data.size();
    			//pick callback and store it outside
    			auto cb = std::move(send_queue->cb);
    			//advance send_queue ptr - if it is not null
    			if (Line::advance(send_queue)) {
    				//if callback is defined, call it
    				if (cb!=nullptr) cb(status);
    				//recursive continue with new queue status
    				write_async_send_queue_next(std::move(send_queue), status);
    			} else {
    				//queue is empty, unlock it
    				auto top = Line::lockPtr();
    				//try to unlock the queue - set queue to nullptr
    		    	if (!_cur_line.compare_exchange_strong(top,nullptr)) {
    		    		//if not success, there is still work to do
    		    		//call the callback now
    		    		if (cb!=nullptr) cb(status);
    		    		//continue with new queue
    		    		write_async_send_queue(status);
    		    	} else {
    		    		//unlock successful - we can call the last callback
    		    		if (cb!=nullptr) cb(status);
    		    	}
    			}
    		} else {
    			//data to send - asynchronously send them
				getContent()->write(send_queue->data.data(),
									send_queue->data.size(),
									[send_queue = std::move(send_queue), this](int r) mutable {
					if (r>0) {
						_pending_writes-= r;
						//some data sent, commit the buffer
						send_queue->data = send_queue->data.substr(r);
						//repeat operation with success state
						write_async_send_queue_next(std::move(send_queue), true);
					} else {
						//timeout or error - repeat operation with failed states
						write_async_send_queue_next(std::move(send_queue), false);
					}
				});
    		}
    	} else {
    		//in this case. just try to unlock
			auto top = Line::lockPtr();
	    	if (!_cur_line.compare_exchange_strong(top,nullptr)) {
	    		write_async_send_queue(status);
	    	}
    	}
    }

    virtual void close_output() {
        write_async(std::string_view(), false, [this](bool) {
            getContent()->closeOutput();
        });
    }

    virtual void timeout_async_write() {
        getContent()->setWrTimeout(0);
        getContent()->cancelAsyncWrite(true);
    }


    virtual std::size_t get_pending_write_size() const {
    	return _pending_writes;
    }


protected:  //character part

    std::vector<char> _chr_buffer;
    static std::size_t flush_advice_size;

    virtual bool put(char c) override {
        _chr_buffer.push_back(c);
        return _chr_buffer.size() > flush_advice_size;
    }
    virtual bool put(const std::string_view &block) override {
        _chr_buffer.insert(_chr_buffer.end(), block.begin(), block.end());
        return _chr_buffer.size() > flush_advice_size;
    }
    virtual bool flush_sync() override {
        bool ret = true;
        if (!_chr_buffer.empty()) {
            ret = write_sync(std::string_view(_chr_buffer.data(), _chr_buffer.size()));
            _chr_buffer.clear();
        }
        return ret;
    }
    virtual void flush_async(Callback<void(bool)> &&cb) override {
        if (!_chr_buffer.empty()) {
            write_async(std::string_view(_chr_buffer.data(), _chr_buffer.size()), false,
                        [this, cb = std::move(cb)](bool x){
                _chr_buffer.clear();
                if (cb != nullptr) cb(x);
            });
        } else {
            cb(true);
        }
    }
    virtual std::size_t get_put_size() const override {
        return _chr_buffer.size();
    }

    virtual std::vector<char> discard_put_buffer() override {
        return std::move(_chr_buffer);
    }


protected:  //misc part
    virtual bool timeouted() {return getContent()->timeouted();}
    virtual void clear_timeout() {getContent()->clearTimeout();}
    virtual void set_read_timeout(int tm_in_ms) override {getContent()->setRdTimeout(tm_in_ms);}
    virtual void set_write_timeout(int tm_in_ms) override {getContent()->setWrTimeout(tm_in_ms);}
    virtual void set_rw_timeout(int tm_in_ms) override {getContent()->setIOTimeout(tm_in_ms);}
    virtual int get_read_timeout() const override {return getContent()->getRdTimeout();}
    virtual int get_write_timeout() const override {return getContent()->getWrTimeout();}


};

template<typename T>
std::size_t StreamInstance<T>::flush_advice_size = 4096;

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

    virtual void put_back(const std::string_view &buffer) override {ref.put_back(buffer);}
    virtual void timeout_async_write() override {ref.timeout_async_write();}
    virtual void read_async(
            userver::Callback<void(std::basic_string_view<char>)> &&callback)
                    override {ref.read_async(std::move(callback));}
    virtual void write_async(const std::string_view &buffer, bool copy_content,
            userver::Callback<void(bool)> &&callback) override {
        ref.write_async(buffer, copy_content, std::move(callback));
    }
    virtual int get_read_timeout() const override {
        return ref.get_read_timeout();
    }
    virtual std::string_view read_sync() override {
        return ref.read_sync();
    }
    virtual void flush_async(userver::Callback<void(bool)> &&cb) override {
        return ref.flush_async(std::move(cb));
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
    virtual bool flush_sync() override {
        return ref.flush_sync();
    }
    virtual void set_write_timeout(int tm_in_ms) override {
        return ref.set_write_timeout(tm_in_ms);
    }
    virtual bool put(char c) override {
        return ref.put(c);
    }
    virtual bool put(const std::string_view &block) override {
        return ref.put(block);
    }
    virtual bool write_sync(const std::string_view &buffer) override {
        return ref.write_sync(buffer);
    }
    virtual void timeout_async_read() override {
        return ref.timeout_async_read();
    }
    virtual std::size_t get_put_size() const override {
        return ref.get_put_size();
    }

    virtual std::vector<char> discard_put_buffer() override {
        return ref.discard_put_buffer();
    }
    virtual std::size_t get_pending_write_size() const override {
    	return ref.get_pending_write_size();
    }

protected:
    AbstractStreamInstance &ref;
};



template<typename T>
inline userver::StreamInstance<T>::~StreamInstance() {

	//disable writes
	_async_write_join = true;
	//disable read and signal pending read to not trigger promise yet
	// however if there is no pending read (half_range + 1), no
	//action is needed
	//setting atomic to this value prevents futher reading
	if ((_pending_read+=half_range+1) > (half_range+1)) {
		//there is pending read, we must explicitly join
		std::promise<bool> join_read;
		//set promise ptr
		_async_read_join = &join_read;
		//decrease 1 from pending read, and if there is some reading...
		if (--_pending_read > half_range) {
			//interrupt it
			timeout_async_read();
			//wait until promise is resolved
			join_read.get_future().wait();
		}
	}

	//lock the queue - to detect, whether there is pending write operation
	auto lk = Line::lockPtr();
	//top = top most waiting operation
	auto top = _cur_line.exchange(lk);
	//if top is not nullptr, there is a pending write
	if (top != nullptr) {
		//there is pending write, we must explicitly join
		std::promise<bool> join_write;
		//prepare (statically allocated) line with empty buffer
		Line jpt;
		//set callback, which resolves promise on completion of the queue
		jpt.cb = [&](bool){
			join_write.set_value(true);
		};
		//set the line to queue.
		auto nt = _cur_line.exchange(&jpt);
		//nt must be lockPtr(). If it is nullptr, current pending
		//operation already completed
		if (nt) {
			//timeout any write
			timeout_async_write();
			//in case of non-null, wait for completion
			join_write.get_future().wait();
		}
		//there should be no pending write operation
		//delete the queue, invoke callbacks with 'false'
		while (top != nullptr && top != lk) {
			auto nx = top->next;
			top->next = nullptr;
			if (top->cb != nullptr) top->cb(false);
			top->release();
			top = nx;
		}
	}

	//now we are good
	//destroy the underlying socket
    getContent()->~T();
}


}

#endif /* SRC_LIBS_USERVER_STREAM_INSTANCE_H_ */

