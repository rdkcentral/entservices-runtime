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
    macro(std::string, cookieAcceptPolicy, {"no-third-party"})          \
    macro(bool, allowMixedContent, {true})                              \
    macro(bool, allowFileURLsCrossAccess, {true})                       \
    macro(bool, enableConsoleLog, {true})                               \
    macro(bool, enableLocalStorage, {true})                             \
    macro(bool, enableNonCompositedWebGL, {false})                      \
    macro(bool, enableMediaStream, {false})                             \
    macro(bool, enableWebAudio, {false})                                \
    macro(bool, disableWebSecurity, {false})                            \
    macro(bool, enableSpatialNavigation, {false})                       \
    macro(std::vector<LocalFilePath>, userScripts, {})                  \
    macro(std::vector<LocalFilePath>, userStyleSheets, {})              \
    macro(int, maxUnresponsiveTimeMs, {60*1000})                        \
    macro(bool, isHeadless, {false})                                    \
    macro(bool, enableGpuMemLimiting, {false})                          \
    macro(std::string, customUserAgent, {})                             \
    macro(std::string, customUserAgentBase, {})                         \
    macro(LoadFailurePolicy, loadFailurePolicy, {LoadFailurePolicy::Terminate}) \
    macro(LocalFilePath, loadFailureErrorPage, {})                      \
    macro(std::vector<LocalFilePath>, browserExtensions, {})            \
    macro(bool, enableTesting, {false})                                 \
    macro(int, localStorageQuotaBytes, {-1})                            \
    macro(bool, enableServiceWorker, {false})                           \
    macro(bool, enableIndexedDB, {false})                               \
    macro(int, indexedDBStorageQuotaRatio, {-1})                        \
    macro(int, maxMemorySavingIterations, {3})                          \
    macro(bool, enableWebRuntimeLoad, {true})                           \
    macro(bool, enableLifecycle2, {true})                               \

//
// configuration options set via envs or container environment
//
#define FOR_EACH_ENV_OPTION(macro)                                      \
    macro(std::string, runtimeDir, {0})                                 \
    macro(unsigned, totalDiskSpaceBytes, {0})                           \
    macro(ProcessEnvironment, browserEnvs, {})                          \
    macro(std::string, navigatorLanguage, {})                           \
    macro(std::string, locale, {})                                      \
    macro(std::string, fireboltEndpoint, {})

