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
#include "launchconfig.h"
#include <unistd.h>
#include <glib.h>
#include <sys/statfs.h>

#include <utility>
#include <iomanip>
#include <fstream>
#include <string>
#include <sstream>
#include <vector>
#include <filesystem>
#include <cstdlib>
#include <type_traits>

#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

using json = nlohmann::json;

namespace
{
    // internal
    //
    // Helper for printing parsed options
    template <typename T>
    void printOption(const char* name, const T& value)
    {
        std::stringstream ss;

        if constexpr (std::is_same_v<T, bool>)
        {
            ss << std::boolalpha << value;
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            ss << std::quoted(value);
        }
        else if constexpr (std::is_same_v<T, LoadFailurePolicy>)
        {
            switch(value)
            {
                case LoadFailurePolicy::Ignore:
                    ss << std::quoted("ignore");
                    break;
                case LoadFailurePolicy::Display:
                    ss << std::quoted("display");
                    break;
                case LoadFailurePolicy::Terminate:
                default:
                    ss << std::quoted("terminate");
                    break;
            }
        }
        else if constexpr (std::is_same_v<T, LocalFilePath>)
        {
            ss << std::quoted(value.string());
        }
        else if constexpr (std::is_same_v<T, std::vector<LocalFilePath>>)
        {
            bool first = true;
            ss << '[';
            for (const auto& v : value)
            {
                if (first)
                    first = false;
                else
                    ss << ',';
                ss << v;
            }
            ss << ']';
        }
        else if constexpr (std::is_same_v<T, std::map<std::string, std::string>>)
        {
            bool first = true;
            ss << "{";
            for (const auto& [k, v] : value)
            {
                if (first)
                    first = false;
                else
                    ss << ',';
                ss << std::quoted(k) << ':' << std::quoted(v);
            }
            ss << "}";
        }
        else
        {
            ss << value;
        }

        g_message("%s = %s", name, ss.str().c_str());
    }

