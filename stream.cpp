/*
 * stream2.cpp
 *
 *  Created on: 9. 7. 2022
 *      Author: ondra
 */



#include <userver/stream.h>
#include "stream_instance.h"

#include "socket.h"



namespace userver {

Stream createSocketStream(Socket &&s) {

    return std::make_unique<StreamInstance<Socket> >(std::move(s));
}

Stream createSocketStream(std::unique_ptr<ISocket> &&socket) {

    return std::make_unique<StreamInstance<StreamSocketWrapper> >(std::move(socket));

}


Stream createStreamReference(Stream &stream) {
    return std::make_unique<StreamReferenceWrapper>(*stream);
}
}
