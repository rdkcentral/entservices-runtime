/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "browserlauncher_test.h"

#include <gio/gio.h>
#include <libsoup/soup.h>

#if defined(HAVE_WESTEROS_COMPOSITOR)
#include <westeros-compositor.h>
#include <essos.h>
#endif

namespace {

// Checks if given json `message` object looks like jsonrpc message
static bool isValidJSONRPC(const json& message)
{
    if (message.is_discarded())
    {
        EXPECT_FALSE(message.is_discarded());
        return false;
    }
    if (!message.contains("id"))
    {
        EXPECT_TRUE(message.contains("id"));
        return false;
    }
    if (!message.contains("method") && !message.contains("result") && !message.contains("error"))
    {
        EXPECT_TRUE(message.contains("method") || message.contains("result") || message.contains("error"));
        return false;
    }
    return true;  // !HasFatalFailure()
}

std::string toString(LifecycleState state)
{
    switch(state)
    {
        case LifecycleState::INITIALIZING:
            return "initializing";
        case LifecycleState::ACTIVE:
            return "active";
        case LifecycleState::PAUSED:
            return "paused";
        case LifecycleState::SUSPENDED:
            return "suspended";
        case LifecycleState::HIBERNATED:
            return "hibernated";
        case LifecycleState::TERMINATING:
            return "terminating";
    }
    return "unknown";
}

}  // namespace

std::ostream& operator<<(std::ostream& out, const LifecycleState& state) {
    return out << toString(state);
}

void BrowserLauncherTest::sendMessage(SoupWebsocketConnection *connection, const json& message)
{
    EXPECT_NE(connection, nullptr);
    EXPECT_EQ(soup_websocket_connection_get_state (connection), SOUP_WEBSOCKET_STATE_OPEN);
    std::string message_str = to_string(message);
    g_message("%s_socket: send: %s", connection == _firebolt_connection ? "fb" : "test", message_str.c_str());
    auto* message_bytes = g_bytes_new(message_str.c_str(), message_str.size());
    soup_websocket_connection_send_message (connection, SOUP_WEBSOCKET_DATA_TEXT, message_bytes);
    g_clear_pointer (&message_bytes, g_bytes_unref);
}

void BrowserLauncherTest::createMainLoop()
{
    _context = g_main_context_new ();
    g_main_context_push_thread_default(_context);
    _loop = g_main_loop_new (_context, FALSE);
}

void BrowserLauncherTest::stopMainLoopAndServer()
{
    g_main_context_pop_thread_default(_context);
    g_clear_pointer(&_context, g_main_context_unref);
    g_clear_pointer(&_loop, g_main_loop_unref);
    g_clear_pointer(&_server, g_object_unref);
}

