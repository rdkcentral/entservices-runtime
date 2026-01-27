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
#include "wpewebkitview.h"

#include "wpewebkitconfig.h"
#include "wpewebkitutils.h"
#include "wpewebkit_2.38.h"
#include "wpewebkit_2.46.h"

#if defined(ENABLE_TESTING)
#include "testing/testrunner.h"
#endif

#include <deque>
#include <cmath>
#include <cinttypes>

namespace {

// Destroys timer source conducting all necessary checks.
bool destroyAndZeroTimerSource(GSource** src)
{
    bool res = false;
    if (src != nullptr)
    {
        if (*src != nullptr)
        {
            if (!g_source_is_destroyed(*src))
            {
                g_source_destroy(*src);
                g_source_unref(*src);
                res = true;
            }
            *src = nullptr;
        }
    }
    return res;
}

PageLifecycleState nextState(PageLifecycleState current, PageLifecycleState target)
{
    // See for valid transitions:
    //
    // https://developer.chrome.com/docs/web-platform/page-lifecycle-api/image/page-lifecycle-api-state.svg
    //
    //                ACTIVE
    //              /   |
    // INITIALIZING - PASSIVE
    //              \   |
    //                HIDDEN - TERMINATED
    //                  |
    //                FROZEN
    //

    switch(current)
    {
        case PageLifecycleState::INITIALIZING:
            switch (target)
            {
                case PageLifecycleState::FROZEN:
                case PageLifecycleState::TERMINATED:
                    return PageLifecycleState::HIDDEN;
                default:
                    break;
            }
            break;

        case PageLifecycleState::ACTIVE:
            switch (target)
            {
                case PageLifecycleState::HIDDEN:
                case PageLifecycleState::FROZEN:
                case PageLifecycleState::TERMINATED:
                    return PageLifecycleState::PASSIVE;
                default:
                    break;
            }
            break;
        case PageLifecycleState::PASSIVE:
            switch (target)
            {
                case PageLifecycleState::FROZEN:
                case PageLifecycleState::TERMINATED:
                    return PageLifecycleState::HIDDEN;
                default:
                    break;
            }
            break;
        case PageLifecycleState::HIDDEN:
            switch (target)
            {
                case PageLifecycleState::ACTIVE:
                    return PageLifecycleState::PASSIVE;
                default:
                    break;
            }
            break;
        case PageLifecycleState::FROZEN:
            switch (target)
            {
                case PageLifecycleState::PASSIVE:
                case PageLifecycleState::ACTIVE:
                case PageLifecycleState::TERMINATED:
                    return PageLifecycleState::HIDDEN;
                default:
                    break;
            }
            break;
        case PageLifecycleState::TERMINATED:
            break;
    };
    return target;
}

}

class WpePageLifecycleDelegate
{
protected:
    WebKitWebView* m_view;
    PageLifecycleState m_currState { PageLifecycleState::INITIALIZING };

    const unsigned m_maxMemorySavingIterations;
    unsigned m_memorySavingTimerIteration { 0 };
    GSource *m_memorySavingTimerSource{nullptr};

    void enableMemorySaving();
    void disableMemorySaving();
    void onMemorySavingTimerTimeout();

public:
    WpePageLifecycleDelegate(WebKitWebView* view, unsigned maxMemorySavingIterations)
        : m_view(view)
        , m_maxMemorySavingIterations(maxMemorySavingIterations) { }

    virtual ~WpePageLifecycleDelegate()
    {
        destroyAndZeroTimerSource(&m_memorySavingTimerSource);
    }

    void changeState(PageLifecycleState newState);
    PageLifecycleState currentState() const { return m_currState; }

    virtual void show()   = 0;
    virtual void hide()   = 0;
    virtual void freeze() = 0;
    virtual void resume() = 0;
    virtual void focus()  = 0;
    virtual void blur()   = 0;
    virtual void tryClose() = 0;
};

void WpePageLifecycleDelegate::changeState(PageLifecycleState newState)
{
    g_message("changeState: %s(0x%x) -> %s(0x%x)",
              toString(m_currState), static_cast<unsigned>(m_currState),
              toString(newState), static_cast<unsigned>(newState));

    //
    // See for details https://developer.chrome.com/docs/web-platform/page-lifecycle-api
    //
    // ACTIVE  - A page is in the active state if it is visible and has input focus.
    // PASSIVE - A page is in the passive state if it is visible and does not have input focus.
    // HIDDEN  - A page is in the hidden state if it is not visible (and has not been frozen, discarded, or terminated).
    // FROZEN  - In the frozen state the browser suspends execution of freezable tasks in the page's task queues until the page is unfrozen.
    // TERMINATED - A page is in the terminated state once it has started being unloaded and cleared from memory by the browser.
    //
    switch(newState)
    {
        case PageLifecycleState::ACTIVE:
        {
            if (m_currState == PageLifecycleState::INITIALIZING)
            {
                show();
            }
            focus();
            break;
        }
        case PageLifecycleState::PASSIVE:
        {
            switch (m_currState)
            {
                case PageLifecycleState::ACTIVE:
                    blur();
                    break;
                case PageLifecycleState::HIDDEN:
                case PageLifecycleState::INITIALIZING:
                    disableMemorySaving();
                    show();
                    break;
                default:
                    break;
            }
            break;
        }
        case PageLifecycleState::HIDDEN:
        {
            switch (m_currState)
            {
                case PageLifecycleState::INITIALIZING:
                    blur();
                    [[fallthrough]];
                case PageLifecycleState::PASSIVE:
                    enableMemorySaving();
                    hide();
                    break;
                case PageLifecycleState::FROZEN:
                    resume();
                    break;
                default:
                    break;
            }
            break;
        }
        case PageLifecycleState::FROZEN:
        {
            freeze();
            break;
        }
        case PageLifecycleState::TERMINATED:
        case PageLifecycleState::INITIALIZING:
            break;
    }
    m_currState = newState;
}

/*!
    \internal
    Starts a mechanism that periodically instructs WPE WebKit to release some
    memory if possible.
 */
