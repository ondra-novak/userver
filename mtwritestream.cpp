/*
 * mtwritestream.cpp
 *
 *  Created on: 2. 12. 2021
 *      Author: ondra
 */

#include "mtwritestream.h"

namespace userver {

MTWriteStream::~MTWriteStream() {
	Line *x = lines.exchange(nullptr);
	while (x) {
		Line *n = x->next;
		delete x;
		x = n;
	}
}

MTWriteStream::Line MTWriteStream::unlocked("");

bool MTWriteStream::send(std::shared_ptr<MTWriteStream> me, const std::string_view &ln) {
	//if stream is closed, no more can be send
	if (me->closed) return false;
	//ulk contains unlocked state - unique pointer value which signals, that stream is idle
	Line *ulk = &unlocked;
	//try to lock stream by passing nullptr instead &unlocked
	if (!me->lines.compare_exchange_strong(ulk, nullptr)) {
		//we failed to lock stream, so this mean, that someone is already operates there

		//allocate line
		Line *lnptr = new(ln) Line(ln);
		//read pointer top of the line stack
		Line *nx = me->lines;
		//this is cycle to perform lockfree insert to stack
		do {
			//set next pointer
			lnptr->next = nx;
			//try to replace top of the stack
		} while (me->lines.compare_exchange_weak(nx, lnptr));
		//now we success - check next pointer
		//because if it was &unlocked, whe incidently locked stream for us
		if (lnptr->next == &unlocked) {
			//so set the next pointer to null
			lnptr->next = nullptr;
			//and perform async send
			sendAsync(me, std::string_view());
		}
		//overall this is success
		return true;
	} else {

		//stream is idle and locked for us, send async
		sendAsync(me, ln);
		//success
		return true;
	}
}

void MTWriteStream::close() {
	closed = true;
}

void MTWriteStream::sendAsync(std::shared_ptr<MTWriteStream> me, const std::string_view &ln) {

		//pick current stack atomically
		Line *x = me->lines.exchange(nullptr);
		//if there is no stack, and line is empty
		if (x == nullptr && ln.empty()) {
			//try to unlock the stream - in case of succes, we can exit now
			if (me->lines.compare_exchange_strong(x, &unlocked)) return;
			//otherwise, we still owning the stream, we can repeat sendAsync
			else sendAsync(me, ln);
		}

		//the stack is in reversed order - we need to reverse it
		//this don't need to be done atomicaly, because this is under lock
		Line *y = nullptr;
		while (x) {
			Line *n = x->next;
			x->next = y;
			y = x;
			x = n;
		}

		//no process lines and write to buffer and delete lines from stack
		while (y) {
			Line *n = y->next;
			me->writeNB(y->str);
			delete y;
			y = n;
		}

		//finally write extra line carried by argument
		me->writeNB(ln);
		//flush stream asynchronously
		me->flushAsync([me](bool ok){
			//locked thread continues here
			if (!ok) {
				//if flush fails, mark stream closed
				me->closed = true;
			} else {
				//repeat sendAsync
				me->sendAsync(me, std::string());
			}
		});

}


void MTWriteStream::monitor(std::shared_ptr<MTWriteStream> me, CallbackT<void(const std::string_view &)> &&cb) {
	me->readAsync([me,cb = std::move(cb)](const std::string_view &data) mutable {
		if (!data.empty()) {
			cb(data);
			monitor(me, std::move(cb));
		} else if (me->timeouted()) {
			me->clearTimeout();
			monitor(me, std::move(cb));
		} else {
			cb(data);
			me->closed = true;
		}
	});
}


}

void* userver::MTWriteStream::Line::operator new(std::size_t sz, const std::string_view &text) {
	std::size_t objsz = std::max<std::size_t>(sizeof(Line), sz);
	std::size_t wholeSize = objsz + text.size()+1;
	void *res = ::operator new(wholeSize);
	std::copy(text.begin(), text.end(), reinterpret_cast<char *>(res)+objsz);
	return res;
}

void userver::MTWriteStream::Line::operator delete(void *ptr, const std::string_view &) {
	::operator delete(ptr);
}

void userver::MTWriteStream::Line::operator delete(void *ptr, std::size_t) {
	::operator delete(ptr);
}
