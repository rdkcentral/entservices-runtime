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

#include "launchconfiginterface.h"
#include "launchconfigoptions.h"

#ifndef DEFAULT_RUNTIME_DIR
#define DEFAULT_RUNTIME_DIR "/runtime"
#endif
#ifndef DEFAULT_LOCAL_FILE_DIR
#define DEFAULT_LOCAL_FILE_DIR "/package"
#endif
#define DEFAULT_CONFIG_FILE_PATH  DEFAULT_LOCAL_FILE_DIR "/rdk.config"

class LaunchConfig final : public LaunchConfigInterface
{
private:
    LaunchConfig(const LaunchConfig&) = delete;
    LaunchConfig& operator=(const LaunchConfig&) = delete;

    explicit LaunchConfig(const std::string& configPath);
public:
    static std::shared_ptr<LaunchConfig> create(const std::string& configPath = DEFAULT_CONFIG_FILE_PATH)
    {
        return std::shared_ptr<LaunchConfig>(new LaunchConfig{configPath});
    }

#define DECLARE_OPTION_ACCESSORS(type_, name_, init_, help_)  \
    type_ name_ () const override { return m_##name_; }

    FOR_EACH_RDK_CONFIG_OPTION(DECLARE_OPTION_ACCESSORS)
    FOR_EACH_ENV_OPTION(DECLARE_OPTION_ACCESSORS)
#undef DECLARE_OPTION_ACCESSORS

    void applyCmdLineOptions(std::map<std::string, std::string>);
    void printConfig() const;

private:
    int estimateLocalStorageQuota() const;

#define DEFINE_OPTIONS(type_, name_, init_, help_)     \
    type_ m_##name_ init_;

    FOR_EACH_RDK_CONFIG_OPTION(DEFINE_OPTIONS)
    FOR_EACH_ENV_OPTION(DEFINE_OPTIONS)
#undef DEFINE_OPTIONS
};
