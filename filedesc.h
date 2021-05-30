/*
 * filedesc.h
 *
 *  Created on: 20. 4. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_FILEDESC_H_
#define SRC_USERVER_FILEDESC_H_
#include "isocket.h"


namespace userver {

class FileDesc: public ISocket {
public:
	FileDesc();
	FileDesc(int fd);
	virtual ~FileDesc() override;

	FileDesc(FileDesc &&other);
	FileDesc &operator=(FileDesc &&other);

	int read(void *buffer, std::size_t size) override;
	int write(const void *buffer, std::size_t size) override;
	void read(void *buffer, std::size_t size, CallbackT<void(int)> &&fn) override;
	void write(const void *buffer, std::size_t size, CallbackT<void(int)> &&fn) override;

	void closeOutput() override;
	void closeInput() override;

	void setRdTimeout(int tm) override;
	void setWrTimeout(int tm) override;
	void setIOTimeout(int tm) override;

	int getRdTimeout() const override;
	int getWrTimeout() const override;

	bool timeouted() const override;

	virtual bool waitConnect(int tm) override;
	virtual void waitConnect(int tm, CallbackT<void(bool)> &&cb) override;;

	bool waitForRead(int tm) const;
	bool waitForWrite(int tm) const;

	int getHandle() const {return fd;}

	virtual void clearTimeout() override;


	void close();
protected:
	int fd=-1;
	int readtm=-1;
	int writetm=-1;
	bool tm = false;
};


}


#endif /* SRC_USERVER_FILEDESC_H_ */
