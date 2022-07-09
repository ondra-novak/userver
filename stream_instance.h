/*
 * stream_instance.h
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */

#ifndef SRC_LIBS_USERVER_STREAM_INSTANCE_H_
#define SRC_LIBS_USERVER_STREAM_INSTANCE_H_

#include "stream2.h"

namespace userver {

template<typename T>
class StreamInstance: public AbstractStreamInstance {
public:

    template<typename ... Args>
    StreamInstance(Args &&... args) {
        new(&_buffer) T(std::forward<Args>(args)...);
    }



    T *operator->() {return getContent();}
    const T *operator->() const {return getContent();}



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
    };

    std::atomic<Line *> _cur_line = std::atomic<Line *>(nullptr);

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
        } else {
            getContent()->write(_first_line.data.data(), _first_line.data.size(),[this](int r){
                finish_write_first_line(r);
            });
        }
    }

    void finish_write_first_line(int c) noexcept  {
        //in locked state
        bool ok = false;
        if (c) {
            _first_line.data = _first_line.data.substr(c);
            if (!_first_line.buffer.empty()) {
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
            while (t) {
                //store next
                Line *x = t->next;
                //set next to rt
                t->next = rt;
                //make rt new top
                rt = t;
                //restore t to continue
                t = x;
            }
            //now, we will use _first_line.next to store our queue
            _first_line.next = rt;
            //next is initialized, now we conditinue to send rest of the list
            write_async_lines_next(true);
            //still locked here
        }
    }


    void write_async_lines_next(bool st) noexcept {
        //still locked
        //data in queue are empty
        auto ln = _first_line.next;
        if (ln == nullptr) {
            //when there is no lines to send - unlock and exit (note unlock can repeat the process)
            write_async_lines_unlock();

        } else if (!st) {
            //when error reported, discard all lines
            while (ln) {
                ln->cb(false);
                auto x = ln;
                ln = ln->next;
                delete x;
            }
            //reset
            _first_line.next = nullptr;
            //unlock
            write_async_lines_unlock();

        } else if (ln->data.empty()) {
            //when whole data has been sent
            auto cb = std::move(ln->cb);
            auto nx = ln->next;
            //delete line
            delete ln;
            //set next line
            _first_line.next = nx;
            //execute callback
            if (cb!=nullptr) cb(st);
            //send next line
            write_async_lines_next(st);

        } else {
            //send line data
            getContent()->write(ln->data.data(), ln->data.size(), [this](int r){
               //if something sent
               if (r) {
                   //remove sent content
                   auto ln = _first_line.next;
                   ln->data = ln->data.substr(r);
                   //repeat send
                   write_async_lines_next(true);
               } else {
                   //if error, just finish
                   write_async_lines_next(false);
               }
            });
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

    virtual void put_char(char c) override {
        _chr_buffer.push_back(c);
    }
    virtual void put_block(const std::string_view &block) override {
        _chr_buffer.insert(_chr_buffer.end(), block.begin(), block.end());
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
        }
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





}


#endif /* SRC_LIBS_USERVER_STREAM_INSTANCE_H_ */

