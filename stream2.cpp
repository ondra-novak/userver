/*
 * stream2.cpp
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */



#include "stream2.h"
#include "stream_instance.h"

#include "socket.h"

template class userver::Stream2_t<std::unique_ptr>;
template class userver::Stream2_t<std::shared_ptr>;
template class userver::StreamInstance<userver::StreamSocketWrapper>;


namespace userver {

Stream2 createSocketStream(SocketHandle socket) {

    return std::make_unique<StreamInstance<Socket> >(socket);
}

Stream2 createSocketStream(std::unique_ptr<ISocket> &&socket) {

    return std::make_unique<StreamInstance<StreamSocketWrapper> >(std::move(socket));

}

}