void WpePageLifecycleDelegate::enableMemorySaving()
{
    if (m_memorySavingTimerSource)
        return;

    if (m_maxMemorySavingIterations > 0)
    {
        g_message("enabling memory saving mode");

        m_memorySavingTimerIteration = 0;
        m_memorySavingTimerSource = g_timeout_source_new(0);
        g_source_set_callback(m_memorySavingTimerSource,
                              G_SOURCE_FUNC(+[](WpePageLifecycleDelegate* self) {
                                  self->onMemorySavingTimerTimeout();
                                  return G_SOURCE_REMOVE;
                              }),
                              this,
                              nullptr);
        g_source_attach(m_memorySavingTimerSource,
                        g_main_context_get_thread_default());
    }
}

/*!
    \internal

    Stops a mechanism that periodically instructs WPE WebKit to release some
    memory if possible.

 */
void WpePageLifecycleDelegate::disableMemorySaving()
{
    if (!m_memorySavingTimerSource)
        return;

    g_message("disabling memory saving mode");

    if (destroyAndZeroTimerSource(&m_memorySavingTimerSource))
    {
        g_info("Destroyed lingering memory saving timer.");
    }
}

/*!
    \internal

    Called periodically when in memory saving mode.
    Effectively it sends memory pressure events to WPE WebKit processes to make
    them releasing some memory if possible.
 */
void WpePageLifecycleDelegate::onMemorySavingTimerTimeout()
{
    destroyAndZeroTimerSource(&m_memorySavingTimerSource);

    // if web view is not frozen then stop sending webkit_web_view_send_memory_pressure_events
    if (m_currState != PageLifecycleState::HIDDEN && m_currState != PageLifecycleState::FROZEN)
    {
        m_memorySavingTimerSource = nullptr;
        m_memorySavingTimerIteration = 0;
        g_message("stopping memory saving mode");
    }
    else
    {
        g_info("sending critical memory pressure event # %u to view", m_memorySavingTimerIteration);

        constexpr auto critical = true;
        webkit_web_view_send_memory_pressure_event(m_view, critical);

        m_memorySavingTimerIteration++;
        if (m_memorySavingTimerIteration < m_maxMemorySavingIterations)
        {
            // log2(x + 1) * 2 = <0, 2, 3.17, 4, 4.64, 5.17, 5.61, 6, 6.34, 6.64s, (...)>
            const guint intervalMilliseconds = static_cast<guint>(log2(m_memorySavingTimerIteration + 1) * 2000.0f);

            m_memorySavingTimerSource = g_timeout_source_new(intervalMilliseconds);
            g_source_set_callback(
                m_memorySavingTimerSource,
                G_SOURCE_FUNC(+[](WpePageLifecycleDelegate* self) {
                    self->onMemorySavingTimerTimeout();
                    return G_SOURCE_REMOVE;
                }),
                this,
                nullptr);
            g_source_attach(m_memorySavingTimerSource, g_main_context_get_thread_default());
        }
    }
}

class WpePageLifecycleV1 : public WpePageLifecycleDelegate
{
public:
    WpePageLifecycleV1(WebKitWebView* view, unsigned maxMemorySavingIterations)
        : WpePageLifecycleDelegate(view, maxMemorySavingIterations)
    {
    }

    ~WpePageLifecycleV1() = default;

    void show() override
    {
        g_message("plc_v1: attempting to show the page");

        // set the visibility flag
        auto* backend = webkit_web_view_backend_get_wpe_backend(webkit_web_view_get_backend(m_view));
        wpe_view_backend_add_activity_state(backend, wpe_view_activity_state_visible);

        // show view
        webkit_web_view_show(m_view);
    }

    void hide() override
    {
        g_message("plc_v1: attempting to hide the page");

        // hide view
        webkit_web_view_hide(m_view);

        // clear the focus and visibility flags
        auto* backend = webkit_web_view_backend_get_wpe_backend(webkit_web_view_get_backend(m_view));
        wpe_view_backend_remove_activity_state(backend, wpe_view_activity_state_visible |
                                               wpe_view_activity_state_focused);
    }
    void freeze() override
    {
        g_message("plc_v1: attempting to suspend page");

        auto* backend = webkit_web_view_backend_get_wpe_backend(webkit_web_view_get_backend(m_view));
        uint32_t activityState = wpe_view_backend_get_activity_state(backend);
        if ((activityState & (wpe_view_activity_state_visible | wpe_view_activity_state_focused)) != 0)
        {
            g_warning("plc_v1: attempted to freeze visible page");
            hide();
        }

        if (!webkit_web_view_is_suspended(m_view))
        {
            g_message("plc_v1: attempting to call webkit_web_view_suspend.");
            webkit_web_view_suspend(m_view);
        }
    }

    void resume() override
    {
        if (webkit_web_view_is_suspended(m_view))
        {
            g_message("plc_v1: attempting to call webkit_web_view_resume");
            webkit_web_view_resume(m_view);
        }
        else
        {
            g_message("plc_v1: attempting to call webkit_web_view_resume when webkit web view's"
                      " not suspended. Likely the app was created in a suspended state");
        }

        g_message("plc_v1: resumed");
    }

    void focus() override
    {
        g_message("plc_v1: attempting to focus the page");

        // set the focus flag
        auto* backend = webkit_web_view_backend_get_wpe_backend(webkit_web_view_get_backend(m_view));
        wpe_view_backend_add_activity_state(backend, wpe_view_activity_state_focused);
    }

    void blur() override
    {
        g_message("plc_v1: attempting to blur the page");

        // clear the focus flag
        auto* backend = webkit_web_view_backend_get_wpe_backend(webkit_web_view_get_backend(m_view));
        wpe_view_backend_remove_activity_state(backend, wpe_view_activity_state_focused);
    }

    void tryClose() override
    {
        g_message("attempting to close the view %p ", m_view);

        // make the call to try and close the page
        webkit_web_view_try_close(m_view);
    }
};

