#include "wsclient.h"

#include <glib.h>
#include <libsoup/soup.h>

WsClient::WsClient() : _session(soup_session_new()) {}

WsClient::~WsClient() {
  disconnect();
  g_clear_object(&_session);
}

void WsClient::connect(const std::string& url) {
  if (_state != State::Disconnected) {
    g_debug("WsClient: connect() ignored — already connecting or connected");
    return;
  }
  _url = url;
  connectInternal();
}

// Cancels any in-flight connect, closes any active WebSocket connection.
// Does NOT invoke onClosed.
void WsClient::disconnect() {
  if (_cancel) {
    g_cancellable_cancel(_cancel);
    g_clear_object(&_cancel);
  }

  if (_ws) {
    g_signal_handlers_disconnect_by_data(_ws, this);
    soup_websocket_connection_close(_ws, SOUP_WEBSOCKET_CLOSE_NORMAL, nullptr);
    g_object_unref(_ws);
    _ws = nullptr;
  }

  _state = State::Disconnected;
}

void WsClient::send(const char* message) {
  if (_state == State::Connected)
    soup_websocket_connection_send_text(_ws, message);
  else
    g_warning("Cannot send ws message");
}

// Initiates the actual async WebSocket connect to _url.
void WsClient::connectInternal() {
  g_debug("WsClient: attempting to open websocket: %s", _url.c_str());

  _state = State::Connecting;

  // Cancel any previous in-flight connect
  if (_cancel) {
    g_cancellable_cancel(_cancel);
    g_object_unref(_cancel);
  }
  _cancel = g_cancellable_new();

  SoupMessage* msg = soup_message_new(SOUP_METHOD_GET, _url.c_str());
  if (!msg) {
    g_warning("WsClient: failed to create SoupMessage for URL: %s",
              _url.c_str());
    g_clear_object(&_cancel);
    _state = State::Disconnected;
    return;
  }

  soup_session_websocket_connect_async(_session, msg,
                                       nullptr,  // origin
                                       nullptr,  // protocols
                                       G_PRIORITY_DEFAULT, _cancel,
                                       onWsConnectFinished, this);

  g_object_unref(msg);
}

// Static trampolines
void WsClient::onWsConnectFinished(GObject* src,
                                   GAsyncResult* res,
                                   gpointer user_data) {
  static_cast<WsClient*>(user_data)->handleConnectFinished(SOUP_SESSION(src), res);
}

void WsClient::onWsMessage(SoupWebsocketConnection* /*ws*/,
                           gint /*type*/,
                           GBytes* message,
                           gpointer user_data) {
  static_cast<WsClient*>(user_data)->handleMessage(message);
}

void WsClient::onWsClosed(SoupWebsocketConnection* /*ws*/, gpointer user_data) {
  static_cast<WsClient*>(user_data)->handleClosed();
}

// Callback from soup_session_websocket_connect_async.
void WsClient::handleConnectFinished(SoupSession* session, GAsyncResult* res) {
  GError* err = nullptr;
  SoupWebsocketConnection* ws =
      soup_session_websocket_connect_finish(session, res, &err);
  if (err) {
    if (!g_error_matches(err, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
      g_warning("WsClient: websocket connect failed: %s", err->message);
      _state = State::Disconnected;
      if (onError)
          onError(err->message ? err->message : "unknown");
    } else {
       // Cancelled by disconnect()
    }
    g_error_free(err);
    return;
  }

  // Release the cancellable — connect is done
  g_clear_object(&_cancel);

  _ws = ws;
  _state = State::Connected;

  g_debug("WsClient: connected to %s", _url.c_str());

  g_signal_connect(_ws, "message", G_CALLBACK(onWsMessage), this);
  g_signal_connect(_ws, "closed", G_CALLBACK(onWsClosed), this);

  if (onOpen)
      onOpen();
}

// Callback when a text message is received on the WebSocket.
void WsClient::handleMessage(GBytes* message) {
  if (!g_log_writer_default_would_drop(G_LOG_LEVEL_DEBUG, G_LOG_DOMAIN)) {
      gsize size = 0;
      const gchar* data =
          static_cast<const gchar*>(g_bytes_get_data(message, &size));
      g_debug("WsClient: websocket text message received: %.*s", size, data);
  }
  if (onMessage)
    onMessage(message);
}

// Callback when the WebSocket is closed unexpectedly.
void WsClient::handleClosed() {
  g_info("WsClient: websocket disconnected");

  if (_ws) {
    g_signal_handlers_disconnect_by_data(_ws, this);
    g_clear_object(&_ws);
  }

  _state = State::Disconnected;

  // Notify owner
  if (onClosed)
    onClosed();
}
