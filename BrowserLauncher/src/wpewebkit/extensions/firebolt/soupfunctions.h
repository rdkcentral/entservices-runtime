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
 #pragma once

#include <glib.h>
#include <gio/gio.h>
#include <variant>

typedef struct _SoupMessage SoupMessage;
typedef struct _SoupSession SoupSession;
typedef struct _SoupWebsocketConnection SoupWebsocketConnection;

typedef enum {
    SOUP_WEBSOCKET_DATA_TEXT = 0x01,
    SOUP_WEBSOCKET_DATA_BINARY = 0x02,
} SoupWebsocketDataType;

typedef SoupSession *(*soup_session_new_t)();

typedef SoupMessage *(*soup_message_new_t)(const char *method,
                                           const char *uri_string);

typedef gboolean (*soup_check_version_t)(guint major,
                                         guint minor,
                                         guint micro);

typedef SoupWebsocketConnection *(*soup_session_websocket_connect_finish_t)(SoupSession *session,
                                                                            GAsyncResult *result,
                                                                            GError **error);

typedef void (*soup_websocket_connection_send_text_t)(SoupWebsocketConnection *self, const char *text);

typedef void (*soup_session_websocket_connect_async_v3_t)(SoupSession *session,
                                                          SoupMessage *msg,
                                                          const char *origin,
                                                          char **protocols,
                                                          int io_priority,
                                                          GCancellable *cancellable,
                                                          GAsyncReadyCallback callback,
                                                          gpointer user_data);

typedef void (*soup_session_websocket_connect_async_v2_t)(SoupSession *session,
                                                          SoupMessage *msg,
                                                          const char *origin,
                                                          char **protocols,
                                                          GCancellable *cancellable,
                                                          GAsyncReadyCallback callback,
                                                          gpointer user_data);

struct SoupFunctions
{

    static SoupFunctions &get();

    soup_session_new_t session_new{nullptr};
    soup_message_new_t message_new{nullptr};
    soup_check_version_t check_version{nullptr};
    soup_session_websocket_connect_finish_t session_websocket_connect_finish{nullptr};
    soup_websocket_connection_send_text_t websocket_connection_send_text{nullptr};

    // Use the v3 signature as the main one
    void session_websocket_connect_async(SoupSession *session,
                                              SoupMessage *msg,
                                              const char *origin,
                                              char **protocols,
                                              int io_priority,
                                              GCancellable *cancellable,
                                              GAsyncReadyCallback callback,
                                              gpointer user_data);

private:
    std::variant<soup_session_websocket_connect_async_v2_t,
                 soup_session_websocket_connect_async_v3_t> session_websocket_connect_async_variant{};

    SoupFunctions();

    ~SoupFunctions() = default;
    SoupFunctions(const SoupFunctions &) = delete;
    SoupFunctions &operator=(const SoupFunctions &) = delete;
};