    bool ciEqual(const std::string& a, const std::string& b)
    {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](unsigned char a, unsigned char b)
        {
            return std::tolower(a) == std::tolower(b);
        });
    }

    bool ciStartsWith(const std::string &s, const std::string &p)
    {
        return (s.size() >= p.size()) && std::equal(p.begin(), p.end(), s.begin(), [](unsigned char a, unsigned char b)
        {
            return std::tolower(a) == std::tolower(b);
        });
    }

    // internal
    //
    // A helper utility, converts a string value to the type of launch option.
    // It is probably full of subtle bugs, use with caution.
    template <typename T>
    T processOption(const std::string& val, bool invert = false)
    {
        if constexpr (std::is_same_v<T, bool>)
        {
            return (val.empty() || ciEqual(val, "false") || (val == "0")) == invert;
        }
        else if constexpr (std::is_same_v<T, LocalFilePath>)
        {
            fs::path fullPath = ([&val](){
                return val.starts_with( DEFAULT_LOCAL_FILE_DIR )
                    ? fs::path { val }
                    : fs::path { DEFAULT_LOCAL_FILE_DIR } /  val;
            })();
            std::error_code ec;
            if (fs::exists(fullPath, ec))
                return LocalFilePath(fullPath);
            g_warning("Specified path('%s') doesn't exist in local package dir('%s').", val.c_str(), DEFAULT_LOCAL_FILE_DIR);
            return {};
        }
        else if constexpr (std::is_same_v<T, std::vector<LocalFilePath>>)
        {
            std::vector<LocalFilePath> result;
            std::string token;
            std::stringstream ss { val };
            while (std::getline(ss, token, ';'))
            {
                auto fullPath = processOption<LocalFilePath>(token);
                if (!fullPath.empty())
                    result.emplace_back(std::move(fullPath));
            }
            return result;
        }
        else if constexpr (std::is_same_v<T, LoadFailurePolicy>)
        {
            auto result = LoadFailurePolicy::Terminate;
            if (ciEqual(val, "terminate"))
                result = LoadFailurePolicy::Terminate;
            else if (ciEqual(val, "display"))
                result = LoadFailurePolicy::Display;
            else if (ciEqual(val, "ignore"))
                result = LoadFailurePolicy::Ignore;
            return result;
        }
        else if constexpr (std::is_same_v<T, int>)
        {
            return static_cast<int>(std::strtol(val.c_str(), nullptr, 10));
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return val;
        }
        else
        {
            g_warning("Unhandled type %s.", typeid(T).name());
            return val;
        }
    }

    unsigned calculateHomeDirCapacityInBytes()
    {
        constexpr unsigned defaultValue = 1 * 1024 * 1024;
        const gchar* homeDir = g_get_home_dir();
        struct statfs homePrivateFSStats{};
        if (statfs(homeDir, &homePrivateFSStats) < 0)
        {
            g_warning("failed to get get FS stats for %s, errno = %d", homeDir, errno);
            return defaultValue;
        }
        const unsigned homePrivateSizeBytes = homePrivateFSStats.f_bsize * homePrivateFSStats.f_blocks;
        g_info("%s size is %d bytes", homeDir, homePrivateSizeBytes);
        return homePrivateSizeBytes;
    }

    std::string stringify(const json& j)
    {
        if(j.is_string())
        {
            return j.get<std::string>();
        }
        return j.dump();
    }

    void setProcessEnv(ProcessEnvironment &envs, const std::string &key, const std::string &val)
    {
        envs[key] = val;
    }

    enum key_match_e {
        MATCH_POSITIVE  = 0,
        MATCH_NEGATIVE  = 1,
        MATCH_NONE      = 2
    };

    key_match_e matchKey(const std::string &key, const std::string &var, bool keyFlag = true)
    {
        auto ret = MATCH_NONE;
        if (ciEqual(key, var))
        {
            ret = MATCH_POSITIVE;
        }
        else if (ciStartsWith(key, "enable"))
        {
            if (keyFlag)
            {
                ret = matchKey(key.substr(6), var, false);
            }
        }
        else if (ciStartsWith(key, "disable"))
        {
            if (keyFlag)
            {
                ret = matchKey(key.substr(7), var, false);
                if (ret == MATCH_POSITIVE)
                {
                    ret = MATCH_NEGATIVE;
                }
                else if (ret == MATCH_NEGATIVE)
                {
                    ret = MATCH_POSITIVE;
                }
            }
        }
        else if (ciStartsWith(var, "enable"))
        {
            if (ciEqual(key, var.substr(6)))
            {
                ret = MATCH_POSITIVE;
            }
        }
        else if (ciStartsWith(var, "disable"))
        {
            if (ciEqual(key, var.substr(7)))
            {
                ret = MATCH_NEGATIVE;
            }
        }
        return ret;
    }
}  // namespace

