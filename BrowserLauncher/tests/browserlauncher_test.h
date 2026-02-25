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
#pragma once

#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <glib.h>

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "nlohmann/json.hpp"

using namespace std::chrono_literals;
using json = nlohmann::json;
static auto gfree_deleter = [](char* v) { g_free(v); };
using gchar_ptr = std::unique_ptr<gchar, decltype(gfree_deleter)>;

typedef struct _SoupServer SoupServer;
typedef struct _SoupWebsocketConnection SoupWebsocketConnection;
typedef struct _SoupServerMessage SoupServerMessage;
typedef struct _GSubprocessLauncher GSubprocessLauncher;
typedef struct _GSubprocess GSubprocess;
typedef struct _WstCompositor WstCompositor;
typedef struct _EssCtx EssCtx;

constexpr unsigned kTestServerPort = 8081;

enum class LifecycleState
{
    INITIALIZING = 0x0,
    ACTIVE       = 0x1,
    PAUSED       = 0x2,
    SUSPENDED    = 0x3,
    HIBERNATED   = 0x4,
    TERMINATING  = 0x5
};

std::ostream& operator<<(std::ostream& out, const LifecycleState&);

class BrowserLauncherTest : public ::testing::Test
{
private:
    SoupServer *_server { nullptr };
    unsigned _server_port { 0 };
    GSubprocessLauncher *_launcher { nullptr };
    GSubprocess *_runtime_process { nullptr };
    bool _should_break_event_loop { false };

    void onMessage(SoupWebsocketConnection *connection, const std::string& message_str);
    void sendMessage(SoupWebsocketConnection *connection, const json& message);
    void createMainLoop();
    void stopMainLoopAndServer();
    void createServer(unsigned port);
    void stopBrowser();
    void breakIfNeeded();
    void createCompositor();
    void destroyCompositor();

    static void websocketHandler (
        SoupServer              *server,
        SoupServerMessage       *msg,
        const char              *path,
        SoupWebsocketConnection *connection,
        gpointer                 user_data);

protected:
    GMainContext *_context { nullptr };
    GMainLoop *_loop { nullptr };
    SoupWebsocketConnection *_firebolt_connection { nullptr };
    SoupWebsocketConnection *_test_connection { nullptr };

    std::vector<unsigned int> _state_change_listeners;
    LifecycleState _current_lc_state { LifecycleState::INITIALIZING };
    std::string _close_type { };
    bool _focused { false };

    WstCompositor* _compositor { nullptr };
    EssCtx *_ess_ctx { nullptr };
    gint64 _first_frame_ts { -1 };
    gint64 _first_request_ts { -1 };
    int _frame_count { 0 };

    void SetUp() override {
        createMainLoop();
        createCompositor();
        createServer(kTestServerPort);
    }

    void TearDown() override {
        stopBrowser();
        destroyCompositor();
        stopMainLoopAndServer();
    }

    virtual void onFireboltMessage(const json& message);
    virtual void onTestMessage(const json& message) { }
    virtual void onConnectionClosed(SoupWebsocketConnection *connection);

    void changeLifecycleStateState(LifecycleState oldState, LifecycleState newState, bool focused = false);
    void sendFireboltMessage(const json& message) { sendMessage(_firebolt_connection, message); }
    void sendTestMessage(const json& message) { sendMessage(_test_connection, message); }
    void launchBrowser(const std::string& url, std::vector<std::string> args = { });
    bool runUntil(
        std::function<bool()> && pred,
        const std::chrono::milliseconds timeout,
        const std::chrono::milliseconds poll_period = 50ms);
};
