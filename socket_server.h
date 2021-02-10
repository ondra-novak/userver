/*
 * socket_server.h
 *
 *  Created on: 9. 1. 2021
 *      Author: ondra
 */

#ifndef SRC_MAIN_SOCKET_SERVER_H_
#define SRC_MAIN_SOCKET_SERVER_H_

#include <memory>
#include <optional>
#include "netaddr.h"
#include "socket.h"
#include "helpers.h"

namespace userver {

class SocketServer {
public:
	SocketServer(const NetAddrList &addrLst);
	~SocketServer();

	struct AcceptInfo {
		Socket sock;
		NetAddr peerAddr;
	};

	void stop();

	std::optional<Socket> waitAccept();
	std::optional<AcceptInfo> waitAcceptGetPeer();

	///Receives connection
	/** If argument has no value, an error happened (currently in catch) */
	using AsyncCallback =  CallbackT<void(std::optional<AcceptInfo> &)>;

	///Accept connection asynchronously
	/**
	 * @param callback callback function
	 * @retval true callback charged
	 * @retval false error - probably conflict on exit
	 */
	bool waitAcceptAsync(AsyncCallback &&callback);

protected:
	std::vector<SocketHandle> fds;
	bool exit = false;

	SocketHandle waitForSocket(sockaddr_storage &sin);

	class AsyncAcceptor;
	std::shared_ptr<AsyncAcceptor> asyncState;

};

}

#endif /* SRC_MAIN_SOCKET_SERVER_H_ */
