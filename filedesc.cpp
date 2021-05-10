/*
 * filedesc.cpp
 *
 *  Created on: 20. 4. 2021
 *      Author: ondra
 */

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <userver/async_provider.h>
#include <userver/async_resource.h>
#include "filedesc.h"
#include <cerrno>
#include <system_error>
#include <sstream>

namespace userver  {

FileDesc::FileDesc() {
}

FileDesc::FileDesc(int fd):fd(fd) {
	int flags = fcntl(fd, F_GETFL, 0);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK)) throw std::system_error(errno, std::generic_category(), "Can't set non-blocking mode");
}

FileDesc::FileDesc(FileDesc &&other):fd(other.fd) {
	other.fd = -1;
}

bool FileDesc::waitConnect(int ) {
	return true;
}

void FileDesc::waitConnect(int , CallbackT<void(bool)> &&cb) {
	cb(true);
}

void FileDesc::close() {
	if (fd != -1) ::close(fd);
}

FileDesc::~FileDesc() {
	close();
}


FileDesc& FileDesc::operator =(FileDesc &&other) {
	close();
	fd = other.fd;
	other.fd = 0;
	return *this;
}

static void error(int errnr, const char *desc) {
	std::ostringstream bld;
	bld << "I/O error: " << desc;
	throw std::system_error(errnr, std::generic_category(), bld.str());
}


int FileDesc::read(void *buffer, std::size_t size) {
	int r = ::read(fd, buffer,size);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			if (!waitForRead(readtm)) {
				tm = true;
				return 0;
			}
			r = ::read(fd, buffer,size);
			if (r < 0) error(errno,"filedesc read()");
		} else {
			error(err,"filedesc read()");
		}
	}
	return r;
}

int FileDesc::write(const void *buffer, std::size_t size) {
	int r = ::write(fd, buffer, size);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			if (!waitForWrite(writetm)) {
				tm = true;
				return 0;
			}
			r = ::write(fd, buffer,size);
			if (r < 0) error(errno,"filedest write()");
		} else {
			error(err,"filedesc write()");
		}
	}
	return r;
}

void FileDesc::closeOutput() {
	close();
}


void FileDesc::closeInput() {
	close();
}

void FileDesc::setRdTimeout(int tm) {
	readtm = tm;
}

void FileDesc::setWrTimeout(int tm) {
	writetm = tm;
}

void FileDesc::setIOTimeout(int tm) {
	readtm = writetm = tm;
}

int FileDesc::getRdTimeout() const {
	return readtm;
}

int FileDesc::getWrTimeout() const {
	return writetm;
}



bool FileDesc::timeouted() const {
	return tm;
}


void FileDesc::read(void *buffer, std::size_t size, CallbackT<void(int)> &&fn) {
	int r = ::read(fd, buffer,size);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			getCurrentAsyncProvider().runAsync(AsyncResource(AsyncResource::read, fd), [this, buffer, size, fn = std::move(fn)](bool succ){
				if (!succ) {
					this->tm = true;
					fn(0);
				} else {
					fn(read(buffer, size));
				}
			}, readtm<0?std::chrono::system_clock::time_point::max()
					 :std::chrono::system_clock::now()+std::chrono::milliseconds(readtm));
		} else {
			error(err,"filedesc read()");
		}
	} else {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn),r]{
			fn(r);
		});
	}
}

void FileDesc::write(const void *buffer, std::size_t size, CallbackT<void(int)> &&fn) {
	int r = ::write(fd, buffer, size);
	if (r < 0) {
		int err = errno;
		if (err == EWOULDBLOCK) {
			getCurrentAsyncProvider().runAsync(AsyncResource(AsyncResource::write, fd), [this, buffer, size, fn = std::move(fn)](bool succ){
				if (!succ) {
					this->tm = true;
					fn(0);
				} else {
					fn(write(buffer, size));
				}
			}, std::chrono::system_clock::now()+std::chrono::milliseconds(writetm));
		} else {
			try {
				error(err,"filedesc write()");
			} catch (...) {
				fn(0);
			}
		}
	} else {
		getCurrentAsyncProvider().runAsync([fn = std::move(fn),r]{
			fn(r);
		});
	}
}


bool FileDesc::waitForRead(int tm) const {
	pollfd pfd = {fd, POLLIN, 0};
	int r = poll(&pfd, 1, tm);
	if (r < 0) {
		int err = errno;
		if (err == EINTR) return waitForRead(tm);
	}
	return r>0;
}


bool FileDesc::waitForWrite(int tm) const {
	pollfd pfd = {fd, POLLOUT, 0};
	int r = poll(&pfd, 1, tm);
	if (r < 0) {
		int err = errno;
		if (err == EINTR) return waitForRead(tm);
	}
	return r>0;
}

void FileDesc::clearTimeout() {
	tm = false;
}


}



