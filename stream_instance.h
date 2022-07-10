/*
 * stream_instance.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_STREAM_INSTANCE_H_
#define SRC_LIBS_USERVER_STREAM_INSTANCE_H_

#include <userver/stream.h>

#include <future>

namespace userver {

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
        if (_put_back.empty()) {
            if (_read_buffer_need_expand) {
                expand_read_buffer();
                _read_buffer_need_expand = false;
            }
            getContent()->read(_read_buffer.data(), _read_buffer.size(),
                              [this, cb = std::move(callback)](int r){
                _read_buffer_need_expand = r == _read_buffer.size();
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

    struct Line {
        std::string_view data;
        std::vector<char> buffer;
        Callback<void(bool)> cb;
        Line *next = nullptr;

        ~Line() {
            //next->next == next - marks statically allocated object (tail)
            if (next && next->next != next) delete next;
        }
    };

    std::atomic<Line *> _cur_line = std::atomic<Line *>(nullptr);
    std::atomic<bool> _async_write_closed = std::atomic<bool>(false);

    Line _first_line;

    virtual bool write_sync(const std::string_view &buffer) override {
        if (buffer.empty()) return true;
        std::size_t r = getContent()->write(buffer.data(), buffer.size());
        return r?write_sync(buffer.substr(r)):false;
    }

    static void set_line_content(Line &ln, const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) {
        if (copy_content) {
            ln.buffer.clear();
            std::copy(buffer.begin(), buffer.end(), std::back_inserter(ln.buffer));
            ln.data = std::string_view(ln.buffer.data(), ln.buffer.size());
        } else {
            ln.data = buffer;
        }
        ln.cb = std::move(callback);
    }

    virtual void write_async(const std::string_view &buffer, bool copy_content, Callback<void(bool)> &&callback) override {
        Line *top = nullptr;
        //check whether first line is unused
        if (_cur_line.compare_exchange_strong(top, &_first_line)) {
            //it is unused, we are first arriving - we also locked the first line, so
            //no other threads can be there
            set_line_content(_first_line, buffer, copy_content, std::move(callback));
            _first_line.next = nullptr;
            write_async_lines();
        } else {
            //we cannot lock the _first_line, so there is a pending operation
            //allocate new line
            std::unique_ptr<Line> l (std::make_unique<Line>());
            //initialize the line
            set_line_content(*l, buffer, copy_content, std::move(callback));
            //try to append the line to the current pending list lock-free
            do {
                //update next as old top
                l->next = top;
                //try to set new top if there is current top equal
            } while (!_cur_line.compare_exchange_strong(top, l.get()));
            //when done, release ownership, line is pending
            l.release();
            //if the previous top was null, pending operation already finished meanwhile
            //so this thread owns the queue, it must initiate transfer
            if (top == nullptr) {
                //initiate transfer
                write_async_lines_unlock();
            }
        }

    }

    void write_async_lines() noexcept {
        //in locked state
        if (_first_line.data.empty()) {
            _first_line.data = std::string_view("",1); //just dummy settings
            finish_write_first_line(1); //send 1 to avoid error
        } else if (_async_write_closed.load()) {
            finish_write_first_line(0); //send 0 as timeout
        } else {
            getContent()->write(_first_line.data.data(), _first_line.data.size(),[this](int r){
                update_flush_size(r, _first_line.data.size());
                finish_write_first_line(r);
            });
        }
    }

    void finish_write_first_line(int c) noexcept  {
        //in locked state
        bool ok = false;
        if (c) {
            _first_line.data = _first_line.data.substr(c);
            if (!_first_line.data.empty()) {
                write_async_lines();
                return;
            } else {
                ok = true;
            }
        }
        auto cb = std::move(_first_line.cb);
        write_async_lines_unlock();
        if (cb != nullptr) cb(ok);
    }

    void write_async_lines_unlock() noexcept  {
        //still locked
        //to unlock this, expect _first_line as _cur_line and set it to null
        Line *top = &_first_line;
        if (!_cur_line.compare_exchange_strong(top,nullptr)) {
            //we can't set _cur_line to null, so there is list
            //we need to get this list atomically, reset the list and keep locked
            //se we will swap the list with pointer to _first_line
            Line *t = _cur_line.exchange(&_first_line);
            //t contains current list, and _cur_line is reset
            //we need to reverse it
            Line *rt = nullptr;
            while (t && t != &_first_line) {
                //store next
                Line *x = t->next;
                //set next to rt
                t->next = rt;
                //make rt new top
                rt = t;
                //restore t to continue
                t = x;
            }
            write_async_chain_lines(std::unique_ptr<Line>(rt));
        }
    }


    void write_async_chain_lines(std::unique_ptr<Line> &&ln) {
        if (ln == nullptr) {
            write_async_lines_unlock();
        } else {
            const void *d = ln->data.data();
            std::size_t sz = ln->data.size();
            if (sz && !_async_write_closed.load()) {
                getContent()->write(d,sz,[=, ln= std::move(ln)](int r) mutable {
                    if (r) {
                        ln->data = ln->data.substr(r);
                        if (ln->data.empty()) {
                          if (ln->cb != nullptr) {
                              ln->cb(true);
                          }
                          Line *n = ln->next;
                          ln->next = nullptr;
                          ln = std::unique_ptr<Line>(n);
                        }
                        write_async_chain_lines(std::move(ln));
                    } else {
                        auto c = ln.get();
                        while (c) {
                            if (c->cb != nullptr) {
                                c->cb(false);
                            }
                            c = c->next;
                        }
                    }
                });
            } else {
                if (ln->cb!= nullptr) {
                    ln->cb(!_async_write_closed.load());
                }
                Line *n = ln->next;
                ln->next = nullptr;
                write_async_chain_lines(std::unique_ptr<Line>(n));
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


protected:  //character part

    std::vector<char> _chr_buffer;
    std::size_t flush_size_advice = 0;
    std::size_t flush_count = 0;

    virtual bool put(char c) override {
        _chr_buffer.push_back(c);
        return flush_count?_chr_buffer.size() > flush_size_advice/flush_count:_chr_buffer.size()>1024;
    }
    virtual bool put(const std::string_view &block) override {
        _chr_buffer.insert(_chr_buffer.end(), block.begin(), block.end());
        return flush_count?_chr_buffer.size() > flush_size_advice/flush_count:_chr_buffer.size()>1024;
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

    void update_flush_size(std::size_t r, std::size_t s) {
        if (r < s) flush_size_advice += r * 3/4;
        else {
            if (flush_count) {
                flush_size_advice += (flush_size_advice*4)/(flush_count*3);
            } else {
                flush_size_advice += r * 4/3;
            }
        }
        flush_count++;
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

protected:
    AbstractStreamInstance &ref;
};



template<typename T>
inline userver::StreamInstance<T>::~StreamInstance() {
    //destroying object in middle of pending operation is discouraged
    //however, some actions are performed

    //problem can be pending writes. Destroying stream without waiting for writes can
    //cause crash. Try to solve here

    //lock the write queue, if the queue is empty
    _async_write_closed = true;
    Line *top = nullptr;
    if (!_cur_line.compare_exchange_strong(top, &_first_line)) {
        //if lock failed, there were pending write
        //prepare a promise
        std::promise<bool> p;
        //enqueue empty write to the write queue, which will resolve the promise.
        //this would be the last 'write' in object's lifetime
        write_async(std::string_view(), false, [&](bool){
           p.set_value(true);
        });

        //short any write timneout to zero and terminate any pending wait
        StreamInstance<T>::timeout_async_write();
        //wait to future resolve
        //NOTE: all pending writes should be finished with timeout (false as result)
        //NOTE: this can hang, if the current thread is also async thread and there is no more async threads
        //NOTE: this can hang also if there are no async threads - however there is no way to pause this destructor
        p.get_future().wait();
    }
    //clear timeout
    getContent()->setIOTimeout(0);
    //cancel all pending actions without calling callback
    getContent()->cancelAsyncRead(false);
    getContent()->cancelAsyncWrite(false);

    getContent()->~T();

}


}

#endif /* SRC_LIBS_USERVER_STREAM_INSTANCE_H_ */