class WpePageLifecycleV2 : public WpePageLifecycleDelegate
{
    struct StateChangeTask
    {
        using CompletionHandler = std::function<void(gboolean)>;
        virtual ~StateChangeTask() = default;
        virtual const char* name() const = 0;
        virtual gboolean run(WebKitWebView* view, CompletionHandler&& handler) = 0;
    };

#define DEFINE_PAGESTATE_CHANGE(_Name, _name)                           \
    struct Async##_Name : public StateChangeTask                        \
    {                                                                   \
        const char* name() const override { return #_name; }            \
                                                                        \
        gboolean run(WebKitWebView* view, CompletionHandler&& handler) override { \
            g_message("plc_v2: attempting to " #_name " the page");     \
            return webkit_web_view_##_name##_plc(                       \
                view,                                                   \
                (GAsyncReadyCallback)(+[](WebKitWebView* view, GAsyncResult* result, CompletionHandler *handler) { \
                    GError* error = nullptr;                            \
                    gboolean ret = webkit_web_view_##_name##_plc_finish(view, result, &error); \
                    if (error || !ret)                                  \
                    {                                                   \
                        g_critical("plc_v2: " #_name " failed, error: %s", error ? error->message : "unknown"); \
                    }                                                   \
                    else                                                \
                    {                                                   \
                        g_message("plc_v2: " #_name " succeeded.");     \
                    }                                                   \
                    (*handler)(ret);                                    \
                    delete handler;                                     \
                }),                                                     \
                new CompletionHandler{ std::move(handler) });           \
        }                                                               \
    }                                                                   \

    DEFINE_PAGESTATE_CHANGE(Show, show);
    DEFINE_PAGESTATE_CHANGE(Hide, hide);
    DEFINE_PAGESTATE_CHANGE(Blur, blur);
    DEFINE_PAGESTATE_CHANGE(Focus, focus);
    DEFINE_PAGESTATE_CHANGE(Freeze, freeze);
    DEFINE_PAGESTATE_CHANGE(Resume, resume);
#undef DEFINE_PAGESTATE_CHANGE

    struct TryClose : public StateChangeTask
    {
        const char* name() const override { return "try_close"; }

        gboolean run(WebKitWebView* view, CompletionHandler&&) override {
            webkit_web_view_try_close(view);
            return FALSE;
        }
    };

    bool m_asyncStateChangeInProgress  { false };
    std::deque<std::unique_ptr<StateChangeTask>> m_stateChangeQueue;
    std::shared_ptr<char> m_token { std::make_shared<char> (42) };

    void processOnePending()
    {
        if (m_asyncStateChangeInProgress || m_stateChangeQueue.empty())
            return;

        auto& stateChange = m_stateChangeQueue.front();

        m_asyncStateChangeInProgress = stateChange->run(m_view, [this, token=std::weak_ptr<char>(m_token)](gboolean ret) {
            if (!token.lock())
                return;
            m_stateChangeQueue.pop_front();
            m_asyncStateChangeInProgress = false;
            processOnePending();
        });

        if (!m_asyncStateChangeInProgress)
        {
            m_stateChangeQueue.pop_front();
        }
    }

    void enqueueAsyncChange(std::unique_ptr<StateChangeTask> &&change)
    {
        g_message("plc_v2: enqueuing async '%s' state change", change->name());
        m_stateChangeQueue.emplace_back(std::move(change));
        processOnePending();
    }

public:
    WpePageLifecycleV2(WebKitWebView* view, unsigned maxMemorySavingIterations)
        : WpePageLifecycleDelegate(view, maxMemorySavingIterations)
    {
    }

    ~WpePageLifecycleV2() = default;

    void show() override
    {
        enqueueAsyncChange(std::make_unique<AsyncShow>());
    }

    void hide() override
    {
        enqueueAsyncChange(std::make_unique<AsyncHide>());
    }

    void focus() override
    {
        enqueueAsyncChange(std::make_unique<AsyncFocus>());
    }

    void blur() override
    {
        enqueueAsyncChange(std::make_unique<AsyncBlur>());
    }

    void freeze() override
    {
        enqueueAsyncChange(std::make_unique<AsyncFreeze>());
    }

    void resume() override
    {
        enqueueAsyncChange(std::make_unique<AsyncResume>());
    }

    void tryClose() override
    {
        enqueueAsyncChange(std::make_unique<TryClose>());
    }

};

WpeWebKitView::WpeWebKitView(const std::shared_ptr<const WpeWebKitConfig> &config,
                             WpeWebKitViewCallbacks &&callbacks)
    : m_config(config)
    , m_callbacks(std::move(callbacks))
    , m_view(nullptr)
    , m_webProcessPid(-1)
    , m_unresponsiveReplies(0)
{
    g_info("constructing the main WpeWebKitView");
}

WpeWebKitView::~WpeWebKitView()
{
    g_info("destructing the main WpeWebKitView");

#if defined(ENABLE_TESTING)
    m_testRunner.reset();
#endif

    m_pageLifecycle.reset();

    if (m_view)
    {
        // destruct the webkit view
        g_clear_object(&m_view);
    }

    g_info("destructed the main WpeWebKitView");
}

/*!
    Creates the WebKit data manager using the same directory paths as were
    used for the old rdkbrowser2 implementation.
 */
WebKitWebsiteDataManager *WpeWebKitView::createDataManager() const
{
    // the following paths are inherited from the old WPE (rdkbrowser2), we set
    // the same ones so that apps can re-use local storage and cookies from
    // the old version
    const std::string homeDir = g_get_home_dir();
    const std::string dataDir = homeDir + "/.local/share/data";
    const std::string cacheDir = g_get_user_cache_dir();

    // local storage
    const std::string localStoragePath = dataDir + "/wpe/local-storage";
    g_mkdir_with_parents(localStoragePath.c_str(), 0700);

    // disk cache
    const std::string diskCachePath = cacheDir + "/wpe/disk-cache";
    g_mkdir_with_parents(diskCachePath.c_str(), 0700);

    // webSQL
    const std::string webSQLPath = dataDir + "/wpe/databases";
    g_mkdir_with_parents(webSQLPath.c_str(), 0700);

    // indexeddb
    const std::string indexedDBPath = dataDir + "/wpe/databases/indexeddb";
    g_mkdir_with_parents(indexedDBPath.c_str(), 0700);

    // offline application cache
    const std::string offlineAppCachePath = cacheDir + "/wpe/appcache";

    // local storage quota (per page)
    const unsigned localStorageQuotaBytes = m_config->localStorageQuotaBytes();
    uint64_t perOriginStorageQuota = localStorageQuotaBytes;
    g_message("setting local storage quota to %u bytes", localStorageQuotaBytes);

    // indexed DB storage quota % use of total disk space (per page)
    const unsigned indexedDBStorageQuotaRatio = m_config->indexedDBStorageQuotaRatio();

    // overall storage quota % use of total disk space
    const unsigned totalStorageQuotaRatio = 90;

    if (m_config->enableIndexedDB())
    {
        // Currently, only use case for "per-origin-storage-quota" is for indexed DB
        // Note: uint64_t cast in calculation ensures no overflow if total disk space > 40MB
        // with 50% indexed DB storage quota ratio
        perOriginStorageQuota = (indexedDBStorageQuotaRatio * static_cast<uint64_t>(m_config->totalDiskSpaceBytes())) / 100;

        g_message("setting IndexedDB storage quota to %" PRIu64 " bytes", perOriginStorageQuota);

        if (perOriginStorageQuota < (5 * 1024 * 1024))
        {
            // WPE WebKit 2.38 allows indexed DB to grow to 5MB when indexed DB storage exceeds
            // the configured value and without an API to control that. If we don't have enough
            // disk space, this can cause issues, so put out a warning till webkit is extended
            g_warning("WARNING: IndexedDB storage quota is set to %" PRIu64 " bytes, but can grow up to 5MB. Disk space may be insufficient!",
                     perOriginStorageQuota);
        }
    }

    if (WpeWebKitUtils::webkitVersion() < VersionNumber(2, 46, 0))
    {
        return webkit_website_data_manager_new("local-storage-directory", localStoragePath.c_str(),
                                               "local-storage-quota", localStorageQuotaBytes,
                                               "disk-cache-directory", diskCachePath.c_str(),
                                               "websql-directory", webSQLPath.c_str(),
                                               "indexeddb-directory", indexedDBPath.c_str(),
                                               "offline-application-cache-directory", offlineAppCachePath.c_str(),
                                               "per-origin-storage-quota", perOriginStorageQuota,
                                               nullptr);
    }
    else
    {
        uint64_t totalDiskSpaceBytes = m_config->totalDiskSpaceBytes();
        double originStorageRatio = static_cast<double> (perOriginStorageQuota) / totalDiskSpaceBytes;
        double totalStorageRatio = static_cast<double> (totalStorageQuotaRatio) / 100.;

        return webkit_website_data_manager_new("local-storage-directory", localStoragePath.c_str(),
                                               "local-storage-quota", localStorageQuotaBytes,
                                               "disk-cache-directory", diskCachePath.c_str(),
                                               "websql-directory", webSQLPath.c_str(),
                                               "indexeddb-directory", indexedDBPath.c_str(),
                                               "offline-application-cache-directory", offlineAppCachePath.c_str(),
                                               "origin-storage-ratio", originStorageRatio,
                                               "total-storage-ratio", totalStorageRatio,
                                               "volume-capacity-override", totalDiskSpaceBytes,
                                               "base-data-directory", dataDir.c_str(),
                                               "base-cache-directory", cacheDir.c_str(),
                                               nullptr);
    }
}

bool WpeWebKitView::createView()
{
    const auto memLimits = m_config->getMemoryLimits();

    // configure Network process memory pressure handler
    const VersionNumber webKitVersion = WpeWebKitUtils::webkitVersion();
    if (webKitVersion >= VersionNumber(2, 38, 0))
    {
        unsigned long networkProcessLimitMB = memLimits.networkProcessLimitMB;
        if (networkProcessLimitMB != 0)
        {
            WebKitMemoryPressureSettings* memoryPressureSettings = webkit_memory_pressure_settings_new();
            webkit_memory_pressure_settings_set_memory_limit(memoryPressureSettings, networkProcessLimitMB);
            webkit_memory_pressure_settings_set_poll_interval(memoryPressureSettings, memLimits.networkProcessPollIntervalSec);
            webkit_website_data_manager_set_memory_pressure_settings(memoryPressureSettings);
            webkit_memory_pressure_settings_free(memoryPressureSettings);
        }
    }

    // create the data manager, which just sets the initial paths persistent storage
    WebKitWebsiteDataManager *wkDataManager = createDataManager();
    if (!wkDataManager)
    {
        g_critical("failed to create the webkit data manager");
        return false;
    }

    // create the main context
    WebKitWebContext *wkContext = nullptr;
    if (webKitVersion >= VersionNumber(2, 38, 0))
    {
        unsigned long webProcessLimitMB = memLimits.webProcessLimitMB;
        if (webProcessLimitMB != 0)
        {
            WebKitMemoryPressureSettings* memoryPressureSettings = webkit_memory_pressure_settings_new();
            webkit_memory_pressure_settings_set_memory_limit(memoryPressureSettings, webProcessLimitMB);
            webkit_memory_pressure_settings_set_poll_interval(memoryPressureSettings, memLimits.pollIntervalSec);

            unsigned long serviceWorkerWebProcessLimitMB = memLimits.serviceWorkerWebProcessLimitMB;
            if (serviceWorkerWebProcessLimitMB != 0)
            {
                WebKitMemoryPressureSettings* serviceWorkerMemoryPressureSettings = webkit_memory_pressure_settings_new();
                webkit_memory_pressure_settings_set_memory_limit(serviceWorkerMemoryPressureSettings, serviceWorkerWebProcessLimitMB);
                webkit_memory_pressure_settings_set_poll_interval(serviceWorkerMemoryPressureSettings, memLimits.pollIntervalSec);

                // pass web process memory pressure settings to WebKitWebContext constructor
                wkContext = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT,
                                                            "website-data-manager", wkDataManager,
                                                            "memory-pressure-settings", memoryPressureSettings,
                                                            "service-worker-memory-pressure-settings", serviceWorkerMemoryPressureSettings,
                                                            nullptr));
                webkit_memory_pressure_settings_free(serviceWorkerMemoryPressureSettings);
            }
            else
            {
                // pass web process memory pressure settings to WebKitWebContext constructor
                wkContext = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT,
                                                            "website-data-manager", wkDataManager,
                                                            "memory-pressure-settings", memoryPressureSettings,
                                                            nullptr));
            }
            webkit_memory_pressure_settings_free(memoryPressureSettings);
        }
    }

    if (!wkContext)
    {
        wkContext = WEBKIT_WEB_CONTEXT(g_object_new(WEBKIT_TYPE_WEB_CONTEXT, "website-data-manager", wkDataManager, nullptr));
    }

    g_object_unref(wkDataManager);


    // set up injected bundle. Will be loaded once WPEWebProcess is started.
    g_signal_connect(wkContext, "initialize-web-extensions", G_CALLBACK(initWebExtensionsCallback), this);


    // setup cookies paths and accept policy
    const std::string cacheDir = g_get_user_cache_dir();
    const std::string cookiesStoragePath = cacheDir + "/cookies.db";
    auto *cookieManager = webkit_web_context_get_cookie_manager(wkContext);
    webkit_cookie_manager_set_persistent_storage(cookieManager,
                                                 cookiesStoragePath.c_str(),
                                                 WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);
    webkit_cookie_manager_set_accept_policy(cookieManager,
                                            m_config->cookieAcceptPolicy());


    // set the preferred language
    if (auto language = m_config->navigatorLanguage(); !language.empty())
    {
        gchar** languages = g_new0(gchar*, 2);
        languages[0] = g_strdup(m_config->navigatorLanguage().c_str());

        webkit_web_context_set_preferred_languages(wkContext, languages);
        g_strfreev(languages);
    }

    // pretend http:// schema is "secure" iff web security is disabled
    if (m_config->disableWebSecurity())
    {
        if ((webKitVersion >= VersionNumber(2, 38, 0)))
        {
            WebKitSecurityManager *securityManager = webkit_web_context_get_security_manager(wkContext);
            webkit_security_manager_register_uri_scheme_as_secure(securityManager, "http");

            // ignore TLS error iff web security is disabled
            webkit_website_data_manager_set_tls_errors_policy(
                webkit_web_context_get_website_data_manager(wkContext),
                WEBKIT_TLS_ERRORS_POLICY_IGNORE);
        }
    }

    // generate the settings from the config
    WebKitSettings *wkSettings = m_config->webKitSettings();

    g_message("creating the webkit view (WPEWebKit " VERSION_FORMAT ")", VERSION_ARGS(webKitVersion));

    m_view = WEBKIT_WEB_VIEW(g_object_new(WEBKIT_TYPE_WEB_VIEW,
                                          "backend", webkit_web_view_backend_new(wpe_view_backend_create(), nullptr, nullptr),
                                          "web-context", wkContext,
                                          "settings", wkSettings,
                                          "is-controlled-by-automation", FALSE, // TODO:
                                          nullptr));

    gboolean enablePLCv2 = FALSE;
    if (g_object_class_find_property(G_OBJECT_GET_CLASS(wkSettings), "enable-page-lifecycle"))
    {
        g_object_get(G_OBJECT(wkSettings), "enable-page-lifecycle", &enablePLCv2, nullptr);
    }
    if (enablePLCv2)
    {
        g_message("Using Page Lifecycle V2");
        m_pageLifecycle = std::make_unique<WpePageLifecycleV2>(
            m_view, m_config->maxMemorySavingIterations());
    }
    else
    {
        g_message("Using Page Lifecycle V1");
        m_pageLifecycle = std::make_unique<WpePageLifecycleV1>(
            m_view, m_config->maxMemorySavingIterations());
    }

    g_object_unref(wkContext);
    g_object_unref(wkSettings);

    g_message("created the webkit view %p ", m_view);

    // configure user scripts
    configureUserContent(m_view);

    // always start with transparent background (maybe look at this in the
    // future, so only apps that chain AS Player have a transparent background)
    {
        WebKitColor transparent = { 0.0f, 0.0f, 0.0f, 0.0f };
        webkit_web_view_set_background_color(m_view, &transparent);
    }

    if (webKitVersion >= VersionNumber(2, 46, 0))
    {
        // Ensure package index.html will have access to local resources if file URLs cross
        // access is disabled
        const gchar *list[] = { "file://" DEFAULT_LOCAL_FILE_DIR "/index.html", nullptr };
        webkit_web_view_set_local_universal_access_allowlist(m_view, list);
    }

    // add signal various signal listeners on the main view
    g_signal_connect(m_view, "notify::uri", G_CALLBACK(uriChangedCallback), this);
    g_signal_connect(m_view, "load-changed", G_CALLBACK(loadChangedCallback), this);
    g_signal_connect(m_view, "load-failed", G_CALLBACK(loadFailedCallback), this);
    g_signal_connect(m_view, "web-process-terminated", G_CALLBACK(webProcessTerminatedCallback), this);
    g_signal_connect(m_view, "close", G_CALLBACK(closeCallback), this);
    g_signal_connect(m_view, "permission-request", G_CALLBACK(permissionRequestCallback), nullptr);
    g_signal_connect(m_view, "show-notification", G_CALLBACK(showNotificationCallback), this);
    g_signal_connect(m_view, "user-message-received", G_CALLBACK(userMessageReceivedCallback), this);
    g_signal_connect(m_view, "notify::is-web-process-responsive", G_CALLBACK(isWebProcessResponsiveCallback), this);
    g_signal_connect(m_view, "authenticate", G_CALLBACK(authenticationCallback), this);
    g_signal_connect(m_view, "decide-policy", G_CALLBACK(decidePolicyCallback), this);

    if (!enablePLCv2)
    {
        // Sync up backend and web_view state.
        auto* backend = webkit_web_view_backend_get_wpe_backend(webkit_web_view_get_backend(m_view));
        uint32_t initialFlags =
            wpe_view_activity_state_in_window | wpe_view_activity_state_visible | wpe_view_activity_state_focused;
        wpe_view_backend_add_activity_state(backend, initialFlags);
    }

    setState(PageLifecycleState::HIDDEN);

    return true;
}

