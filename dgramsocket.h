/*prim
 * udpsocket.h
 *
 *  Created on: 19. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_DGRAMSOCKET_H_
#define SRC_USERVER_DGRAMSOCKET_H_

#include <string_view>
#include <type_traits>
#include <vector>
#include <chrono>
#include "async_provider.h"
#include "platform_def.h"

#include "helpers.h"

namespace userver {

class NetAddr;
class SocketResource;

///Socket to handle sending datagrams
/**
 *
 */
class DGramSocket {
public:

	///Create object using socket id
	DGramSocket(SocketHandle i);
	///Create object bound to specified address
	DGramSocket(const NetAddr &addr);

	///Move instance to different location
	DGramSocket(DGramSocket &&other);

	///Assign to other instance (move)
	DGramSocket &operator=(DGramSocket &&other);

	///Close socket
	~DGramSocket();

	///Receive a signle datagram
	/**
	 * @param timeout timeout to wait on data (default is infinity)
	 * @retval true a datagram has been received
	 * @retval false timeout ellapsed
	 *
	 * @note received data are stored in buffer and can be retrieved by calling getData();
	 */
	bool recv(int timeout = -1);

	///Retrieves data received by last recv()
	/**
	 * @return received data
	 *
	 * @note if the empty buffer is returned, then a packet has been received, but it has
	 * been corrupted and discarded. However you can still retrieve a peer address
	 */
	std::string_view getData() const;

	///Retrieves peer address
	NetAddr getPeerAddr() const;

	///Reads asynchronously
	/**
	 * This allows to read data without blocking current thread
	 * @param cb callback function. The callback function receives data in buffer. It can retrieve
	 *  empty buffer in case of timeout
	 * @param timeout timeout
	 */
	template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<std::string_view>()))>
	void readAsync(Fn &&cb, int timeout = -1);

	///Reads asynchronously
	/**
	 * This allows to read data without blocking current thread
	 * @param cb callback function. The callback function receives data in buffer. It can retrieve
	 *  empty buffer in case of timeout
	 * @param timeout timeout
	 */
	template<typename Fn, typename = decltype(std::declval<Fn>()(std::declval<std::string_view>()))>
	void readAsync(AsyncProvider aprovider, Fn &&cb, int timeout = -1);

	///Sends the data to the target
	/** There is no async version, because in most cases, the call doesn't block the thread
	 *
	 * @param data data to send
	 * @param target target
	 */
	void send(const std::string_view &data, const NetAddr &target);


protected:
	SocketHandle s;
	std::vector<char> inputBuffer;
	std::vector<char> addrBuffer;
	int rcvsize = 0;

	const SocketResource &getReadAsync() ;

};
template<typename Fn, typename>
void DGramSocket::readAsync(AsyncProvider aprovider, Fn &&cb, int timeout) {
	aprovider.runAsync(getReadAsync(), [this,aprovider,timeout,fn = std::move(cb)](bool succ){
		if (!succ) fn(std::string_view());
		else {
			recv(0);
			std::string_view data = getData();
			if (!data.empty()) fn(data);
			else readAsync(aprovider, std::move(fn), timeout);
		}
	},  timeout < 0?
			 std::chrono::system_clock::time_point::max():
			 std::chrono::system_clock::now() + std::chrono::milliseconds(timeout));
}


template<typename Fn, typename>
void DGramSocket::readAsync(Fn &&cb, int timeout) {
	readAsync(getCurrentAsyncProvider(), std::move(cb), timeout);
}

}



#endif /* SRC_USERVER_DGRAMSOCKET_H_ */