LaunchConfig::LaunchConfig(const std::string &configPath)
{
    // First parse given config file and apply options from configuration.options and configuration.envs
    do
    {
        if (configPath.empty())
        {
            g_info("Empty config path, proceeding with default configuration");
            break;
        }

        std::ifstream ifs(configPath);
        if (!ifs.is_open())
        {
            g_warning("Couldn't open config file at %s", configPath.c_str());
            break;
        }
        auto config = json::parse(ifs, nullptr, false, true);
        if (config.is_discarded() || !config.contains("configuration"))
            break;

        auto configuration = config["configuration"];
        if (configuration.contains("options"))
        {
            auto options = configuration["options"];
            for (auto& [key, val] : options.items())
            {
                if (!val.is_primitive())
                {
                    if (val.contains("key") && val["key"].is_primitive() &&
                        val.contains("value") && val["value"].is_primitive())
                    {
                        applyOption(stringify(val["key"]), stringify(val["value"]));
                    }
                }
                else
                {
                    applyOption(key, stringify(val));
                }
            }
        }

        if (configuration.contains("envs"))
        {
            auto envs = configuration["envs"];
            for (auto& [key, val] : envs.items())
            {
                if (!val.is_primitive())
                {
                    if (val.contains("key") && val["key"].is_primitive() &&
                        val.contains("value") && val["value"].is_primitive())
                    {
                        setProcessEnv(m_browserEnvs, stringify(val["key"]), stringify(val["value"]));
                    }
                }
                else
                {
                    setProcessEnv(m_browserEnvs, key, stringify(val));
                }
            }
        }
    } while (0);

    // Next deduce "other" options and perform a sanity check
    m_totalDiskSpaceBytes = calculateHomeDirCapacityInBytes();

    if (m_localStorageQuotaBytes < 0)
        m_localStorageQuotaBytes = estimateLocalStorageQuota();

    if (m_indexedDBStorageQuotaRatio < 0 || m_indexedDBStorageQuotaRatio > 100)
        m_indexedDBStorageQuotaRatio = 50;

    if (const auto* endpoint = g_getenv("FIREBOLT_ENDPOINT"); endpoint != nullptr)
        m_fireboltEndpoint = endpoint;

    if (const char* env = g_getenv("LANG"); env != nullptr)
    {
        std::string lang { env };

        // strip off codeset
        lang = lang.substr(0, lang.find('.'));

        // replace posix style '_' with '-', ie. "en_GB" becomes "en-GB"
        if ((lang.length() > 2) && (lang[2] == '_'))
        {
            lang[2] = '-';
        }
        // apparently if country code is present and is only 2 digits long,
        // then it must be upper case, ie. "en-GB" not "en-gb"
        if ((lang.length() == 5) && (lang[2] == '-'))
        {
            lang[3] = std::toupper(lang[3]);
            lang[4] = std::toupper(lang[4]);
        }
        m_navigatorLanguage = lang;

        if (lang[2] == '-')
            m_locale = lang.substr(3);
    }
    else
    {
        m_navigatorLanguage = "en";
    }

    m_runtimeDir = ([](){
        std::string result = DEFAULT_RUNTIME_DIR;
        char exe_path[PATH_MAX];
        ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
        if (len != -1) {
            exe_path[len] = '\0';
            gchar *tmp = g_path_get_dirname(exe_path);
            result = std::string(tmp);
            g_free(tmp);
        }
        return result;
    })();
}

int LaunchConfig::estimateLocalStorageQuota() const
{
    // if no custom local storage quota is specified we use predefined values
    // which are optimal for average (leaning towards optimistic) cases

    // CAUTION!
    // below defaults assume only 1 local storage file is used, so if
    // both the app and index.html are going to use local storage, the custom
    // value should be defined for such application - please note that in such
    // case the quota will apply to each file separately

    // the overall formula for obtaining LS quota is:
    // (
    // total disk space of /home/private/
    // - disk space reserved for WPE WebKit (cookie jar etc.) (150K[average]-300K[worst])
    // - SQLite shared memory file (one per DB) (32K)
    // )
    // / 2.0 (one half of the space is for DB file, and the other is for WAL file)
    // / LS-to-DB-file-ratio (2.5[average]-4.5[worst])

    const int homePrivateSizeBytes = totalDiskSpaceBytes();
    if (homePrivateSizeBytes <= 1 * 1024 * 1024)
        return 150 * 1024;
    if (homePrivateSizeBytes <= 2 * 1024 * 1024)
        return 350 * 1024;
    if (homePrivateSizeBytes <= 3 * 1024 * 1024)
        return 570 * 1024;
    return 770 * 1024;
}

void LaunchConfig::printConfig() const
{
#define PRINT_OPTION(type_, name_, init_, help_)    \
    printOption(#name_, name_());

    FOR_EACH_RDK_CONFIG_OPTION(PRINT_OPTION)
    FOR_EACH_ENV_OPTION(PRINT_OPTION)

#undef PRINT_OPTION
}

void LaunchConfig::applyCmdLineOptions(std::map<std::string, std::string> options)
{
    for (const auto& [key, val] : options)
    {
        applyOption(key, val);
    }
}

bool LaunchConfig::applyOption(const std::string &key, const std::string &val)
{
    key_match_e match = MATCH_NONE;
#define HANDLE_RDK_CONFIG_OPTION(type_, name_, init_, help_)            \
    if ((match = matchKey(key, #name_)) != MATCH_NONE)             \
    {                                                                   \
        m_##name_ = processOption<type_>(val, match == MATCH_NEGATIVE); \
    }                                                                   \
    else

    FOR_EACH_RDK_CONFIG_OPTION(HANDLE_RDK_CONFIG_OPTION)
    {
        g_message("Unknown option: %s", key.c_str());
        return false;
    }
#undef HANDLE_RDK_CONFIG_OPTION
    return true;
}
