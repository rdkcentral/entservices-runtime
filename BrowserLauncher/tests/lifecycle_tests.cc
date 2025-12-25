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

#include <vector>
#include <utility>

namespace {

// Convert Firebolt Lifecycle 2.0 state string to W3C Page Lifecycle string
static std::string toPageLifecycleState(LifecycleState fireboltState, bool focused)
{
    if (focused)
    {
        EXPECT_EQ(fireboltState, LifecycleState::ACTIVE);
        return "active";
    }
    else if (fireboltState == LifecycleState::ACTIVE)
    {
        EXPECT_FALSE(focused);
        return "passive";
    }
    else if (fireboltState == LifecycleState::PAUSED)
    {
        return "hidden";
    }
    else if (fireboltState == LifecycleState::SUSPENDED || fireboltState == LifecycleState::HIBERNATED)
    {
        return "frozen";
    }
    else if (fireboltState == LifecycleState::TERMINATING)
    {
        return "terminated";
    }
    else if (fireboltState == LifecycleState::INITIALIZING)
    {
        return "initializing";
    }
    EXPECT_TRUE(false) << "Unexpected firebolt state: " << fireboltState;
    return "";
}

class LifecycleStateTest: public BrowserLauncherTest
{
protected:
    void onTestMessage(const json& message) override;
    void onConnectionClosed(SoupWebsocketConnection *connection) override;

    void sendWindowClose();
    void sendWindowMinimize();

    std::string _page_state { "initializing" };
};

void LifecycleStateTest::sendWindowClose()
{
    json message = {
        {"jsonrpc", "2.0"},
        {"method", "Window.close"}
    };
    sendTestMessage(message);
}

void LifecycleStateTest::sendWindowMinimize()
{
    json message = {
        {"jsonrpc", "2.0"},
        {"method", "Window.minimize"}
    };
    sendTestMessage(message);
}

void LifecycleStateTest::onTestMessage(const json& message)
{
    EXPECT_FALSE(message.contains("error"));
    if (HasFatalFailure())
        return;

    // const unsigned int id = message["id"].get<unsigned int>();
    const std::string method = message["method"].get<std::string>();

    if (method == "LifecycleTest.onStateChanged")
    {
        auto params = message["params"];
        EXPECT_FALSE(params.is_discarded());
        EXPECT_TRUE(params.contains("oldState"));
        EXPECT_TRUE(params.contains("newState"));

        auto oldState = params["oldState"].get<std::string>();
        auto newState = params["newState"].get<std::string>();

        EXPECT_EQ(oldState, _page_state);
        _page_state = newState;
    }
}

void LifecycleStateTest::onConnectionClosed(SoupWebsocketConnection *connection)
{
    if (connection == _test_connection)
    {
        if (_current_lc_state == LifecycleState::TERMINATING)
        {
            // The web socket message sent on browser shutdown is not guaranteed to be delivered,
            // change the state explicitly here to make tests more reliable.
            _page_state = "terminated";
        }
    }
    BrowserLauncherTest::onConnectionClosed(connection);
}

}  // namespace

TEST_F(LifecycleStateTest, SunnyDay)
{
    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/page_lifecycle.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _state_change_listeners.size() > 0;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    EXPECT_NE(_firebolt_connection, nullptr);
    EXPECT_EQ(_state_change_listeners.size(), 1);

    if (HasFatalFailure())
        return;

    // wait for web page to connect and report intial state
    {
        // NB: browser starts in 'hidden' state, we need to adjust firebolt state
        // to keep the test in sync with browser
        bool focused = false;
        const auto oldState = LifecycleState::INITIALIZING;
        const auto newState = LifecycleState::PAUSED;
        const std::string pageState = toPageLifecycleState(newState, focused);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            return _test_connection != nullptr && _page_state == pageState;
        }, 5s);
        EXPECT_NE(_test_connection, nullptr);
        EXPECT_FALSE(timed_out) << "timed out awaiting for initial page state change";
    }

    if (HasFatalFailure())
        return;

    // test state transitions
    std::vector<std::tuple<LifecycleState, LifecycleState, bool>> state_transitions = {
        {LifecycleState::PAUSED,    LifecycleState::ACTIVE,     false},
        {LifecycleState::ACTIVE,    LifecycleState::ACTIVE,     true},
        {LifecycleState::ACTIVE,    LifecycleState::ACTIVE,     false},
        {LifecycleState::ACTIVE,    LifecycleState::PAUSED,     false},
        {LifecycleState::PAUSED,    LifecycleState::SUSPENDED,  false},
        {LifecycleState::SUSPENDED, LifecycleState::HIBERNATED, false},
        {LifecycleState::HIBERNATED,LifecycleState::SUSPENDED,  false},
        {LifecycleState::SUSPENDED, LifecycleState::PAUSED,     false},
        {LifecycleState::PAUSED,    LifecycleState::ACTIVE,     false},
        {LifecycleState::ACTIVE,    LifecycleState::ACTIVE,     true},
        {LifecycleState::ACTIVE,    LifecycleState::ACTIVE,     false},
        {LifecycleState::ACTIVE,    LifecycleState::PAUSED,     false},
        {LifecycleState::PAUSED,    LifecycleState::TERMINATING,false},
    };

    for (const auto& transition : state_transitions)
    {
        const auto oldState = std::get<0>(transition);
        const auto newState = std::get<1>(transition);
        const bool focused = std::get<2>(transition);
        const std::string pageState = toPageLifecycleState(newState, focused);

        EXPECT_EQ (_current_lc_state, oldState);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            g_message("page state: %s, target: %s", _page_state.c_str(), pageState.c_str());
            return _page_state == pageState;
        }, 1s);
        EXPECT_FALSE(timed_out) << "timed out awaiting for page state change to: " << pageState;
        EXPECT_TRUE(_close_type.empty() || (newState == LifecycleState::TERMINATING && _close_type == "unload"));  // browser did not crash
        EXPECT_EQ(_page_state, pageState);   // page state changed
    }
}

