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

#include <vector>
#include <memory>

struct GVariantDeleter
{
  void operator()(GVariant* v) { if ( v ) g_variant_unref(v); }
};
using GVariantRef = std::unique_ptr<GVariant, GVariantDeleter>;

class WpeWebKitConfig
{
    WpeWebKitConfig() = delete;
    WpeWebKitConfig(const WpeWebKitConfig &rhs) = delete;
    WpeWebKitConfig& operator=(const WpeWebKitConfig &rhs) = delete;

public:
    explicit WpeWebKitConfig(const std::shared_ptr<const LaunchConfigInterface> &launchConfig);
    ~WpeWebKitConfig();

    void setEnvironment() const;

    std::vector<std::string> userScripts() const;
    std::vector<std::string> userStyleSheets() const;

    WebKitSettings* webKitSettings() const;
    WebKitCookieAcceptPolicy cookieAcceptPolicy() const;

    std::string extensionsDirectory() const;
    GVariantRef commonExtensionSettings() const;
    GVariantRef webRuntimeExtensionSettings() const;

    inline LoadFailurePolicy loadFailurePolicy() const
    {
        return m_launchConfig->loadFailurePolicy();
    }

    std::string loadFailureErrorPage() const;

    struct MemoryLimits
    {
        unsigned long networkProcessLimitMB = 0;
        unsigned long webProcessLimitMB = 0;
        unsigned long serviceWorkerWebProcessLimitMB = 0;
        double networkProcessPollIntervalSec = 1.0;
        double pollIntervalSec = 1.0;
    };

    inline MemoryLimits getMemoryLimits() const
    {
        return m_memLimits;
    }

    inline std::string navigatorLanguage() const
    {
        return m_launchConfig->navigatorLanguage();
    }

    inline int localStorageQuotaBytes() const
    {
        return m_launchConfig->localStorageQuotaBytes();
    }

    inline int indexedDBStorageQuotaRatio() const
    {
        return m_launchConfig->indexedDBStorageQuotaRatio();
    }

    inline bool disableWebSecurity() const
    {
        return m_launchConfig->disableWebSecurity();
    }

    inline bool enableServiceWorker() const
    {
        return m_launchConfig->enableServiceWorker();
    }

    inline bool enableIndexedDB() const
    {
        return m_launchConfig->enableIndexedDB();
    }

    inline bool enableTesting() const
    {
        return m_launchConfig->enableTesting();
    }

    inline unsigned totalDiskSpaceBytes() const
    {
        return m_launchConfig->totalDiskSpaceBytes();
    }

    inline uint maxMemorySavingIterations() const
    {
        return m_launchConfig->maxMemorySavingIterations();
    }

    inline bool enableWebRuntimeLoad() const
    {
        return m_launchConfig->enableWebRuntimeLoad();
    }

private:
    static std::string escapeJavascriptString(const std::string &str);

    bool initExtensionDir();

    std::string userAgent(const std::string &existing) const;

    static uint getCpusAllowed();

    static void setGStreamerEnvironment();

    static bool setRialtoEnvironment();

private:
    std::shared_ptr<const LaunchConfigInterface> m_launchConfig;

    MemoryLimits m_memLimits { };
    std::string m_extTmpDirectory;
};
