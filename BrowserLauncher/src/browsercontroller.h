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

#include "browserinterface.h"
#include "runloop.h"

#include <firebolt/firebolt.h>

#include <memory>
#include <thread>

class BrowserController
{
public:
    BrowserController(BrowserInterface *impl,
                      std::shared_ptr<LaunchConfigInterface> launchConfig,
                      std::string &&packageUrl);

    ~BrowserController();

    void launch();
    void close();

private:
    void onBrowserLaunched();
    void onFireboltConnected();
    void onBrowserClose(CloseReason);
    void onLifecycleStateChanged(std::vector<Firebolt::Lifecycle::StateChange>);
    void onFocusedChanged(bool);

    BrowserInterface * const m_browser;
    std::shared_ptr<LaunchConfigInterface> m_launchConfig;
    std::string m_packageUrl;

    bool m_isFocused { false };
    Firebolt::Lifecycle::LifecycleState m_lifecycleState { Firebolt::Lifecycle::LifecycleState::INITIALIZING };
    std::unique_ptr<RunLoop> m_mainRunLoop;
    std::jthread m_connectJob;
};