/*!
    \internal

    Configures both the user script(s) and style(s)

 */
void WpeWebKitView::configureUserContent(WebKitWebView* view)
{
    g_message("attempting to add user scripts / style sheets");

    // get the user content manager object
    auto* userContentManager = webkit_web_view_get_user_content_manager(view);
    if (!userContentManager)
    {
        g_warning("failed to get user content manager object");
        return;
    }

    // get all the user scripts, there is typically at least one that is used
    // to set some globals in the DOM
    const std::vector<std::string>& scripts = m_config->userScripts();
    for (const std::string &script : scripts)
    {
        g_info("adding userscript to WPEWebKit instance");

        auto* wkScript = webkit_user_script_new(script.c_str(),
                                                WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                                nullptr,
                                                nullptr);

        webkit_user_content_manager_add_script(userContentManager, wkScript);
        webkit_user_script_unref(wkScript);
    }

    const std::vector<std::string> &stylesheets = m_config->userStyleSheets();
    for (const std::string &stylesheet : stylesheets)
    {
        g_info("adding user stylesheet to WPEWebKit instance");

        auto* wkStylesheet = webkit_user_style_sheet_new(stylesheet.c_str(),
                                                         WEBKIT_USER_CONTENT_INJECT_ALL_FRAMES,
                                                         WEBKIT_USER_STYLE_LEVEL_USER,
                                                         nullptr,
                                                         nullptr);

        webkit_user_content_manager_add_style_sheet(userContentManager, wkStylesheet);
        webkit_user_style_sheet_unref(wkStylesheet);
    }
}