void BrowserLauncherTest::createServer(unsigned port)
{
    _server_port = port;
    _server = soup_server_new ("server-header", "BrowserLauncherTest ", nullptr);

    soup_server_add_websocket_handler (_server, "/fb_socket", nullptr, nullptr, BrowserLauncherTest::websocketHandler, this, nullptr);
    soup_server_add_websocket_handler (_server, "/test_socket", nullptr, nullptr, BrowserLauncherTest::websocketHandler, this, nullptr);

    soup_server_add_handler(_server, "/tests", [](SoupServer*, SoupServerMessage* message, const char* path, GHashTable*, gpointer user_data) {
        auto *test = reinterpret_cast<BrowserLauncherTest*>(user_data);
        if (test->_first_request_ts == -1)
            test->_first_request_ts = g_get_monotonic_time();

        GError *error = nullptr;
        gchar_ptr resourcePath { g_build_filename("/org/rdk", path, nullptr) };
        GBytes *bytes = g_resources_lookup_data(resourcePath.get(), G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
        if (error)
        {
            g_warning("failed to load error page from resources, %s", error->message);
            soup_server_message_set_status(message, SOUP_STATUS_INTERNAL_SERVER_ERROR, error->message);
            g_error_free(error);
            error = nullptr;
        }
        else
        {
            gsize dataSize;
            const guchar *dataPtr = reinterpret_cast<const guchar *>(g_bytes_get_data(bytes, &dataSize));
            gchar_ptr fileName { g_path_get_basename(resourcePath.get()) };
            gchar_ptr contentType { g_content_type_guess(fileName.get(), dataPtr, dataSize, nullptr) };
            soup_message_headers_append(soup_server_message_get_response_headers(message), "Content-Type", "text/html");
            soup_message_body_append(soup_server_message_get_response_body(message), SOUP_MEMORY_COPY, dataPtr, dataSize);
            soup_server_message_set_status(message, SOUP_STATUS_OK, nullptr);
            g_bytes_unref(bytes);
        }
    }, this, nullptr);

    if (!soup_server_listen_all (_server, port, static_cast<SoupServerListenOptions>(0), nullptr))
    {
        g_error ("Failed to listen on port %d", port);
    }
}

void BrowserLauncherTest::launchBrowser(const std::string& url, std::vector<std::string> args)
{
    EXPECT_EQ(_launcher, nullptr);
    EXPECT_EQ(_runtime_process, nullptr);

    GError *error = nullptr;
    _launcher = g_subprocess_launcher_new(
        static_cast<GSubprocessFlags>(G_SUBPROCESS_FLAGS_INHERIT_FDS |
                                      G_SUBPROCESS_FLAGS_SEARCH_PATH_FROM_ENVP));

    if (_server_port > 0)
    {
        g_subprocess_launcher_setenv(
            _launcher, "FIREBOLT_ENDPOINT",
            gchar_ptr(g_strdup_printf("ws://127.0.0.1:%u/fb_socket", _server_port)).get(),
            true);
    }

#if defined(HAVE_WESTEROS_COMPOSITOR)
    if (_compositor)
    {
        const char *display_name = WstCompositorGetDisplayName(_compositor);
        g_subprocess_launcher_setenv(
            _launcher, "WAYLAND_DISPLAY", display_name, true);
        // g_subprocess_launcher_setenv(
        //     _launcher, "WAYLAND_DEBUG", "client", true);
    }
#endif

    char **new_argv;
    args.emplace_back("--url");
    args.emplace_back(url);
    new_argv = g_newa(char *, args.size() + 2);
    if (!new_argv)
    {
        EXPECT_TRUE(new_argv != nullptr) << "Couldn't allocate new_argv";
    }
    else
    {
        unsigned i = 0;
        new_argv[i++] = const_cast<char *>("./BrowserLauncher");
        for (const auto &arg : args)
            new_argv[i++] = const_cast<char *>(arg.data());
        new_argv[i++] = nullptr;
        g_message("Launching '%s' with %d arg(s).", new_argv[0], args.size());
        for (i = 1; new_argv[i] != nullptr; ++i)
            g_message(" argv[%d] = %s", i, new_argv[i]);
    }

    _runtime_process = g_subprocess_launcher_spawnv(_launcher, new_argv, &error);
    EXPECT_EQ(error, nullptr);
    EXPECT_NE(_runtime_process, nullptr);
}

void BrowserLauncherTest::stopBrowser()
{
    if (!_launcher)
        return;

    EXPECT_NE(_runtime_process, nullptr);

    auto *cancellable = g_cancellable_new();

    g_subprocess_send_signal(_runtime_process, SIGTERM);

    g_subprocess_wait_async(
        _runtime_process, cancellable,
        [](GObject *sub_process, GAsyncResult *result, gpointer user_data) {
            GError *error = nullptr;
            g_subprocess_wait_finish(G_SUBPROCESS(sub_process), result, &error);
            if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
                return;
            g_spawn_check_wait_status(
                g_subprocess_get_status(G_SUBPROCESS(sub_process)), &error);
            g_message("Runtime process finished. %s", error ? error->message : "");
            g_main_loop_quit(reinterpret_cast<GMainLoop *>(user_data));
        },
        _loop);

    GSource *timeoutSource = g_timeout_source_new_seconds(5);
    g_source_set_callback(timeoutSource, G_SOURCE_FUNC(+[](BrowserLauncherTest* test) {
        g_subprocess_force_exit(test->_runtime_process);
        return G_SOURCE_REMOVE;
    }), this, nullptr);
    g_source_attach(timeoutSource, _context);

    g_main_loop_run (_loop);

    g_source_destroy(timeoutSource);
    g_source_unref(timeoutSource);
    g_cancellable_cancel(cancellable);
    g_clear_pointer(&cancellable, g_object_unref);
    g_clear_pointer(&_runtime_process, g_object_unref);
    g_clear_pointer(&_launcher, g_object_unref);

    EXPECT_EQ(_runtime_process, nullptr);
}

void BrowserLauncherTest::breakIfNeeded()
{
    if (_should_break_event_loop)
    {
        g_main_loop_quit(_loop);
    }
}

void BrowserLauncherTest::onMessage(SoupWebsocketConnection *connection, const std::string& message_str)
{
    auto message_json = json::parse(message_str, nullptr, false, true);
    if (!isValidJSONRPC(message_json))
    {
        g_warning("failed to parse: %s", message_str.c_str());
        return;
    }
    else if (_firebolt_connection == connection)
    {
        g_message("fb_socket: recv: %s", message_str.c_str());
        onFireboltMessage(message_json);
    }
    else if (_test_connection == connection)
    {
        g_message("test_socket: recv: %s", message_str.c_str());
        onTestMessage(message_json);
    }
    else
    {
        EXPECT_TRUE(false) << "Unexpected message";
        return;
    }
}

void BrowserLauncherTest::changeLifecycleStateState(LifecycleState oldState, LifecycleState newState, bool focused)
{
    std::string oldStateStr = toString(oldState);
    std::string newStateStr = toString(newState);

    g_message("changeLifecycleStateState: oldState=%s, newState=%s, focused=%c", oldStateStr.c_str(), newStateStr.c_str(), focused ? 'y' : 'n');

    if (_current_lc_state != newState)
    {
        _current_lc_state = newState;

        const json state_change = {
            {"oldState", oldStateStr},
            {"newState", newStateStr}
        };

        json message = {
            {"jsonrpc", "2.0"},
            {"method", "Lifecycle2.onStateChanged"},
            {"params", { state_change }}
        };

        sendFireboltMessage(message);
    }

    if (_focused != focused)
    {
        _focused = focused;

        json message = {
            {"jsonrpc", "2.0"},
            {"method", "Presentation.onFocusedChanged"},
            {"params", focused}
        };

        sendFireboltMessage(message);
    }
}

void BrowserLauncherTest::onFireboltMessage(const json& message)
{
    EXPECT_FALSE(message.contains("error"));

    if (HasFatalFailure())
        return;

    const unsigned int id = message["id"].get<unsigned int>();
    const std::string method = message["method"].get<std::string>();

    if (method == "Lifecycle2.onStateChanged")
    {
        auto params = message["params"];
        EXPECT_FALSE(params.is_discarded());
        EXPECT_TRUE(params.contains("listen"));
        bool listen = params["listen"].get<bool>();
        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {{"listening",listen},{"event",method}}}
        };
        sendFireboltMessage(result);

        if (listen)
        {
            _state_change_listeners.push_back(id);
        }
        else
        {
            _state_change_listeners.erase(
                std::remove(_state_change_listeners.begin(), _state_change_listeners.end(), id),
                _state_change_listeners.end());
        }
    }
    else if (method == "Presentation.onFocusedChanged")
    {
        auto params = message["params"];
        EXPECT_FALSE(params.is_discarded());
        EXPECT_TRUE(params.contains("listen"));
        bool listen = params["listen"].get<bool>();
        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", {{"listening",listen},{"event",method}}}
        };
        sendFireboltMessage(result);
    }
    else if (method == "Lifecycle2.state")
    {
        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", _current_lc_state}
        };
        sendFireboltMessage(result);
    }
    else if (method == "Lifecycle2.close")
    {
        auto params = message["params"];
        EXPECT_FALSE(params.is_discarded());
        EXPECT_TRUE(params.contains("type"));

        _close_type = params["type"].get<std::string>();

        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", nullptr}
        };
        sendFireboltMessage(result);
    }
    else if (method == "Presentation.focus")
    {
        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", _focused}
        };
        sendFireboltMessage(result);
    }
    else
    {
        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result", nullptr}
        };
        sendFireboltMessage(result);
    }

}