TEST_F(LifecycleStateTest, LaunchToActive)
{
    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/page_lifecycle.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _state_change_listeners.size() > 0;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    EXPECT_NE(_firebolt_connection, nullptr);
    EXPECT_EQ(_state_change_listeners.size(), 1);

    // change firebolt lifecycle state and wait for web page to connect and report intial state
    {
        bool focused = true;
        const auto oldState = LifecycleState::INITIALIZING;
        const auto newState = LifecycleState::ACTIVE;
        const std::string pageState = toPageLifecycleState(newState, focused);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            return _test_connection != nullptr && _page_state == pageState;
        }, 1s);
        EXPECT_NE(_test_connection, nullptr);
        EXPECT_EQ(_page_state, pageState);
        EXPECT_FALSE(timed_out) << "timed out awaiting for initial page state change";
    }

    // attempt to shutdown browser gracefully
    changeLifecycleStateState(_current_lc_state, LifecycleState::TERMINATING);
    {
        runUntil([this] {
            return _page_state == "terminated" && !_close_type.empty();
        }, 500ms);
    }
    EXPECT_EQ(_close_type, "unload");
    EXPECT_EQ(_page_state, "terminated");
}

TEST_F(LifecycleStateTest, ResumeToActive)
{
    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/page_lifecycle.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _test_connection != nullptr &&
                _state_change_listeners.size() > 0;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    EXPECT_NE(_firebolt_connection, nullptr);
    EXPECT_EQ(_state_change_listeners.size(), 1);

    // first move browser to frozen state
    bool focused = false;
    auto oldState = LifecycleState::INITIALIZING;
    for (const auto newState : {LifecycleState::PAUSED, LifecycleState::SUSPENDED} )
    {
        const std::string pageState = toPageLifecycleState(newState, focused);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            return _test_connection != nullptr && _page_state == pageState;
        }, 1s);
        EXPECT_NE(_test_connection, nullptr);
        EXPECT_EQ(_page_state, pageState);
        EXPECT_FALSE(timed_out) << "timed out awaiting for initial page state change";

        oldState = newState;
    }

    // next resume to active
    {
        focused = true;
        const auto newState = LifecycleState::ACTIVE;
        const std::string pageState = toPageLifecycleState(newState, focused);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            return _test_connection != nullptr && _page_state == pageState;
        }, 1s);
        EXPECT_NE(_test_connection, nullptr);
        EXPECT_EQ(_page_state, pageState);
        EXPECT_FALSE(timed_out) << "timed out awaiting for initial page state change";
    }

    // attempt to shutdown browser gracefully
    changeLifecycleStateState(_current_lc_state, LifecycleState::TERMINATING);
    {
        runUntil([this] {
            return _page_state == "terminated" && !_close_type.empty();
        }, 500ms);
    }
    EXPECT_EQ(_close_type, "unload");
    EXPECT_EQ(_page_state, "terminated");
}

TEST_F(LifecycleStateTest, WindowClose)
{
    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/page_lifecycle.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _state_change_listeners.size() > 0;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    EXPECT_NE(_firebolt_connection, nullptr);
    EXPECT_EQ(_state_change_listeners.size(), 1);

    // change firebolt lifecycle state and wait for web page to connect and report intial state
    {
        bool focused = true;
        const auto oldState = LifecycleState::INITIALIZING;
        const auto newState = LifecycleState::ACTIVE;
        const std::string pageState = toPageLifecycleState(newState, focused);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            return _test_connection != nullptr && _page_state == pageState;
        }, 5s);
        EXPECT_NE(_test_connection, nullptr);
        EXPECT_FALSE(timed_out) << "timed out awaiting for initial page state change";
    }

    // send message to close the page
    sendWindowClose();
    {
        runUntil([this] {
            return !_close_type.empty();
        }, 500ms);
    }
    EXPECT_EQ(_close_type, "unload");
}

TEST_F(LifecycleStateTest, WindowMinimize)
{
    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/page_lifecycle.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _state_change_listeners.size() > 0;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    EXPECT_NE(_firebolt_connection, nullptr);
    EXPECT_EQ(_state_change_listeners.size(), 1);

    // change firebolt lifecycle state and wait for web page to connect and report intial state
    {
        bool focused = true;
        const auto oldState = LifecycleState::INITIALIZING;
        const auto newState = LifecycleState::ACTIVE;
        const std::string pageState = toPageLifecycleState(newState, focused);

        changeLifecycleStateState(oldState, newState, focused);

        bool timed_out = !runUntil([this, pageState] {
            return _test_connection != nullptr && _page_state == pageState;
        }, 5s);
        EXPECT_NE(_test_connection, nullptr);
        EXPECT_FALSE(timed_out) << "timed out awaiting for initial page state change";
    }

    // send message to minimize the page
    sendWindowMinimize();
    {
        runUntil([this] {
            return !_close_type.empty();
        }, 500ms);
    }
    EXPECT_EQ(_close_type, "deactivate");
}