/*!
    Attempts to set the URL to load into the web page.
 */
bool WpeWebKitView::loadUrl(const std::string &url)
{
    // sanity check we have a WPE view
    if (!m_view)
    {
        g_warning("unexpectedly we don't have a valid WPE view object");
        return false;
    }

    g_message("attempting to navigate to '%s'", url.c_str());

    // make the call to set the url
    webkit_web_view_load_uri(m_view, url.c_str());
    return true;
}

/*!
    Attempts to change the lifecycle state of the web page, transitioning through intermittent states if needed.
      https://developer.chrome.com/docs/web-platform/page-lifecycle-api
      https://wiki.rdkcentral.com/display/RDK/App+Lifecycle+2.0
 */
bool WpeWebKitView::setState(PageLifecycleState newState)
{
    g_return_val_if_fail (m_view != nullptr, false);
    g_return_val_if_fail (m_pageLifecycle.get() != nullptr, false);

    PageLifecycleState currState = m_pageLifecycle->currentState();

    if (currState == newState)
    {
        g_info("setState: ignore new state, browser is already in requested state");
        return true;
    }

    if (newState == PageLifecycleState::INITIALIZING)
    {
        g_critical("setState: ignore incorrect state transition %s(0x%x) -> %s(0x%x)",
                   toString(currState), static_cast<unsigned>(currState),
                   toString(newState), static_cast<unsigned>(newState));
        return false;
    }

    if (currState == PageLifecycleState::TERMINATED)
    {
        g_critical("ignore incorrect state transition %s(0x%x) -> %s(0x%x)",
                   toString(currState), static_cast<unsigned>(currState),
                   toString(newState), static_cast<unsigned>(newState));
        return false;
    }

    do
    {
        m_pageLifecycle->changeState(nextState(currState, newState));
    }
    while((currState = m_pageLifecycle->currentState()) != newState);

    return true;
}

