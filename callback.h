/*
 * callback.h
 *
 *  Created on: 6. 11. 2021
 *      Author: ondra
 */

#ifndef SRC_USERVER_CALLBACK_H_
#define SRC_USERVER_CALLBACK_H_
#include <cassert>
#include <typeinfo>
#include <atomic>
#include "shared/callback.h"

namespace userver {

template<typename T>
using Callback = ondra_shared::Callback<T>;
template<typename T>
using CallbackT = ondra_shared::Callback<T>;


}
#endif /* SRC_USERVER_CALLBACK_H_ */

