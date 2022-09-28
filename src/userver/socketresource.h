/*
 * fdresource.h
 *
 *  Created on: 12. 6. 2022
 *      Author: ondra
 */

#ifndef SRC_USERVER_SOCKETRESOURCE_H_
#define SRC_USERVER_SOCKETRESOURCE_H_
#include "async_provider.h"

#include "platform_def.h"

namespace userver {

    class SocketResource: public IAsyncResource {
    public:
        enum Op {
            read, write
        };
        Op op;
        SocketHandle socket;

        SocketResource (Op op, SocketHandle socket):op(op),socket(socket) {}
    };

}



#endif /* SRC_USERVER_SOCKETRESOURCE_H_ */