/*!
    Attempts to close the page / view.

    This will fire the onbeforeunload event in the browser before closing the
    page.
 */
bool WpeWebKitView::tryClose()
{
    // sanity check we have a WPE view
    if (!m_view)
    {
        g_warning("unexpectedly we don't have a valid WPE view object");
        return false;
    }

    m_pageLifecycle->tryClose();

    return true;
}

/*!
    Checks if the main web process is still responding.
 */
bool WpeWebKitView::checkResponsive()
{
    if (!m_view)
    {
        g_warning("unexpectedly we don't have a valid WPE view object");
        return true;
    }

    bool isResponsive = (webkit_web_view_get_is_web_process_responsive(m_view) == TRUE);

    if (!isResponsive || (m_unresponsiveReplies > 0))
    {
        const char* uriPtr = webkit_web_view_get_uri(m_view);
        const std::string activeURL = uriPtr ? uriPtr : "";

        if (isResponsive)
        {
            g_critical("WebProcess recovered after %d unresponsive replies, url=%s",
                      m_unresponsiveReplies, activeURL.c_str());

            m_unresponsiveReplies = 0;
        }
        else
        {
            m_unresponsiveReplies++;

            g_critical("WebProcess is unresponsive, reply num=%d, url=%s",
                      m_unresponsiveReplies, activeURL.c_str());
        }
    }

    return isResponsive;
}

/*!
    Returns the pid of the main WPEWebProcess running for the view.  If the
    view isn't create or has been destroyed then this returns -1.

 */
pid_t WpeWebKitView::getWebProcessIdentifier() const
{
    if (!m_view)
    {
        g_warning("unexpectedly we don't have a valid WPE view object");
        return -1;
    }

    if (m_webProcessPid < 1)
    {
        m_webProcessPid = webkit_web_view_get_web_process_identifier(m_view);
    }

    return m_webProcessPid;
}

/*!
    Pass HDR settings to the WebKitSettings object.
 */
void WpeWebKitView::setScreenSupportsHDR(bool enable)
{
    if (!m_view)
    {
        g_warning("unexpectedly we don't have a valid WPE view object");
        return;
    }

    WebKitSettings *settings = webkit_web_view_get_settings(m_view);

    auto* klass = G_OBJECT_GET_CLASS(settings);
    if (!g_object_class_find_property(klass, "screen-supports-hdr"))
    {
        g_warning("WPEWebKit doesn't support 'screen-supports-hdr' setting.");
        return;
    }

    gboolean wasEnabled = FALSE;
    g_object_get(settings, "screen-supports-hdr", &wasEnabled, nullptr);
    if (wasEnabled != enable)
    {
        g_message("WebKitSetting screen supports HDR set to %d", enable);
        g_object_set(G_OBJECT(settings), "screen-supports-hdr",
                     enable ? TRUE : FALSE, nullptr);
    }
}

/*!
    Runs the javascript code in \a js within the context of the currently
    running page.

 */
