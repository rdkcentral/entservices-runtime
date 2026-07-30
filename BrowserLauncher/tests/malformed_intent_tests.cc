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

#include <string>

namespace {

struct MalformedIntentParam
{
    std::string description;
    json        payload;
};

// Returns malformed Actions.intent payloads under test.
const MalformedIntentParam kMalformedIntentCases[] = {
    {
        "null_result",
        nullptr
    },
    {
        "null_intent",
        {{"intent", nullptr}, {"intentId", 0}}
    },
    {
        "empty_string_intent",
        {{"intent", ""}, {"intentId", 0}}
    },
    {
        "empty_object_intent",
        {{"intent", json::object()}, {"intentId", 0}}
    },
    {
        "incorrect_type_of_intent",
        {{"intent", 42}, {"intentId", 0}}
    },
    {
        "incorrect_type_of_action",
        {{"intent", {{"action", 37}, {"context", {{"source", "system"}}}}}, {"intentId", 0}}
    },
};

// Name provider for INSTANTIATE_TEST_SUITE_P — produces readable test names
struct MalformedIntentParamName
{
    std::string operator()(const ::testing::TestParamInfo<MalformedIntentParam>& info) const
    {
        return info.param.description;
    }
};

class MalformedIntentTest : public BrowserLauncherTest
                          , public ::testing::WithParamInterface<MalformedIntentParam>
{
protected:
    void onTestMessage(const json& message) override;
    void onFireboltMessage(const json& message) override;

    void loadTestPage()
    {
        launchBrowser(
            gchar_ptr(g_strdup_printf(
                "http://127.0.0.1:%u/tests/page_lifecycle.html",
                kTestServerPort)).get());
    }

    std::string _page_state { "initializing" };
};

// Forward page lifecycle state updates reported by the in-page JS.
void MalformedIntentTest::onTestMessage(const json& message)
{
    if (message.contains("method") &&
        message["method"].get<std::string>() == "pageLifecycle.stateChanged")
    {
        if (message.contains("params") && message["params"].contains("state"))
            _page_state = message["params"]["state"].get<std::string>();
    }
}

// Intercept Actions.intent and return the malformed payload under test.
// All other methods are handled by the base class.
void MalformedIntentTest::onFireboltMessage(const json& message)
{
    if (message.contains("method"))
    {
        const std::string method = message["method"].get<std::string>();
        if (method == "Actions.intent")
        {
            const unsigned int id = message["id"].get<unsigned int>();
            json result = {
                {"jsonrpc", "2.0"},
                {"id",      id},
                {"result",  GetParam().payload}
            };
            sendFireboltMessage(result);
            return;
        }
    }

    BrowserLauncherTest::onFireboltMessage(message);
}

TEST_P(MalformedIntentTest, BrowserSurvivesMalformedInitialIntent)
{
    loadTestPage();

    // Wait for connections to be established.
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _test_connection != nullptr &&
                _state_change_listeners.size() > 0;
        }, 5s);
        EXPECT_FALSE(timed_out) << "Timed out waiting for browser launcher";
    }

    // Let it settle.
    runUntil([] {
        return false;
    }, 500ms, 500ms);

    // Browser process must still be running.
    EXPECT_NE(_firebolt_connection, nullptr)
        << "Firebolt connection closed unexpectedly — browser may have crashed";
}

INSTANTIATE_TEST_SUITE_P(
    MalformedIntent,
    MalformedIntentTest,
    ::testing::ValuesIn(kMalformedIntentCases),
    MalformedIntentParamName{});

}  // namespace
