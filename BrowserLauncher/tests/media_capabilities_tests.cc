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

#include <unordered_map>
#include <vector>
#include <optional>
#include <algorithm>

namespace {

class MediaCapabilitiesTest: public BrowserLauncherTest
{
protected:
    void onFireboltMessage(const json& message) override;
    void onTestMessage(const json& message) override;

    std::vector<unsigned int> _hdr_listeners;
    bool _is_hdr_on { true };
    unsigned _req_id { 0 };

    std::unordered_map<unsigned, json> _pending_test_replies;
    std::unordered_map<unsigned, json> _pending_test_errors;
};

void MediaCapabilitiesTest::onFireboltMessage(const json& message)
{
    EXPECT_FALSE(message.contains("error"));
    if (HasFatalFailure())
        return;

    const unsigned int id = message["id"].get<unsigned int>();
    const std::string method = message["method"].get<std::string>();
    if (method == "Device.onHdrChanged")
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
            _hdr_listeners.push_back(id);
        }
        else
        {
            _hdr_listeners.erase(
                std::remove(_hdr_listeners.begin(), _hdr_listeners.end(), id),
                _hdr_listeners.end());
        }
    }
    else if (method == "Device.hdr")
    {
        json result = {
            {"jsonrpc", "2.0"},
            {"id", id},
            {"result",
              {
                 {"dolbyVision", _is_hdr_on},
                 {"hdr10", _is_hdr_on},
                 {"hdr10Plus", _is_hdr_on},
                 {"hlg", _is_hdr_on}
              }
            }
        };
        sendFireboltMessage(result);
    }
    else
    {
        BrowserLauncherTest::onFireboltMessage(message);
    }
}

void MediaCapabilitiesTest::onTestMessage(const json& message)
{
    const unsigned int id = message["id"].get<unsigned int>();
    if (message.contains("result")) {
        _pending_test_replies[id] = message["result"];
        return;
    }
    if (message.contains("error")) {
        _pending_test_errors[id] = message["error"];
        return;
    }
    // const std::string method = message["method"].get<std::string>();
}

class HdrSupportTest: public MediaCapabilitiesTest
                    , public ::testing::WithParamInterface<bool>
{
};

struct DecodingInfoTestConfig
{
    std::string _type;
    struct VideoConfiguration {
        std::string _content_type;
        unsigned long _width;
        unsigned long _height;
        double _framerate;
        unsigned long long _bitrate;
    };
    struct AudioConfiguration {
        std::string _content_type;
        std::string _channels;
    };
    std::optional<VideoConfiguration> _video;
    std::optional<AudioConfiguration> _audio;
};

class DecodingInfoTest: public MediaCapabilitiesTest
                      , public ::testing::WithParamInterface<DecodingInfoTestConfig>
{
};

}  // namespace

TEST_P(HdrSupportTest, InitialHdrSetting)
{
    _is_hdr_on = GetParam();

    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/media_capabilities.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return _firebolt_connection != nullptr && _test_connection != nullptr;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    if (HasFatalFailure())
        return;

    changeLifecycleStateState(LifecycleState::INITIALIZING, LifecycleState::ACTIVE, true);

    unsigned id = _req_id++;
    json req = {
        {"id", id},
        {"method", "MediaCapabilitiesTest.isHDROn"}
    };
    sendTestMessage(req);
    bool timed_out = !runUntil([this, id] {
        return _pending_test_replies.contains(id);
    }, 5s);
    EXPECT_FALSE(timed_out) << "timed out waiting for result";
    EXPECT_TRUE(_pending_test_replies.contains(id));

    if (_pending_test_replies.contains(id))
    {
        json result = _pending_test_replies[id];
        EXPECT_FALSE(result.is_discarded());
        if (!result.is_discarded())
        {
            EXPECT_EQ(result.get<bool>(), _is_hdr_on);
        }
        _pending_test_replies.erase(id);
    }

    EXPECT_TRUE(_pending_test_replies.empty());
}

