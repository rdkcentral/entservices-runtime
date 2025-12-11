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
#include "browsercontroller.h"

#include <glib.h>
#include <gio/gio.h>

BrowserController::BrowserController(BrowserInterface *impl,
                                     std::shared_ptr<LaunchConfigInterface> launchConfig,
                                     std::string &&packageUrl)
    : m_browser(impl)
    , m_launchConfig(launchConfig)
    , m_packageUrl(std::move(packageUrl))
{
}

BrowserController::~BrowserController()
{
}

// Called at start-up from the main event loop - should start the browser
// using supplied launch details.
void BrowserController::launch()
{
    m_mainRunLoop = std::make_unique<RunLoop>();

    // connect callbacks
    m_browser->onLaunched.connect(std::bind(&BrowserController::onBrowserLaunched, this));
    m_browser->onClose.connect(std::bind(&BrowserController::onBrowserClose, this, std::placeholders::_1));

    // launch the browser
    if (!m_browser->launch(m_launchConfig)) {
        g_message("Couldn't launch browser");
        g_application_quit(g_application_get_default());
        return;
    }

    if (auto fireboltEndpoint = m_launchConfig->fireboltEndpoint(); !fireboltEndpoint.empty())
    {
        Firebolt::Config cfg {
            .wsUrl = fireboltEndpoint,
            #ifndef NDEBUG
            .log = {
                .level = Firebolt::LogLevel::Debug,
            }
            #endif
        };
        Firebolt::IFireboltAccessor::Instance().Connect(cfg, [this](const bool connected, const Firebolt::Error code) {
            if (!connected)
            {
                g_message("Firebolt disconnected, code = %d", code);
                return;
            }
            if (!m_connectJob.joinable())
            {
                m_connectJob = std::jthread(std::bind(&BrowserController::onFireboltConnected, this));
            }
        });
    }
}

void BrowserController::close()
{
    if (m_lifecycleState != Firebolt::Lifecycle::LifecycleState::TERMINATING)
    {
        g_message("close: terminating");
        m_lifecycleState = Firebolt::Lifecycle::LifecycleState::TERMINATING;
        m_browser->setState(PageLifecycleState::TERMINATED);
    }
}

void BrowserController::onBrowserLaunched()
{
    g_assert(g_main_context_is_owner (g_main_context_default()));

    g_message("Browser launched");

    if (m_launchConfig->fireboltEndpoint().empty())
    {
        m_browser->setState(PageLifecycleState::ACTIVE);
    }

    m_browser->navigateTo(m_packageUrl);
}


void BrowserController::onFireboltConnected()
{
    using namespace Firebolt;

    // subscribe to lifecycle state change events
    {
        auto &presentation = Firebolt::IFireboltAccessor::Instance().PresentationInterface();
        Result<SubscriptionId> result = presentation.subscribeOnFocusedChanged([this](const bool focused) {
            m_mainRunLoop->InvokeTask([this, focused]() {
                onFocusedChanged(focused);
            });
        });
        if (!result)
        {
            g_warning("presentation.subscribeOnFocusedChanged failed, error code = %d", result.error());
        }
        auto &lifecycle = Firebolt::IFireboltAccessor::Instance().LifecycleInterface();
        result = lifecycle.subscribeOnStateChanged([this](const std::vector<Lifecycle::StateChange>& changes) {
            m_mainRunLoop->InvokeTask([this, changes = std::vector<Lifecycle::StateChange> { changes }]() {
                onLifecycleStateChanged(changes);
            });
        });
        if (!result)
        {
            g_warning("lifecycle.subscribeOnStateChanged failed, error code = %d", result.error());
        }
    }
}

void BrowserController::onBrowserClose(CloseReason reason)
{
    g_assert(g_main_context_is_owner (g_main_context_default()));

    g_message("Browser close, reason = %s(0x%x)", toString(reason), static_cast<uint8_t>(reason));

    auto &lifecycle = Firebolt::IFireboltAccessor::Instance().LifecycleInterface();
    switch(reason)
    {
        case CloseReason::DEACTIVATE:
        {
            auto result = lifecycle.close(Firebolt::Lifecycle::CloseType::DEACTIVATE);
            if (!result)
            {
                g_critical("Lifecycle.close(deactivate) failed, error: %d", result.error());
                break;
            }
            return;  // keep the browser running
        }
        case CloseReason::UNLOAD:
            lifecycle.close(Firebolt::Lifecycle::CloseType::UNLOAD);
            break;
        case CloseReason::ERROR:
        default:
            break;
    }
    m_mainRunLoop->Disable();
    Firebolt::IFireboltAccessor::Instance().Disconnect();

    g_application_quit(g_application_get_default());
}

void BrowserController::onLifecycleStateChanged(std::vector<Firebolt::Lifecycle::StateChange> changes)
{
    g_assert(g_main_context_is_owner (g_main_context_default()));

    auto toPageSate = [](Firebolt::Lifecycle::LifecycleState state, bool focused) {
        switch(state)
        {
            case Firebolt::Lifecycle::LifecycleState::ACTIVE:
                return focused ? PageLifecycleState::ACTIVE : PageLifecycleState::PASSIVE;
            case Firebolt::Lifecycle::LifecycleState::PAUSED:
                return PageLifecycleState::HIDDEN;
            case Firebolt::Lifecycle::LifecycleState::SUSPENDED:
            case Firebolt::Lifecycle::LifecycleState::HIBERNATED:
                return PageLifecycleState::FROZEN;
            case Firebolt::Lifecycle::LifecycleState::TERMINATING:
                return PageLifecycleState::TERMINATED;
            case Firebolt::Lifecycle::LifecycleState::INITIALIZING:
                return PageLifecycleState::INITIALIZING;
            default:
                break;
        };
        g_assert_not_reached();
    };

    for (auto& change : changes)
    {
        g_message("lifecycle state change: newState=0x%x, oldState=0x%x",
                  static_cast<unsigned>(change.newState),
                  static_cast<unsigned>(change.oldState));
        m_lifecycleState = change.newState;
        m_browser->setState(toPageSate(change.newState, m_isFocused));
    }
}

void BrowserController::onFocusedChanged(bool focused)
{
    g_assert(g_main_context_is_owner (g_main_context_default()));

    if (m_isFocused == focused)
        return;

    g_message("lifecycle focus change, focused = %c", focused ? 'y' : 'n');
    m_isFocused = focused;
    if (m_lifecycleState == Firebolt::Lifecycle::LifecycleState::ACTIVE)
    {
        m_browser->setState(focused ? PageLifecycleState::ACTIVE : PageLifecycleState::PASSIVE);
    }
}
