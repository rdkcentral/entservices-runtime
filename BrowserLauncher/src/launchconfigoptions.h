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

//
// configuration options set in rdk.config
//
#define FOR_EACH_RDK_CONFIG_OPTION(macro)                               \
    macro(std::string, cookieAcceptPolicy, {"no-third-party"}, "Change cookie accept policy. Possible values: 'always', 'never', 'no-third-party'.") \
    macro(bool, allowMixedContent, {true}, "Allow running and displaying of insecure content.") \
    macro(bool, allowFileURLsCrossAccess, {true},"Allow file access from file urls. ") \
    macro(bool, enableConsoleLog, {true}, "Enable console.log.")    \
    macro(bool, enableLocalStorage, {true}, "Enable W3C local storage.") \
    macro(bool, enableNonCompositedWebGL, {false}, "Enable non-composited WebGL.") \
    macro(bool, enableMediaStream, {false}, "Enable WebRTC support.") \
    macro(bool, enableWebAudio, {false}, "Enable WebAudio support.")   \
    macro(bool, disableWebSecurity, {false}, "Disable Web security.")   \
    macro(bool, enableSpatialNavigation, {false}, "Enable Spatial Navigation.") \
    macro(std::vector<LocalFilePath>, userScripts, {}, "User scripts to inject into the browser.") \
    macro(std::vector<LocalFilePath>, userStyleSheets, {}, "User styles to inject into the browser.") \
    macro(int, maxUnresponsiveTimeMs, {60*1000}, "Browser watchdog timeout.") \
    macro(bool, isHeadless, {false}, "Enable 'headless' mode.")          \
    macro(bool, enableGpuMemLimiting, {true}, "Enable GPU memory monitoring.") \
    macro(std::string, customUserAgent, {}, "Override browser user agent.") \
    macro(std::string, customUserAgentBase, {}, "Override base of browser user agent.") \
    macro(LoadFailurePolicy, loadFailurePolicy, {LoadFailurePolicy::Terminate}, "Specify how to handle page load failure. Possible values: 'ignore', 'display', 'terminate'.") \
    macro(LocalFilePath, loadFailureErrorPage, {}, "Path to page to show on page load failure.") \
    macro(std::vector<LocalFilePath>, browserExtensions, {}, "List of additional extentions to load.") \
    macro(bool, enableTesting, {false}, "Enable test framework support.") \
    macro(int, localStorageQuotaBytes, {-1}, "Local storage quota. -1 mean estimate from data storage capacity.") \
    macro(bool, enableServiceWorker, {false}, "Enable Service Worker support.") \
    macro(bool, enableIndexedDB, {false}, "Enable IndexedDB support. ") \
    macro(int, indexedDBStorageQuotaRatio, {-1}, "") \
    macro(int, maxMemorySavingIterations, {3}, "") \
    macro(bool, enableWebRuntimeLoad, {true}, "Enable WebRuntimeLoad builtin extension.") \
    macro(bool, enableLifecycle2, {true}, "Enable page lifecycle.")     \

//
// configuration options set via envs or container environment
//
#define FOR_EACH_ENV_OPTION(macro)                                      \
    macro(std::string, runtimeDir, {0}, "")                             \
    macro(unsigned, totalDiskSpaceBytes, {0}, "")                       \
    macro(ProcessEnvironment, browserEnvs, {}, "")                      \
    macro(std::string, navigatorLanguage, {}, "")                       \
    macro(std::string, locale, {}, "")                                  \
    macro(std::string, fireboltEndpoint, {}, "")