void BrowserLauncherTest::onConnectionClosed(SoupWebsocketConnection *connection)
{
    if (_test_connection == connection)
    {
        _test_connection = nullptr;
    }
    else if (_firebolt_connection == connection)
    {
        _firebolt_connection = nullptr;
    }
}

// static
void BrowserLauncherTest::websocketHandler (
    SoupServer              *server,
    SoupServerMessage       *msg,
    const char              *path,
    SoupWebsocketConnection *connection,
    gpointer                 user_data)
{
  g_print ("New WebSocket connection(%p) request on path: %s, query: %s\n", connection, path, g_uri_get_query(soup_server_message_get_uri(msg)));

  auto *test = reinterpret_cast<BrowserLauncherTest*>(user_data);
  if (g_str_has_prefix(path, "/fb_socket"))
  {
      if (test->_firebolt_connection != nullptr) {
          g_print("Firebolt connection is already established, ignoring new connection request\n");
          return;
      }
      test->_firebolt_connection = g_object_ref (connection);
  }
  else if (g_str_has_prefix(path, "/test_socket"))
  {
      EXPECT_EQ(test->_test_connection, nullptr);
      test->_test_connection = g_object_ref (connection);
  }
  else
  {
      g_print("Ignoring WebSocket connection request\n");
      return;
  }

  g_signal_connect (
      connection,
      "message",
      G_CALLBACK (+[](SoupWebsocketConnection *connection,
                      SoupWebsocketDataType type,
                      GBytes *message,
                      gpointer user_data) {
          EXPECT_EQ(type, SOUP_WEBSOCKET_DATA_TEXT);
          if (type != SOUP_WEBSOCKET_DATA_TEXT)
              return;

          gsize sz;
          const void *ptr = g_bytes_get_data(message, &sz);

          EXPECT_TRUE(ptr != nullptr);
          EXPECT_GT(sz, 0);

          if (!ptr || sz == 0)
              return;

          auto *test = reinterpret_cast<BrowserLauncherTest*>(user_data);
          test->onMessage(connection, std::string(reinterpret_cast<const char*>(ptr), sz));
          test->breakIfNeeded();
      }), user_data);

  g_signal_connect (
      connection,
      "closed",
      G_CALLBACK (+[](SoupWebsocketConnection *connection, gpointer user_data){
          g_print ("WebSocket connection(%p) closed.\n", connection);
          auto *test = reinterpret_cast<BrowserLauncherTest*>(user_data);
          test->onConnectionClosed(connection);
          test->breakIfNeeded();
          g_object_unref (connection); // Release the connection
      }), user_data);

  g_print ("WebSocket connection established.\n");
  test->breakIfNeeded();
}