TEST_P(HdrSupportTest, HdrSettingChange)
{
    _is_hdr_on = GetParam();

    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/media_capabilities.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return
                _firebolt_connection != nullptr &&
                _test_connection != nullptr &&
                !_hdr_listeners.empty();
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    changeLifecycleStateState(LifecycleState::INITIALIZING, LifecycleState::ACTIVE, true);

    for (int i = 0; i < 2; ++i)
    {
        // check current state
        unsigned id = _req_id++;
        json req = {
            {"id", id},
            {"method", "MediaCapabilitiesTest.isHDROn"}
        };
        sendTestMessage(req);
        bool timed_out = !runUntil([this, id] {
            return _pending_test_replies.contains(id);
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for result";
        EXPECT_TRUE(_pending_test_replies.contains(id));

        if (_pending_test_replies.contains(id))
        {
            json result = _pending_test_replies[id];
            EXPECT_FALSE(result.is_discarded());
            if (!result.is_discarded())
            {
                EXPECT_EQ(result.get<bool>(), _is_hdr_on);
            }
            _pending_test_replies.erase(id);
        }
        EXPECT_TRUE(_pending_test_replies.empty());

        if (i == 0)
        {
            // flip the setting
            _is_hdr_on = !_is_hdr_on;
            json result = {
                {"dolbyVision", _is_hdr_on},
                {"hdr10", _is_hdr_on},
                {"hdr10Plus", _is_hdr_on},
                {"hlg", _is_hdr_on}
            };
            if (!_hdr_listeners.empty())
            {
                json message = {
                    {"jsonrpc", "2.0"},
                    {"method", "Device.onHdrChanged"},
                    {"params", result}
                };
                sendFireboltMessage(message);
            }

            // wait for browser to apply the change
            runUntil([] {
                return false;
            }, 100ms, 100ms);
        }
    }
}

INSTANTIATE_TEST_SUITE_P(MediaCapabilitiesTests,
                         HdrSupportTest,
                         ::testing::Values(true, false));

TEST_P(DecodingInfoTest, CodecSupport)
{
    auto param = GetParam();
    _is_hdr_on = true;

    // launch browser
    launchBrowser(gchar_ptr(g_strdup_printf("http://127.0.0.1:%u/tests/media_capabilities.html", kTestServerPort)).get());

    // wait for launcher to establish "firebolt" connection
    {
        bool timed_out = !runUntil([this] {
            return _firebolt_connection != nullptr && _test_connection != nullptr;
        }, 5s);
        EXPECT_FALSE(timed_out) << "timed out waiting for browser launcher";
    }

    if (HasFatalFailure())
        return;

    changeLifecycleStateState(LifecycleState::INITIALIZING, LifecycleState::ACTIVE, true);

    unsigned id = _req_id++;
    json req = {
        {"id", id},
        {"method", "MediaCapabilitiesTest.decodingInfo"},
        {"params", { {"type", param._type} } }
    };

    if (param._video.has_value())
    {
        json config = {
            {
                "video",
                {
                    {"contentType", param._video->_content_type},
                    {"width", param._video->_width},
                    {"height", param._video->_height},
                    {"framerate", param._video->_framerate},
                    {"bitrate", param._video->_bitrate}
                }
            }
        };
        req["params"].update(config, true);
    }

    if (param._audio.has_value())
    {
        json config = {
            {
                "audio",
                {
                    {"contentType", param._audio->_content_type},
                    {"channels", param._audio->_channels}
                }
            }
        };
        req["params"].update(config, true);
    }

    sendTestMessage(req);
    bool timed_out = !runUntil([this, id] {
        return _pending_test_replies.contains(id) || _pending_test_errors.contains(id);
    }, 5s);
    EXPECT_FALSE(timed_out) << "timed out waiting for result";
    EXPECT_FALSE(_pending_test_errors.contains(id));
    EXPECT_TRUE(_pending_test_replies.contains(id));

    if (_pending_test_replies.contains(id))
    {
        json result = _pending_test_replies[id];
        EXPECT_FALSE(result.is_discarded());
        if (!result.is_discarded())
        {
            EXPECT_TRUE(result.contains("supported"));
            EXPECT_TRUE(result.value("supported", json(false)).get<bool>());
        }
        _pending_test_replies.erase(id);
    }
    EXPECT_TRUE(_pending_test_replies.empty());
}

std::vector<DecodingInfoTestConfig> GetDecodingInfoTestConfigs() {
    return {
        {
            ._type = "media-source",
            ._video = {
                {
                    ._content_type = "video/mp4;codecs=\"avc1.42000a\"",
                    ._width = 1080,
                    ._height = 720,
                    ._framerate = 30.0,
                    ._bitrate = 10000
                }
            }
        },
        {
            ._type = "media-source",
            ._video = {
                {
                    ._content_type = "video/mp4;codecs=\"hev1.1.6.L93.B0\"",
                    ._width = 1080,
                    ._height = 720,
                    ._framerate = 30.0,
                    ._bitrate = 10000
                }
            }
        },
        {
            ._type = "media-source",
            ._video = {
                {
                    ._content_type = "video/mp4;codecs=\"av01.0.00M.08\"",
                    ._width = 1080,
                    ._height = 720,
                    ._framerate = 30.0,
                    ._bitrate = 10000
                }
            }
        },
        {
            ._type = "media-source",
            ._audio = {
                {
                    ._content_type = "audio/mp4;codecs=\"mp4a.40.1\"",
                    ._channels = "2"
                }
            }
        },
        {
            ._type = "media-source",
            ._audio = {
                {
                    ._content_type = "audio/mp4;codecs=\"opus\"",
                    ._channels = "2"
                }
            }
        },
        {
            ._type = "media-source",
            ._audio = {
                {
                    ._content_type = "audio/mp4;codecs=\"ac-3\"",
                    ._channels = "2"
                }
            }
        },
        {
            ._type = "media-source",
            ._audio = {
                {
                    ._content_type = "audio/mp4;codecs=\"ec-3\"",
                    ._channels = "2"
                }
            }
        },
        {
            ._type = "media-source",
            ._audio = {
                {
                    ._content_type = "audio/flac",
                    ._channels = "2"
                }
            }
        },
    };
}

INSTANTIATE_TEST_SUITE_P(MediaCapabilitiesTests,
                         DecodingInfoTest,
                         ::testing::ValuesIn(GetDecodingInfoTestConfigs()));
