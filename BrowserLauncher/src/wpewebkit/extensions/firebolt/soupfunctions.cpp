/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 #include "soupfunctions.h"

#include <dlfcn.h>
#include <functional>
#include <type_traits>

SoupFunctions &SoupFunctions::get()
{
    static SoupFunctions instance;
    return instance;
}

SoupFunctions::SoupFunctions()
{
#define RESOLVE_FN(name)                                                \
    do {                                                                \
        this->name = reinterpret_cast<soup_##name##_t>(dlsym(RTLD_DEFAULT, "soup_"#name)); \
        g_assert(this->name != 0x0);                                    \
    } while(0)

    RESOLVE_FN(session_new);
    RESOLVE_FN(message_new);
    RESOLVE_FN(check_version);
    RESOLVE_FN(session_websocket_connect_finish);
    RESOLVE_FN(websocket_connection_send_text);
#undef RESOLVE_FN

    if (check_version && check_version(2, 99, 2)) {
        session_websocket_connect_async_variant = reinterpret_cast<soup_session_websocket_connect_async_v3_t>(dlsym(RTLD_DEFAULT, "soup_session_websocket_connect_async"));
    } else {
        session_websocket_connect_async_variant = reinterpret_cast<soup_session_websocket_connect_async_v2_t>(dlsym(RTLD_DEFAULT, "soup_session_websocket_connect_async"));
    }
}

void SoupFunctions::session_websocket_connect_async(SoupSession *session,
                                                         SoupMessage *msg,
                                                         const char *origin,
                                                         char **protocols,
                                                         int io_priority,
                                                         GCancellable *cancellable,
                                                         GAsyncReadyCallback callback,
                                                         gpointer user_data)
{
    std::visit([&](auto fn){
        using Fn = std::decay_t<decltype(fn)>;
        if constexpr (std::is_same_v<Fn, soup_session_websocket_connect_async_v3_t>) {
            fn(session, msg, origin, protocols, io_priority, cancellable, callback, user_data);
        } else {
            fn(session, msg, origin, protocols, cancellable, callback, user_data);
        }
    }, session_websocket_connect_async_variant);
}