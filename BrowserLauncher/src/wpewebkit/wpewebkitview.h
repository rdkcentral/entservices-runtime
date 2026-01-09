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

#include <wpe/webkit.h>
#include <sys/types.h>

#include <functional>
#include <memory>

#if defined(ENABLE_TESTING)
namespace Testing {
class TestRunner;
}
#endif

class WpeWebKitConfig;

struct WpeWebKitViewCallbacks
{
    // Called when Web Page invokes `window.close()`, `window.minimize()` or after `tryClose()`.
    std::function<void(CloseReason)> close;
    // Called when Web Process has been terminated.
    std::function<void()> processTerminated;
    // Called when Web Process has become responsive again.
    std::function<void()> notifyResponsive;
};

class WpePageLifecycleDelegate;

class WpeWebKitView
{
public:
    WpeWebKitView(const std::shared_ptr<const WpeWebKitConfig> &config,
                  WpeWebKitViewCallbacks &&callbacks);
    ~WpeWebKitView();

    bool createView();
    bool loadUrl(const std::string &url);
    bool setState(PageLifecycleState state);
    bool tryClose();
    bool checkResponsive();
    pid_t getWebProcessIdentifier() const;
    bool runJavaScript(const std::string &js);
    void setScreenSupportsHDR(bool enable);

private:
    WebKitWebsiteDataManager *createDataManager() const;

    void configureUserContent(WebKitWebView *view);

    static void uriChangedCallback(WebKitWebView *webView, GParamSpec*,
                                   void *userData);
    static void loadChangedCallback(WebKitWebView *webView,
                                    WebKitLoadEvent loadEvent,
                                    void *userData);
    static gboolean loadFailedCallback(WebKitWebView *webView,
                                       WebKitLoadEvent loadEvent,
                                       gchar *failingUri,
                                       GError *error,
                                       void *userData);
    static void webProcessTerminatedCallback(WebKitWebView *webView,
                                             WebKitWebProcessTerminationReason reason,
                                             void *userData);
    static gboolean showNotificationCallback(WebKitWebView *webView,
                                             WebKitNotification* notification,
                                             void *userData);
    static void closeCallback(WebKitWebView *webView, void *userData);

    static gboolean permissionRequestCallback(WebKitWebView *webView,
                                              WebKitPermissionRequest *permissionRequest,
                                              void *userData);

    static gboolean authenticationCallback(WebKitWebView *webView,
                                           WebKitAuthenticationRequest *request,
                                           void *userData);

    static gboolean decidePolicyCallback(WebKitWebView* webView,
                                         WebKitPolicyDecision* decision,
                                         WebKitPolicyDecisionType type,
                                         void *userData);


    static void initWebExtensionsCallback(WebKitWebContext *context,
                                          void *userData);

    static void fetchedCachedWebsiteDataCallback(GObject *dataManager,
                                                 GAsyncResult *result,
                                                 void *userData);
    static void removedCachedWebsiteDataCallback(GObject *dataManager,
                                                 GAsyncResult *result,
                                                 void *userData);

    static void isWebProcessResponsiveCallback(WebKitWebView *webView,
                                               GParamSpec *paramSpec,
                                               void *userData);

    static gboolean userMessageReceivedCallback(WebKitWebView *webView,
                                                WebKitUserMessage *message,
                                                void *userData);

    gboolean onWebRuntimeLoadUrl(WebKitUserMessage *message);


    void onMemorySavingTimerTimeout();

private:
    const std::shared_ptr<const WpeWebKitConfig> m_config;
    WpeWebKitViewCallbacks m_callbacks;

    WebKitWebView* m_view;
    mutable pid_t m_webProcessPid;

    int m_unresponsiveReplies;

    std::unique_ptr<WpePageLifecycleDelegate> m_pageLifecycle;

#if defined(ENABLE_TESTING)
    std::unique_ptr<Testing::TestRunner> m_testRunner;
#endif
};
