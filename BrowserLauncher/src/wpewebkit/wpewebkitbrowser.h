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

#include "../browserinterface.h"
#include "../runloop.h"

#include <memory>
#include <string>
#include <optional>

typedef struct _GMainContext GMainContext;
class WpeWebKitView;
class BridgeObjectImpl;

class WpeWebKitBrowser final : public BrowserInterface
{
public:
    WpeWebKitBrowser();
    ~WpeWebKitBrowser() override;

    bool launch(const std::shared_ptr<const LaunchConfigInterface> &launchConfig) override;
    void dispose() override;
    void navigateTo(const std::string &url) override;
    bool setState(PageLifecycleState state) override;
    void setScreenSupportsHDR(bool enable) override;

private:
    void close();
    void onBrowserClose(CloseReason reason);
    void onBrowserCrashed();
    void onBrowserUnresponsive(int64_t secsSinceLastResponsive, int webProcessPid);
    void invokeOnMainThreadLoop(std::function<void()> call, std::optional<uint32_t> delay = {});
    int  checkBrowserResponsiveness();

private:
    GMainContext *m_mainThreadContext { nullptr };

    const int m_tryCloseTimeout;
    const int m_browserTerminateTimeout;
    const unsigned m_hangPollIntervalSecs;
    int m_maxUnresponsiveTimeSecs;
    uint32_t m_unresponsivePingNum;
    bool m_unloading { false };

    std::unique_ptr<WpeWebKitView> m_mainView;
    std::unique_ptr<RunLoop> m_runLoop;
};

extern "C" __attribute__((visibility("default"))) BrowserInterface* CreateBrowserInterface();
