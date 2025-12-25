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

#include <memory>
#include <functional>

#include "launchconfiginterface.h"
#include "simplesignalslot.h"

enum class CloseReason : uint8_t
{
    DEACTIVATE,  // conceal browser window (window.minimize)
    UNLOAD,      // completely unload (window.close)
    ERROR,       // web page crashed
};

// Page lifecycle states as defined in
// https://developer.chrome.com/docs/web-platform/page-lifecycle-api
enum class PageLifecycleState : uint8_t
{
    INITIALIZING = (1 << 0),
    ACTIVE       = (1 << 1),
    PASSIVE      = (1 << 2),
    HIDDEN       = (1 << 3),
    FROZEN       = (1 << 4),
    TERMINATED   = (1 << 5),
};

class BrowserInterface
{
public:
    virtual ~BrowserInterface() = default;

    // Setup browser with given launch config and prepare to load web app
    virtual bool launch(
        const std::shared_ptr<const LaunchConfigInterface> &launchConfig) = 0;

    // Request loading of the specified web app
    virtual void navigateTo(const std::string &url) = 0;

    // Change the lifecycle state of the web page
    virtual bool setState(PageLifecycleState state) = 0;

    // Let browser know that screen supports HDR
    virtual void setScreenSupportsHDR(bool enable) = 0;

    // Destroy the main web view
    virtual void dispose() = 0;

    // Notifies that browser launched and ready to load url
    Signal<> onLaunched;

    // Notifies that browser needs to be closed (or concealed)
    Signal<CloseReason> onClose;
};

// Factory function to instantiate browser instance
typedef BrowserInterface* (*CreateBrowserInterfaceFunctionPtr)();

// Helper functions for logging
static inline const char* toString(CloseReason state)
{
#define CASE(x) case CloseReason::x: return #x
    switch(state)
    {
        CASE(DEACTIVATE);
        CASE(UNLOAD);
        CASE(ERROR);
    }
#undef CASE
    return "Unknown";
}
static inline const char* toString(PageLifecycleState state)
{
#define CASE(x) case PageLifecycleState::x: return #x
    switch(state)
    {
        CASE(INITIALIZING);
        CASE(ACTIVE);
        CASE(PASSIVE);
        CASE(HIDDEN);
        CASE(FROZEN);
        CASE(TERMINATED);
    }
#undef CASE
    return "Unknown";
}
