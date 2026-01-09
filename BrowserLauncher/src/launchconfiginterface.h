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

#include <vector>
#include <string>
#include <map>
#include <filesystem>

using LocalFilePath = std::filesystem::path;
using ProcessEnvironment = std::map<std::string, std::string>;
enum class LoadFailurePolicy
{
    Ignore,
    Display,
    Terminate,
};

class LaunchConfigInterface
{
public:
    virtual ~LaunchConfigInterface() = default;

    virtual std::string runtimeDir() const = 0;
    virtual std::string navigatorLanguage() const = 0;
    virtual std::string locale() const = 0;
    virtual ProcessEnvironment browserEnvs() const = 0;
    virtual unsigned totalDiskSpaceBytes() const = 0;
    virtual std::string fireboltEndpoint() const = 0;
    virtual std::string cookieAcceptPolicy() const = 0;
    virtual bool enableConsoleLog() const = 0;
    virtual bool allowMixedContent() const = 0;
    virtual bool allowFileURLsCrossAccess() const = 0;
    virtual bool enableLocalStorage() const = 0;
    virtual bool enableNonCompositedWebGL() const = 0;
    virtual bool enableMediaStream() const = 0;
    virtual bool enableWebAudio() const = 0;
    virtual std::string customUserAgent() const = 0;
    virtual bool disableWebSecurity() const = 0;
    virtual bool enableSpatialNavigation() const = 0;
    virtual std::vector<LocalFilePath> userScripts() const = 0;
    virtual std::vector<LocalFilePath> userStyleSheets() const = 0;
    virtual std::vector<LocalFilePath> browserExtensions() const = 0;
    virtual int maxUnresponsiveTimeMs() const = 0;
    virtual bool isHeadless() const = 0;
    virtual bool enableGpuMemLimiting() const = 0;
    virtual std::string customUserAgentBase() const = 0;
    virtual LoadFailurePolicy loadFailurePolicy() const = 0;
    virtual LocalFilePath loadFailureErrorPage() const = 0;
    virtual bool enableTesting() const = 0;
    virtual int localStorageQuotaBytes() const = 0;
    virtual bool enableServiceWorker() const = 0;
    virtual bool enableIndexedDB() const = 0;
    virtual int indexedDBStorageQuotaRatio() const = 0;
    virtual int maxMemorySavingIterations() const = 0;
    virtual bool enableWebRuntimeLoad() const = 0;
    virtual bool enableLifecycle2() const = 0;
};
