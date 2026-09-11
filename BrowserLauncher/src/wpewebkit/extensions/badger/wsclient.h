#pragma once

#include <gio/gio.h>
#include <glib.h>

#include <functional>
#include <string>

typedef struct _SoupSession SoupSession;
typedef struct _SoupWebsocketConnection SoupWebsocketConnection;

class WsClient final {
  // Non-copyable, non-movable
  WsClient(const WsClient&) = delete;
  WsClient& operator=(const WsClient&) = delete;

public:
  WsClient();
  ~WsClient();

  // Initiates an async WebSocket connection to url.
  void connect(const std::string& url);

  // Cancels any in-flight connect, closes any active connection.
  // Does NOT invoke onClosed.
  void disconnect();

  // Send text message to the peer.
  void send(const char* message);

  // Called when WebSocket connected.
  std::function<void()> onOpen;

  // Called if WebSocket connection failed.
  std::function<void(const char*)> onError;

  // Called with raw text content of every incoming WebSocket text frame.
  std::function<void(GBytes*)> onMessage;

  // Called on every unexpected WebSocket close.
  // NOT called when disconnect() is used.
  std::function<void()> onClosed;

 private:
  enum class State { Disconnected, Connecting, Connected };

  void connectInternal();

  // Static trampolines
  static void onWsConnectFinished(GObject* src,
                                  GAsyncResult* res,
                                  gpointer user_data);
  static void onWsMessage(SoupWebsocketConnection* ws,
                          gint type,
                          GBytes* message,
                          gpointer user_data);
  static void onWsClosed(SoupWebsocketConnection* ws, gpointer user_data);

  // Member handlers
  void handleConnectFinished(SoupSession* session, GAsyncResult* res);
  void handleMessage(GBytes* message);
  void handleClosed();

 private:
  SoupSession* _session{nullptr};
  SoupWebsocketConnection* _ws{nullptr};
  GCancellable* _cancel{nullptr};
  State _state{State::Disconnected};
  std::string _url;
};