bool WpeWebKitView::runJavaScript(const std::string &js)
{
    // sanity check we have a WPE view
    if (!m_view)
    {
        g_warning("unexpectedly we don't have a valid WPE view object");
        return false;
    }

    g_message("attempting to execute JS code");

    // try and execute the JS code
    if (WpeWebKitUtils::webkitVersion() < VersionNumber(2, 46, 0))
    {
        G_GNUC_BEGIN_IGNORE_DEPRECATIONS
        webkit_web_view_run_javascript(m_view, js.c_str(), nullptr, nullptr, nullptr);
        G_GNUC_END_IGNORE_DEPRECATIONS
    }
    else
    {
        webkit_web_view_evaluate_javascript(m_view, js.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
    }

    return true;
}

/*!
    \internal
    \static

    Called when WebKit is started and loading extensions.  This is where we
    supply the path to our extensions.

 */
void WpeWebKitView::initWebExtensionsCallback(WebKitWebContext *context,
                                              void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);

    const std::string path = self->m_config->extensionsDirectory();

    g_message("initialising extensions directory to %s", path.c_str());

    // set our local extension directory from the widget
    webkit_web_context_set_web_extensions_directory(context, path.c_str());

    // generate config for the extensions, this is passed to all the extensions
    // so is an amalgamation of settings for the console.log, etc, extensions

    auto common   = self->m_config->commonExtensionSettings();
    auto webRuntime = self->m_config->webRuntimeExtensionSettings();

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    g_variant_builder_add(&builder, "{sv}", "common",   common.release());
    g_variant_builder_add(&builder, "{sv}", "webruntime", webRuntime.release());

    // set the user data for (all) the extensions
    GVariant *data = g_variant_builder_end(&builder);
    webkit_web_context_set_web_extensions_initialization_user_data(context, data);
}

/*!
    \internal
    \static

 */
void WpeWebKitView::uriChangedCallback(WebKitWebView *webView, GParamSpec*,
                                       void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    const char* url = webkit_web_view_get_uri(webView);
    g_message("wpe url changed to '%s'", url);
}

/*!
    \internal
    \static

 */
void WpeWebKitView::loadChangedCallback(WebKitWebView *webView,
                                        WebKitLoadEvent loadEvent,
                                        void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    const gchar *url = webkit_web_view_get_uri(webView);

    switch (loadEvent)
    {
        case WEBKIT_LOAD_STARTED:
            g_message("wpe load started '%s'", url);
            break;
        case WEBKIT_LOAD_REDIRECTED:
            g_message("wpe load redirected to '%s'", url);
            break;
        case WEBKIT_LOAD_COMMITTED:
            g_message("wpe load committed to '%s'", url);
            break;
        case WEBKIT_LOAD_FINISHED:
            g_message("wpe load finished '%s'", url);
            break;
    }
}

/*!
    \internal
    \static

    Called when failed to load a webpage in the page.  This is not called for
    iframes or other content.  Also this doesn't capture HTTP errors like 404.

 */
gboolean WpeWebKitView::loadFailedCallback(WebKitWebView *webView,
                                           WebKitLoadEvent loadEvent,
                                           gchar *failingUri,
                                           GError *error,
                                           void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    g_critical("failed to load page '%s' due to - %s",
              failingUri, error ? error->message : "???");

    // check the policy settings
    switch (self->m_config->loadFailurePolicy())
    {
        case LoadFailurePolicy::Ignore:
            // do nothing, we've logged it above
            break;

        case LoadFailurePolicy::Display:
            {
                const std::string errorPage = self->m_config->loadFailureErrorPage();
                webkit_web_view_load_alternate_html(webView, errorPage.c_str(), failingUri, nullptr);
            }
            return TRUE;

        case LoadFailurePolicy::Terminate:
            {
                g_critical("page load failed so closing the view");
                webkit_web_view_try_close(webView);
            }
            return TRUE;
    }

    // allow this event to propagate further
    return FALSE;
}

/*!
    \internal
    \static

 */
void WpeWebKitView::closeCallback(WebKitWebView *webView, void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    g_message("closed parent view %p callback called", webView);

    self->m_callbacks.close(CloseReason::UNLOAD);
}

/*!
    \internal
    \static

    Called when client to decide about a permission request, such as allowing
    the browser to switch to fullscreen mode, sharing its location or similar
    operations.

    \see https://webkitgtk.org/reference/webkit2gtk/2.5.1/WebKitWebView.html#WebKitWebView-permission-request
 */
gboolean WpeWebKitView::permissionRequestCallback(WebKitWebView *webView,
                                                  WebKitPermissionRequest *permissionRequest,
                                                  void *userData)
{
    // allowing all matches the thunder browser plugin, but wonder if we should
    // log this and block certain things ?
    webkit_permission_request_allow(permissionRequest);

    return TRUE;
}

/*!
    \internal
    \static

    Called when JS raises an alert, we just log it but don't allow it to show
    on screen.

 */
gboolean WpeWebKitView::showNotificationCallback(WebKitWebView *webView,
                                                 WebKitNotification* notification,
                                                 void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    g_message("HTML5Notification: %s - %s",
          webkit_notification_get_title(notification),
          webkit_notification_get_body(notification));

    return FALSE;
}

/*!
    \internal
    \static

 */
void WpeWebKitView::webProcessTerminatedCallback(WebKitWebView *,
                                                 WebKitWebProcessTerminationReason reason,
                                                 void *userData)
{
    auto self = reinterpret_cast<WpeWebKitView*>(userData);

    switch (reason)
    {
        case WEBKIT_WEB_PROCESS_CRASHED:
            g_warning("CRASH: WebProcess crashed: exiting ...");
            break;
        case WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT:
            g_warning("CRASH: WebProcess terminated due to memory limit: exiting ...");
            break;
        case WEBKIT_WEB_PROCESS_TERMINATED_BY_API:
            g_warning("CRASH: WebProcess terminated by API");
            break;

        default:
            g_warning("CRASH: WebProcess terminated for unknown reason");
            break;
    }

    self->m_callbacks.processTerminated();
}


/*!
    \internal
    \static

    Notification callback, typically indicates the web process is now responsive
    again.

 */
