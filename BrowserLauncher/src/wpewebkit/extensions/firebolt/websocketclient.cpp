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
 
 #include "websocketclient.h"
 #include <glib.h>

static inline SoupFunctions& soup() {
    return SoupFunctions::get();
}

WebSocketClient::WebSocketClient(const char *url)
    : m_url(g_strdup(url))
{
}

WebSocketClient::~WebSocketClient()
{
    Disconnect();
    g_free(m_url);
}

bool WebSocketClient::Connect(std::function<void(const bool)>&& onConnect,
                              std::function<void(const std::string&)>&& onMessage)
{
    m_session = soup().session_new();
    SoupMessage *msg = soup().message_new("GET", m_url);
    if (!msg) {
        g_printerr("Failed to create SoupMessage\n");
        return false;
    }
    m_onConnect = std::move(onConnect);
    m_onMessage = std::move(onMessage);

    auto connectCallback = [](GObject *source_object, GAsyncResult *res, gpointer user_data) {
        WebSocketClient *self = static_cast<WebSocketClient*>(user_data);
        GError *error = nullptr;
        self->m_conn = soup().session_websocket_connect_finish(self->m_session, res, &error);
        if (error) {
            g_printerr("WebSocket connection failed: %s\n", error->message);
            g_error_free(error);
            return;
        }
        self->onConnection(self->m_conn);
    };

    soup().session_websocket_connect_async(m_session, msg, nullptr, nullptr, G_PRIORITY_DEFAULT, nullptr, connectCallback, this);

    g_object_unref(msg);
    return true;
}

void WebSocketClient::onConnection(SoupWebsocketConnection *ws)
{
    if (!ws)
    {
        g_warning("couldn't establish jsonrpc ws connection.");
        m_onConnect(false);
        return;
    }
    m_conn = reinterpret_cast<SoupWebsocketConnection*>(g_object_ref(ws));
    g_signal_connect(ws, "message", G_CALLBACK(+[](SoupWebsocketConnection *ws, gint type, GBytes *message, gpointer userData) {
        auto *self = reinterpret_cast<WebSocketClient*>(userData);
        self->onMessage(type, message);
    }), this);
    g_signal_connect(ws, "error", G_CALLBACK(+[](SoupWebsocketConnection *ws, GError *error, gpointer userData) {
        auto *self = reinterpret_cast<WebSocketClient*>(userData);
        self->onError(error);
    }), this);
    g_signal_connect(ws, "closed", G_CALLBACK(+[](SoupWebsocketConnection *ws, gpointer userData) {
        auto *self = reinterpret_cast<WebSocketClient*>(userData);
        self->onClosed();
    }), this);
    m_onConnect(true);
}

void WebSocketClient::onMessage(gint type, GBytes *message)
{
    if (type != SOUP_WEBSOCKET_DATA_TEXT) {
        g_printerr("Received non-text WebSocket message, ignoring\n");
        return;
    }
    gsize sz;
    const void *ptr = g_bytes_get_data(message, &sz);
    std::string message_str = std::string(reinterpret_cast<const char*>(ptr), sz);
    g_message("recv: %s", message_str.c_str());
    m_onMessage(message_str);
}

void WebSocketClient::onError(GError *error)
{
    g_warning("error detected - %s", error->message);
    m_onConnect(false);
}

void WebSocketClient::onClosed()
{
    g_info("ws connection closed");
    m_onConnect(false);
}

void WebSocketClient::SendMessage(const std::string& message)
{
    if (!m_conn) {
        g_printerr("Cannot send message, WebSocket connection is not established\n");
        return;
    }
    soup().websocket_connection_send_text(m_conn, message.c_str());
}

void WebSocketClient::Disconnect()
{
    if (m_conn) {
        soup_websocket_connection_close(m_conn, 1000, "Normal Closure");
        g_object_unref(m_conn);
        m_conn = nullptr;
    }
    if (m_session) {
        g_object_unref(m_session);
        m_session = nullptr;
    }
    m_onConnect(false);
}