// Runs glib's main loop and periodically checks for `pred()`. Stops when `pred()` returns true or
// given `timeout` is reached. Returns false if timed out.
bool BrowserLauncherTest::runUntil(
    std::function<bool()> && pred,
    const std::chrono::milliseconds timeout,
    const std::chrono::milliseconds poll_period)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    bool result = true;
    GSource *timeoutSource = g_timeout_source_new(poll_period.count());
    g_source_set_callback(timeoutSource, G_SOURCE_FUNC(+[](gpointer data) {
        auto* loop = reinterpret_cast<GMainLoop*>(data);
        g_main_loop_quit(loop);
        return G_SOURCE_CONTINUE;
    }), _loop, nullptr);
    g_source_attach(timeoutSource, g_main_context_get_thread_default());
    while (!pred())
    {
        if (std::chrono::steady_clock::now() > deadline)
        {
            result = false;
            break;
        }
        _should_break_event_loop = true;
        g_main_loop_run(_loop);
        _should_break_event_loop = false;
    }
    g_source_destroy(timeoutSource);
    g_source_unref(timeoutSource);
    return result;
}

void BrowserLauncherTest::createCompositor()
{
#if defined(HAVE_WESTEROS_COMPOSITOR)
    if (_compositor)
        return;

    int window_width = 1920;
    int window_height = 1080;

    // Setup Essos
    _ess_ctx = EssContextCreate();
    if (!EssContextStart(_ess_ctx))
    {
        const char *detail = EssContextGetLastErrorDetail(_ess_ctx);
        g_critical("Couldn't create essos context, err = %s", detail);
        g_clear_pointer(&_ess_ctx, EssContextDestroy);
        return;
    }
    EssContextGetDisplaySize(_ess_ctx, &window_width, &window_height);

    // Create and setup compositor
    _compositor = WstCompositorCreate();

    WstCompositorSetIsEmbedded(_compositor, true);
    WstCompositorSetOutputSize(_compositor, window_width, window_height);
    WstCompositorSetClientStatusCallback(_compositor, +[](WstCompositor *wctx, int status, int client_pid, int detail, void *user_data) {
        auto *self = reinterpret_cast<BrowserLauncherTest*>(user_data);
        struct InvokeData {
            BrowserLauncherTest* self;
            int status;
        };
        g_message("Wst status: 0x%x, detail: %d, client pid: %d", status, detail, client_pid);
        g_main_context_invoke_full(
            self->_context,
            G_PRIORITY_DEFAULT,
            [](gpointer data) {
                auto *invoke_data = reinterpret_cast<InvokeData*>(data);
                switch (invoke_data->status) {
                    case WstClient_disconnected:
                    case WstClient_connected:
                        invoke_data->self->_first_frame_ts = -1;
                        break;
                    case WstClient_firstFrame:
                        invoke_data->self->_first_frame_ts = g_get_monotonic_time();
                        break;
                    default:
                        break;
                }
                return G_SOURCE_REMOVE;
            },
            new InvokeData { self, status},
            [](gpointer data) {
                delete reinterpret_cast<InvokeData*>(data);
            });
    }, this);

    // Setup draw callback
    static GSourceFuncs s_sourceFunctions = {
      .prepare = nullptr,
      .check = nullptr,
      .dispatch = [](GSource* source, GSourceFunc callback, gpointer user_data) -> gboolean
      {
          if (g_source_get_ready_time(source) == -1)
              return G_SOURCE_CONTINUE;
          g_source_set_ready_time(source, -1);
          return callback(user_data);
      },
      .finalize = nullptr,
      .closure_callback = nullptr,
      .closure_marshal = nullptr,
    };
    GSource *draw_event_source = g_source_new(&s_sourceFunctions, sizeof(GSource));
    g_source_set_callback(draw_event_source, +[](gpointer user_data) -> gboolean {
        auto *self = reinterpret_cast<BrowserLauncherTest*>(user_data);
        if (!self->_ess_ctx || !self->_compositor)
            return G_SOURCE_REMOVE;
        const float identity[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };
        int window_width = 1920;
        int window_height = 1080;
        bool needHolePunch = false;
        std::vector<WstRect> rects;
        unsigned int hints = WstHints_applyTransform | WstHints_noRotation | WstHints_holePunch;
        EssContextGetDisplaySize(self->_ess_ctx, &window_width, &window_height);
        WstCompositorComposeEmbedded(self->_compositor,
                                     0, 0,
                                     window_width, window_height,
                                     const_cast<float*>(identity), 1.0,
                                     hints, &needHolePunch, rects);
        EssContextUpdateDisplay(self->_ess_ctx);
        EssContextRunEventLoopOnce(self->_ess_ctx);
        if (self->_first_frame_ts != -1)
        {
            ++self->_frame_count;
            g_message("draw_count: %d", self->_frame_count);
        }
        return G_SOURCE_CONTINUE;
    }, this, nullptr);
    g_source_set_priority(draw_event_source, G_PRIORITY_HIGH);
    g_source_set_can_recurse(draw_event_source, TRUE);
    g_source_set_ready_time(draw_event_source, -1);
    g_source_attach(draw_event_source, _context);

    WstCompositorSetInvalidateCallback(_compositor, [](WstCompositor *, void *user_data) {
        auto *source = reinterpret_cast<GSource*>(user_data);
        if (g_source_get_ready_time(source) == -1)
            g_source_set_ready_time(source, g_get_monotonic_time() + 16666);
    }, draw_event_source);

    if (const char* parent_display = g_getenv("WAYLAND_DISPLAY"))
    {
        WstCompositorSetIsNested(_compositor, true);
        WstCompositorSetNestedDisplayName(_compositor, parent_display);
    }

    if (!WstCompositorStart(_compositor))
    {
        g_critical("failed to start the compositor: %s", WstCompositorGetLastErrorDetail(_compositor));
        destroyCompositor();
        return;
    }
#endif
}

void BrowserLauncherTest::destroyCompositor()
{
#if defined(HAVE_WESTEROS_COMPOSITOR)
    g_clear_pointer(&_compositor, WstCompositorDestroy);
    g_clear_pointer(&_ess_ctx, EssContextDestroy);
#endif
}
