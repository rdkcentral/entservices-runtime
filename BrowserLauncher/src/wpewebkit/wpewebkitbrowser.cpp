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
#include "wpewebkitbrowser.h"

#include "wpewebkitconfig.h"
#include "wpewebkitview.h"
#include "wpewebkitutils.h"

#include <cinttypes>
#include <string_view>
#include <filesystem>

#include <sys/syscall.h>
#include <glib.h>

namespace fs = std::filesystem;

WpeWebKitBrowser::WpeWebKitBrowser()
    : m_tryCloseTimeout(150)
    , m_browserTerminateTimeout(10000)
    , m_hangPollIntervalSecs(5)
    , m_maxUnresponsiveTimeSecs(60)
    , m_unresponsivePingNum(0)
{
}

WpeWebKitBrowser::~WpeWebKitBrowser()
{
    g_assert(m_mainView.get() == nullptr);
}

bool WpeWebKitBrowser::launch(const std::shared_ptr<const LaunchConfigInterface> &launchConfig)
{
    // sanity check
    if (m_mainView)
    {
        g_warning("Browser already launched / running");
        return false;
    }

    g_message("Launching WPEWebKit " VERSION_FORMAT, VERSION_ARGS(WpeWebKitUtils::webkitVersion()));

    // remember the main context to dispatch callbacks on
    m_runLoop = std::make_unique<RunLoop>();

    // override the max timeout time
    m_maxUnresponsiveTimeSecs = launchConfig->maxUnresponsiveTimeMs() / 1000;

    // remove any old gst cache from previous runs
    std::error_code ec;
    if (fs::path gstreamerCache = fs::path { g_get_user_cache_dir() } / "gstreamer-1.0";
        fs::exists(gstreamerCache, ec))
    {
        g_message("clearing gstreamer cache");
        fs::remove_all(gstreamerCache, ec);
    }

    // convert the generic launchConfig to WPE specific configuration
    auto config = std::make_shared<WpeWebKitConfig>(launchConfig);

    // set the environment variables from the config
    config->setEnvironment();

    WpeWebKitViewCallbacks viewCallbacks {
        // close
        [this](CloseReason reason) {
            onBrowserClose(reason);
        },
        // processTerminated
        [this]() {
            onBrowserCrashed();
        },
        // notifyResponsive
        [this]() {
            g_info("received responsive notification");
            m_unresponsivePingNum = 0;
        }
    };

    // create the WPE instance with the supplied config
    m_mainView = std::make_unique<WpeWebKitView>(config, std::move(viewCallbacks));

    // launch it with the given config
    if (!m_mainView->createView())
    {
        return false;
    }

    GSource *hangTimerSource = g_timeout_source_new_seconds(m_hangPollIntervalSecs);
    g_source_set_callback(hangTimerSource, G_SOURCE_FUNC(+[](WpeWebKitBrowser* self) {
        return self->checkBrowserResponsiveness();
    }), this, nullptr);
    g_source_attach(hangTimerSource, g_main_context_get_thread_default());
    g_source_unref(hangTimerSource);

    // notify browser launched on next cycle
    m_runLoop->InvokeTask([this] {
        g_message("signalling that the browser has launched");
        onLaunched.emit();
    }, 0);
    return true;
}

void WpeWebKitBrowser::dispose()
{
    // Destroy web view
    g_message("dispose: destroying browser view");
    m_mainView.reset();
}

void WpeWebKitBrowser::navigateTo(const std::string &url)
{
    // sanity check
    if (!m_mainView)
    {
        g_warning("navigateTo: browser not running - nothing to do");
        return;
    }
    // navigate to the supplied url
    m_mainView->loadUrl(url);
}

bool WpeWebKitBrowser::setState(PageLifecycleState state)
{
    // sanity check
    if (!m_mainView)
    {
        g_warning("setState: browser not running - nothing to do");
        return false;
    }

    g_message("setState: state=%s(0x%x)", toString(state), static_cast<unsigned>(state));

    m_mainView->setState(state);

    if (state == PageLifecycleState::TERMINATED)
    {
        close();
    }

    return true;
}