void WpeWebKitView::isWebProcessResponsiveCallback(WebKitWebView *webView,
                                                   GParamSpec *paramSpec,
                                                   void *userData)
{
    g_info("received 'notify::is-web-process-responsive' callback");

    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    if (self->m_webProcessPid < 1)
    {
        self->m_webProcessPid = webkit_web_view_get_web_process_identifier(webView);
    }

    if (webkit_web_view_get_is_web_process_responsive(webView) == TRUE)
    {
        if (self->m_unresponsiveReplies > 0)
        {
            g_message("WebProcess recovered after %d unresponsive replies, url=%s",
                  self->m_unresponsiveReplies, webkit_web_view_get_uri(webView));

            self->m_unresponsiveReplies = 0;
        }

        self->m_callbacks.notifyResponsive();
    }
    else
    {
        g_warning("WebProcess is currently unresponsive");
    }
}

/*!
    \internal
    \static

    Called when an extension calls webkit_web_page_send_message_to_view().

 */
gboolean WpeWebKitView::userMessageReceivedCallback(WebKitWebView *webView,
                                                    WebKitUserMessage *message,
                                                    void *userData)
{
    g_info("received 'user-message' callback");

    auto self = reinterpret_cast<WpeWebKitView*>(userData);
    g_assert(self && (self->m_view == webView));

    const char* name = webkit_user_message_get_name(message);
    g_message("user message name='%s'", name);
    if (strcmp(name, "WebRuntime.LoadUrl") == 0)
    {
        return self->onWebRuntimeLoadUrl(message);
    }
#if defined(ENABLE_TESTING)
    else if (self->m_config->enableTesting() && g_str_has_prefix(name, Testing::Tags::TestRunnerPrefix))
    {
        if (!self->m_testRunner)
        {
            g_info("Create new TestRunner instance");
            self->m_testRunner =
                Testing::TestRunner::Create(webView, self->m_config->extensionsDirectory());
        }
        if (self->m_testRunner)
            self->m_testRunner->handleUserMessage(message);
        return TRUE;
    }
#endif
    else if (strcmp(name, "Window.minimize") == 0)
    {
        self->m_callbacks.close(CloseReason::DEACTIVATE);
        return TRUE;
    }
    else
    {
        g_warning("received unknown user message '%s'", name);
        return FALSE;
    }
}

/*!
    \internal
    \static

    Called when the user is challenged with HTTP authentication.
 */
gboolean WpeWebKitView::authenticationCallback(WebKitWebView* webView,
                                               WebKitAuthenticationRequest* request,
                                               void *userData)
{
    webkit_authentication_request_authenticate(request, nullptr);
    return TRUE;
}

gboolean WpeWebKitView::decidePolicyCallback(WebKitWebView* webView,
                                             WebKitPolicyDecision* decision,
                                             WebKitPolicyDecisionType type,
                                             void *userData)
{
    if (type == WEBKIT_POLICY_DECISION_TYPE_RESPONSE)
    {
        auto *response = webkit_response_policy_decision_get_response(WEBKIT_RESPONSE_POLICY_DECISION(decision));
        if (webkit_uri_response_is_main_frame(response))
        {
            g_message("wpe web main frame response status=%d, '%s'",
                      webkit_uri_response_get_status_code(response),
                      webkit_uri_response_get_uri(response));
        }
    }
    webkit_policy_decision_use(decision);
    return TRUE;
}

/*!
    \internal

    Called when a user-message is received from the WebRuntime extension, telling us
    that an app has requested that we load a new URL with supplied optional
    custom headers and options.

 */
gboolean WpeWebKitView::onWebRuntimeLoadUrl(WebKitUserMessage *message)
{
    // get the url string and headers
    GVariant *payload = webkit_user_message_get_parameters(message);
    if (!payload)
    {
        g_warning("failed to get the user-message payload");
        return FALSE;
    }

#if GLIB_CHECK_VERSION(2,72,0)
    if (g_log_get_debug_enabled())
    {
        gchar *str = g_variant_print(payload, TRUE);
        g_debug("received WebRuntime.LoadUrl request - %s", str);
        g_free(str);
    }
#endif

    // the variant is a tuple of variants, the first is a string for the url and
    // the second is a map of custom options
    // Custom headers are not part of the message. They are applied directly by the
    // WebRuntime extension.
    const char *url = nullptr;
    GVariant *options = nullptr;
    g_variant_get(payload, "(&s@a{sv})", &url, &options);
    if (!url || !options)
    {
        g_warning("failed to get the request args from the payload");
        return FALSE;
    }

    // create a new web request for the page navigation
    WebKitURIRequest *request = webkit_uri_request_new(url);

    // check the options to see if should be changing the user agent at the
    // same time
    const gchar* userAgent = nullptr;
    if (g_variant_lookup(options, "userAgent", "&s", &userAgent))
        g_info("userAgent = %s", userAgent);

    // simple object to store the view and request object
    struct LoadUrlRequest
    {
        WebKitWebView *view;
        WebKitURIRequest *request;
        std::string userAgent;
    } *loadRequest = new LoadUrlRequest{ m_view, request, std::string(userAgent ? userAgent : "") };

    g_variant_unref(options);

    // make the call to set the url the next time through the event loop
    GSource *source = g_idle_source_new();
    g_source_set_callback(
        source,
        [](gpointer userData) -> gboolean {
            auto *request = reinterpret_cast<LoadUrlRequest*>(userData);

            // if we have a new user agent then set that before the request
            if (!request->userAgent.empty())
            {
                WebKitSettings *settings = webkit_web_view_get_settings(request->view);
                if (!settings)
                {
                    g_warning("failed to get the settings for current webview");
                }
                else
                {
                    g_message("changing the user agent to '%s'", request->userAgent.c_str());
                    webkit_settings_set_user_agent(settings, request->userAgent.c_str());
                }
            }

            g_message("attempting to navigate to '%s'",
                  webkit_uri_request_get_uri(request->request));

            // now make the load request
            webkit_web_view_load_request(request->view, request->request);

            return G_SOURCE_REMOVE;
        },
        loadRequest,
        [](gpointer userData) -> void {
            auto *request = reinterpret_cast<LoadUrlRequest*>(userData);
            g_object_unref(request->request);
            delete request;
        });

    g_source_attach(source, g_main_context_get_thread_default());
    g_source_unref(source);

    return TRUE;
}