void WpeWebKitBrowser::setScreenSupportsHDR(bool enable)
{
    // sanity check
    if (!m_mainView)
    {
        g_warning("setScreenSupportsHDR: browser not running - nothing to do");
        return;
    }

    m_mainView->setScreenSupportsHDR(enable);
}

void WpeWebKitBrowser::close()
{
    // sanity check
    if (!m_mainView)
    {
        g_warning("close: browser not running - nothing to do");
        return;
    }

    if (m_unloading)
    {
        g_print("browser is unloading - nothing to do");
        return;
    }

    g_message("closing wpe webkit");

    uint32_t closeTimeout = 0;
    if (m_mainView->tryClose())
    {
        g_message("sent message to try gracefully to close the page");
        closeTimeout = m_tryCloseTimeout;
    }

    // start a timer to force close the browser after timeout
    m_runLoop->InvokeTask([this] {
        g_warning("timed-out waiting for the web page to close");
        onBrowserClose(CloseReason::UNLOAD);
    }, closeTimeout);
}

void WpeWebKitBrowser::onBrowserClose(CloseReason reason)
{
    g_message("browser close, reason = %s(0x%x)", toString(reason), static_cast<unsigned>(reason));
    m_runLoop->InvokeTask([this, reason] {
        if (reason != CloseReason::DEACTIVATE)
            m_unloading = true;
        onClose.emit(reason);
    }, 0);
}

void WpeWebKitBrowser::onBrowserCrashed()
{
    g_critical("fatal browser error occurred - terminating browser");
    onBrowserClose(CloseReason::ERROR);
}

void WpeWebKitBrowser::onBrowserUnresponsive(
    int64_t secsSinceLastResponsive, int webProcessPid)
{
    g_warning("detected browser (pid %d) is unresponsive, has been for %" PRId64 " seconds",
             webProcessPid, secsSinceLastResponsive);

    g_assert(webProcessPid > 0);

    // check if the browser has been unresponsive for too long
    if (secsSinceLastResponsive > m_maxUnresponsiveTimeSecs)
    {
        g_critical("browser (pid %d) has been unresponsive for too long, terminating",
                  webProcessPid);

        if (syscall(__NR_tgkill, webProcessPid, webProcessPid, SIGFPE) < 0)
        {
            g_critical("tgkill failed for pid / tid %d, errno=%d", webProcessPid, errno);
            std::quick_exit(EXIT_FAILURE);
        }
        else
        {
            // start a timer to signal the launcher exit after 10sec, this will
            // allow the minidump generation to get completed before the
            // launcher exits
            m_runLoop->InvokeTask([] {
                g_warning("timed-out waiting for the browser to terminate after SIGFPE signalled");
                std::quick_exit(EXIT_FAILURE);
            }, m_browserTerminateTimeout);
        }
    }
}

int WpeWebKitBrowser::checkBrowserResponsiveness()
{
    if (!m_mainView)
        return G_SOURCE_REMOVE;

    const bool isResponsive = m_mainView->checkResponsive();
    g_info("check browser responsiveness : %s", isResponsive ? "true" : "false");

    if (isResponsive)
    {
        m_unresponsivePingNum = 0;
    }
    else
    {
        ++m_unresponsivePingNum;

        const uint32_t secsUnresponsive = m_unresponsivePingNum * m_hangPollIntervalSecs;
        const int webProcessPid = m_mainView->getWebProcessIdentifier();
        onBrowserUnresponsive(secsUnresponsive, webProcessPid);
    }

    return G_SOURCE_CONTINUE;

}

// Factory function for instantiating WpeWebKitBrowser.
BrowserInterface* CreateBrowserInterface()
{
    return new WpeWebKitBrowser();
}
