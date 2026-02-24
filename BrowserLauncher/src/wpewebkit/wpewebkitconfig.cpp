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
#include "wpewebkitconfig.h"
#include "wpewebkitutils.h"

#include <unistd.h>
#include <sys/sysinfo.h>

#include <regex>
#include <bit>
#include <set>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <numeric>
#include <sstream>

namespace fs = std::filesystem;

namespace
{

unsigned long readLimits(const std::string cgroup_path, unsigned long defaultLimit)
{
    GError *error = nullptr;
    gchar *mem_limit_str;
    gsize length;
    unsigned long totalLimitMb = 0;
    if (!g_file_get_contents(cgroup_path.c_str(), &mem_limit_str, &length, &error))
    {
        g_warning("failed to open cgroup memory limit file %s - %s",
                  cgroup_path.c_str(), error->message);
        totalLimitMb = defaultLimit;
        g_error_free(error);
    }
    else
    {
        unsigned long long limitInBytes = std::strtoll(mem_limit_str, nullptr, 10);
        g_free(mem_limit_str);
        totalLimitMb = std::clamp<unsigned long>(limitInBytes / 1024UL / 1024UL, 100UL, 2048UL);
    }
    return totalLimitMb;
}

void setEnvVar(const char *varName, const std::string& value, bool replace)
{
    // FIXME: setenv/putenv are not thread safe and may cause random crashes.
    g_setenv(varName, value.c_str(), replace);
}

void prependLdLibraryPath(const std::string& value, const bool replace = true)
{
    std::string ldLibPath = value;

    if (const auto* envLdLibPath = g_getenv("LD_LIBRARY_PATH"); envLdLibPath != nullptr)
        ldLibPath += std::string(":") + envLdLibPath;

    setEnvVar("LD_LIBRARY_PATH", ldLibPath, replace);
}

} // namespace

WpeWebKitConfig::WpeWebKitConfig(const std::shared_ptr<const LaunchConfigInterface> &launchConfig)
    : m_launchConfig(launchConfig)
{
    initExtensionDir();

    // memory limits for WPE are based on cgroup limits so read that first
    unsigned long totalLimitMb = readLimits("/sys/fs/cgroup/memory/memory.limit_in_bytes", 200);

    m_memLimits.networkProcessLimitMB = 50;
    m_memLimits.webProcessLimitMB = totalLimitMb - m_memLimits.networkProcessLimitMB;
    m_memLimits.networkProcessPollIntervalSec = 5.0; // Check memory usage every 5 sec
    m_memLimits.pollIntervalSec = 1.0; // Check memory usage every 1 sec

    if (m_launchConfig->enableServiceWorker())
    {
        // Currently there is only one use case for service workers. With other use cases,
        // the service worker typical mem usage may need to become configurable
        m_memLimits.serviceWorkerWebProcessLimitMB = 100;
        m_memLimits.webProcessLimitMB -= m_memLimits.serviceWorkerWebProcessLimitMB;
    }

    // Sanity check that there is no overflow error
    if (m_memLimits.webProcessLimitMB > totalLimitMb)
    {
        // Set an invalid memory limit for the web process so this can be detected on use attempt
        m_memLimits.webProcessLimitMB = 0;
    }
}

WpeWebKitConfig::~WpeWebKitConfig()
{
    if (std::error_code ec; fs::exists(m_extTmpDirectory, ec))
    {
        g_message("clearing tmp dir");
        fs::remove_all(m_extTmpDirectory, ec);
    }
}

/*!
    \internal

    Creates the temporary directory for the extensions and then adds symlinks
    to point back to the real extensions based on what extensions should be
    loaded.
 */
bool WpeWebKitConfig::initExtensionDir()
{
    gchar *tmpDir = g_mkdtemp(g_build_filename(g_get_tmp_dir(), "webkit.view.extensions.XXXXXX", nullptr));
    if (tmpDir != nullptr)
    {
        m_extTmpDirectory = tmpDir;
        g_clear_pointer(&tmpDir, g_free);
    }

    // populate the temporary extension dir with symlinks
    if (m_extTmpDirectory.empty())
    {
        g_critical("failed to create temporary directory for the extensions");
        return false;
    }

    // default extensions, always loaded
    std::set<std::string> extensions {
        "libWindowMinimizeExtension.so"
    };

    // extensions loaded based on config
    if (m_launchConfig->enableConsoleLog())
        extensions.insert("libLogExtension.so");

    std::string extDirectory = m_launchConfig->runtimeDir() + "/wpewebkit/extensions";

    // symlink the extensions
    for (const auto &extFileName : extensions)
    {
        std::error_code ec;
        const std::string oldPath = extDirectory + "/" + extFileName;
        const std::string newPath = m_extTmpDirectory + "/" + extFileName;
        if (!fs::exists(oldPath, ec))
        {
            g_warning("failed to find web extension '%s' at '%s'",
                      extFileName.c_str(), oldPath.c_str());
            continue;
        }

        fs::create_symlink(oldPath, newPath, ec);
        if (ec)
        {
            g_critical("failed to create symlink '%s' -> '%s', %s",
                       oldPath.c_str(), newPath.c_str(), ec.message().c_str());
        }
        else
        {
            g_message("added extension symlink '%s' -> '%s'",
                      oldPath.c_str(), newPath.c_str());
        }
    }

    // add any extra extensions from the app package
    const auto& extraAppExts = m_launchConfig->browserExtensions();
    for (const auto &appExtFilePath : extraAppExts)
    {
        std::error_code ec;
        const std::string newPath = m_extTmpDirectory + "/" + fs::path(appExtFilePath).filename().string();
        if (!fs::exists(appExtFilePath, ec))
        {
            g_warning("failed to find web extension '%s'",
                      appExtFilePath.c_str());
            continue;
        }

        fs::create_symlink(appExtFilePath, newPath, ec);
        if (ec)
        {
            g_critical("failed to create symlink '%s' -> '%s', %s",
                       appExtFilePath.c_str(), newPath.c_str(), ec.message().c_str());
        }
        else
        {
            g_message("added extension symlink '%s' -> '%s'",
                      appExtFilePath.c_str(), newPath.c_str());
        }
    }

    return true;
}

/*!
    \internal
    \static

    Gets the number of CPUs the process is allowed to use.

    It reads the `Cpus_allowed` entry in the /proc/self/status file as that
    matches the actual CPUs available to the container.

    \see https://man7.org/linux/man-pages/man7/cpuset.7.html
 */
uint WpeWebKitConfig::getCpusAllowed()
{
    GError *error = nullptr;
    gchar *status;
    gsize length;

    if (!g_file_get_contents("/proc/self/status", &status, &length, &error))
    {
        g_warning("failed to open '/proc/self/status' - %s", error->message);
        g_error_free(error);
        return 1;
    }

    if (length == 0 || status == nullptr)
    {
        g_warning("failed to read '/proc/self/status'");
        g_free(status);
        return 1;
    }

    std::istringstream iss{std::string{status}};
    g_clear_pointer(&status, g_free);

    const std::string prefix = std::string("Cpus_allowed:\t");
    std::string line;
    while (std::getline(iss, line))
    {
        if (line.starts_with(prefix))
        {
            unsigned bits = std::strtol(line.substr(prefix.length()).c_str(), nullptr, 16);
            return std::clamp(static_cast<unsigned>(std::popcount(bits)), 1u, 32u);
        }
    }

    g_warning("failed to get the number of allowed cpus, defaulting to 1");
    return 1;
}

/*!
    Sets the platform specific environment variables mainly for gstreamer. This
    function is not called if Rialto is enabled.
 */
void WpeWebKitConfig::setGStreamerEnvironment()
{
    enum class Platform {
        Unknown,
        Realtek,
        Broadcom,
        Amlogic,
        Mediatek
    };

    const auto detectPlatform = [](){
        std::error_code ec;
        if (fs::exists("/proc/brcm", ec))
            return Platform::Broadcom;
        else if (fs::exists("/dev/aml_msync", ec))
            return Platform::Amlogic;
        else if (fs::exists("/usr/lib/realtek", ec))
            return Platform::Realtek;
        return Platform::Unknown;
    };

    switch (detectPlatform())
    {
        case Platform::Realtek:
            prependLdLibraryPath("/usr/lib/realtek");
            setEnvVar("LD_PRELOAD", "/usr/lib/libwayland-client.so.0"
                                    ":/usr/lib/libwayland-egl.so.0"
                                    ":/usr/lib/realtek/libVOutWrapper.so"
                                    ":/usr/lib/realtek/libjpu.so"
                                    ":/usr/lib/realtek/libvpu.so"
                                    ":/usr/lib/realtek/libAOutWrapper.so", false);
            setEnvVar("WEBKIT_GST_QUIRKS", "realtek,westeros", false);
            break;

        case Platform::Broadcom:
            setEnvVar("WEBKIT_GST_QUIRKS", "broadcom,westeros", false);
            break;

        case Platform::Amlogic:
            setEnvVar("WESTEROS_SINK_AMLOGIC_USE_DMABUF", "1", false);
            setEnvVar("WESTEROS_SINK_USE_FREERUN", "1", false);
            setEnvVar("WEBKIT_GST_QUIRKS", "amlogic,westeros", false);
            break;

        case Platform::Mediatek:
            setEnvVar("WEBKIT_GST_QUIRKS", "mediatek,westeros", false);
            break;

        case Platform::Unknown:
        default:
            setEnvVar("WEBKIT_GST_QUIRKS", "amlogic,realtek,broadcom,westeros", false);
            g_warning("Unknown platform");
            break;
    }

    setEnvVar("WEBKIT_GST_HOLE_PUNCH_QUIRK", "westeros", false);
}

/*!
    \internal
    \static

    Sets up an isolated environment for Rialto. Exposes minimal set of
    GStreamer plugins from the device; overrides OCDM implementation.

 */
bool WpeWebKitConfig::setRialtoEnvironment()
{
    std::string tmpDir = ([]{
        std::string result;
        gchar *tmp = g_build_filename (g_get_tmp_dir (), "webkit.view.rialto.XXXXXX", nullptr);
        if (tmp = g_mkdtemp(tmp); tmp != nullptr)  {
            result = tmp;
            g_free(tmp);
            return result;
        }
        g_critical("failed to create temporary directory");
        return result;
    })();

    const std::string pluginsDir = tmpDir + "/gst";
    const std::string libsDir = tmpDir + "/lib";
    std::error_code ec;

    if (!fs::create_directories(pluginsDir, ec))
    {
        g_critical("Failed to create directory '%s' for GStreamer plugins sym links, %s", pluginsDir.c_str(), ec.message().c_str());
        return false;
    }

    if (!fs::create_directories(libsDir, ec))
    {
        g_critical("Failed to create directory '%s' for OCDM library sym links, %s", libsDir.c_str(), ec.message().c_str());
        return false;
    }

    const std::set<std::string> plugins = {
        "libgstapp.so",
        "libgstaudioresample.so",
        "libgstcoreelements.so",
        "libgstplayback.so",
        "libgstaudioconvert.so",
        "libgstautodetect.so",
        "libgstrialtosinks.so",
        "libgsttypefindfunctions.so",
        "libgstaudioparsers.so",
        "libgstvideoparsersbad.so",
        "libgstopusparse.so",
        "libgstisomp4.so",
        "libgstmatroska.so",
        "libgstid3demux.so",
        "libgsticydemux.so",
        "libgstwavparse.so",
        "libgstinter.so",
        "libgstaudiomixer.so",
        "libgstgio.so",
        "libgstinterleave.so",
    };

    for (const std::string &pluginFileName : plugins)
    {
        std::error_code ec;
        const std::string sysPath = "/usr/lib/gstreamer-1.0/" + pluginFileName;
        const std::string newPath = pluginsDir + "/" + pluginFileName;
        if (!fs::exists(sysPath, ec))
        {
            g_warning("failed to find gst plugin '%s'",
                      pluginFileName.c_str());
        }

        fs::create_symlink(sysPath, newPath, ec);
        if (ec)
        {
            g_critical("failed to create symlink '%s' -> '%s'",
                       sysPath.c_str(), newPath.c_str());
            return false;
        }
        else
        {
            g_info("added gst plugin symlink '%s' -> '%s'",
                   sysPath.c_str(), newPath.c_str());
        }
    }

    for (const std::string libFileName : {"libocdm.so.2", "libocdm.so.4"})
    {
        std::error_code ec;
        const std::string sysPath = "/usr/lib/" + libFileName;
        const std::string newPath = libsDir + "/" + libFileName;
        const std::string ocdmRialtoPath = "/usr/lib/libocdmRialto.so.1";
        if (!fs::exists(sysPath, ec))
        {
            continue;
        }
        else if (!fs::exists(ocdmRialtoPath, ec))
        {
            g_critical("'%s' not found.", ocdmRialtoPath.c_str());
            return false;
        }

        fs::create_symlink(ocdmRialtoPath, newPath, ec);
        if (ec)
        {
            g_critical("failed to create symlink '%s' -> '%s'",
                       sysPath.c_str(), newPath.c_str());
            return false;
        }
        else
        {
            g_info("added ocdm library symlink '%s' -> '%s'",
                   sysPath.c_str(), newPath.c_str());
        }
    }

    prependLdLibraryPath(libsDir);
    setEnvVar("GST_PLUGIN_SYSTEM_PATH", pluginsDir, true);
    setEnvVar("WEBKIT_GST_QUIRKS", "rialto", false);
    setEnvVar("WEBKIT_GST_HOLE_PUNCH_QUIRK", "rialto", false);
    setEnvVar("WEBKIT_GST_ENABLE_AUDIO_MIXER", "1", false);

    return true;
}

/*!
    Sets the environment variables used by WPE based on the launch config.

 */
void WpeWebKitConfig::setEnvironment() const
{
    g_info("creating wpewebkit environment");

    if (m_launchConfig->isHeadless())
    {
        // set the headless backend
        setEnvVar("WPE_BACKEND_LIBRARY", "/usr/lib/libWPEBackend-headless.so", true);
    }
    else
    {
        // assume 1080p by default, the correct resolution will be set later after egl target is created
        setEnvVar("WEBKIT_RESOLUTION_WIDTH", "1920", false);
        setEnvVar("WEBKIT_RESOLUTION_HEIGHT", "1080", false);
    }

    auto fontConfigPath = m_launchConfig->runtimeDir() + "/fonts.conf";
    if (std::error_code ec; fs::exists(fontConfigPath, ec))
    {
        // use font.conf from within the runtime widget
        setEnvVar("FONTCONFIG_FILE", fontConfigPath, false);
    }

    // flash usage limits
    {
        // disable WPE disk caching of browser pages / resources
        setEnvVar("WPE_DISK_CACHE_SIZE", "0", false);

        // disable media disk cache for all apps, as otherwise containers may run
        // out of space, potentially causing app crashes (e.g. Spotify podcasts,
        // XITE) or other errors
        setEnvVar("WPE_SHELL_DISABLE_MEDIA_DISK_CACHE", "1", false);

        // limit localStorage SQLite wal journal file, because unless it's growing
        // indefinitely. Set value means pages, so it is limited to ~40kB
        setEnvVar("WPE_WAL_AUTOCHECKPOINT", "10", false);

        // disable persistent gstreamer cache - instead put it in /tmp
        setEnvVar("GST_REGISTRY", "/tmp/gstreamer-registry.bin", false);
    }

    // memory limits
    {
        char buffer[128];
        snprintf(buffer, sizeof(buffer), "%luM",
                 m_memLimits.networkProcessLimitMB + m_memLimits.webProcessLimitMB +
                 m_memLimits.serviceWorkerWebProcessLimitMB);
        setEnvVar("WPE_RAM_SIZE", buffer, false);

        if (WpeWebKitUtils::webkitVersion() < VersionNumber(2, 38, 0))
        {
            snprintf(buffer, sizeof(buffer),
                     "wpenetworkprocess:%lum,wpewebprocess:%lum",
                     m_memLimits.networkProcessLimitMB, m_memLimits.webProcessLimitMB);
            setEnvVar("WPE_POLL_MAX_MEMORY", buffer, false);
        }
        else
        {
            // WPE WebKit 2.38 has different defaults than 2.28, so let's set
            // this envvar to make default buffer sizes consistent across versions
            // Please note that apps are still able to override that
            setEnvVar("MSE_MAX_BUFFER_SIZE", "v:30m,a:3m,t:1m", false);
        }
    }

    // GPU-memory-based memory pressure mechanism setup
    if (m_launchConfig->enableGpuMemLimiting())
    {
        unsigned long totalGPULimitMb = readLimits("/sys/fs/cgroup/gpu/gpu.limit_in_bytes", 0);
        if (totalGPULimitMb > 0)
        {
            char maxGPUMemoryBuffer[64];
            snprintf(maxGPUMemoryBuffer, sizeof(maxGPUMemoryBuffer), "%luM", totalGPULimitMb);
            setEnvVar("WPE_POLL_MAX_MEMORY_GPU", maxGPUMemoryBuffer, false);
            setEnvVar("WPE_POLL_MAX_MEMORY_GPU_FILE", "/sys/fs/cgroup/gpu/gpu.usage_in_bytes", false);
        }
    }

    // force MSAA compositor
    setEnvVar("CAIRO_GL_COMPOSITOR", "msaa", false);

    // enable threaded compositor
    static const uint cpusAllowed = getCpusAllowed();
    setEnvVar("WEBKIT_NICOSIA_PAINTING_THREADS", (cpusAllowed > 2 ? "2" : "1"), false);

    // if rialto is enabled then use a different set of env vars
    if (g_getenv("RIALTO_SOCKET_PATH") && setRialtoEnvironment())
    {
        g_message("set up environment for rialto");
    }
    else
    {
        setGStreamerEnvironment();
    }

    // for Mali platforms need to set WPE_POLL_GPU_IN_FOOTPRINT=1 for removing
    // the memory footprint from the RSS otherwise it skews the memory pressure
    // logic
    if (std::error_code ec; fs::exists("/dev/mali0", ec))
    {
        setEnvVar("WPE_POLL_GPU_IN_FOOTPRINT", "1", false);
    }

    // enable TLSv1 for prime.video
    if (m_launchConfig->disableWebSecurity())
    {
        setEnvVar("NO_FORCE_TLS_1_2", "1", false);
    }

    // WPE 2.28 doesn't support the legacy web inspector, however the AI code
    // is setting the env var for that, so swap WEBKIT_LEGACY_INSPECTOR_SERVER
    // for WEBKIT_INSPECTOR_HTTP_SERVER
    // (FIXME: add some more generic way to enable webinspector)
    const char* webInspectorAddr = g_getenv("WEBKIT_LEGACY_INSPECTOR_SERVER");
    if (webInspectorAddr && webInspectorAddr[0])
    {
        g_unsetenv("WEBKIT_LEGACY_INSPECTOR_SERVER");
        setEnvVar("WEBKIT_INSPECTOR_HTTP_SERVER", webInspectorAddr, false);
    }

    // finally insert additional envs from config
    const auto& envVars = m_launchConfig->browserEnvs();
    for (const auto& kv : envVars)
    {
        setEnvVar(kv.first.c_str(), kv.second.c_str(), true);
    }
}

/*!
    \internal
    \static

    Simple utility to do basic string escaping for javascript strings.

 */
std::string WpeWebKitConfig::escapeJavascriptString(const std::string &str)
{
    auto replace_all = [](std::string& text, const char c, const std::string& str) {
        size_t pos = 0;
        while ((pos = text.find(c, pos)) != std::string::npos) {
            text.replace(pos, 1, str);
            pos += str.length();
        }
    };

    std::string escaped(str);
    replace_all(escaped, '\\', R"(\\)");
    replace_all(escaped, '\'', R"(\')");
    replace_all(escaped, '\"', R"(\")");
    replace_all(escaped, '\n', R"(\n)");
    replace_all(escaped, '\r', R"(\r)");
    return escaped;
}

/*!
    Creates the user script to inject into the browser.

 */
std::vector<std::string> WpeWebKitConfig::userScripts() const
{
    std::vector<std::string> scripts;

    // create the default user script
    {
        std::string script;
        std::stringstream scriptStream;

        const std::string fireboltEndpoint = m_launchConfig->fireboltEndpoint();
        if (!fireboltEndpoint.empty())
        {
            scriptStream << "window.__firebolt = { endpoint: '"
                         << escapeJavascriptString(fireboltEndpoint) << "' };\n";
        }

        // is this still needed ?
        scriptStream << "window.FileSystem = undefined;\n";

        scripts.emplace_back(scriptStream.str());
    }

    // add any other user scripts from the config
    for (const auto& filePath : m_launchConfig->userScripts())
    {
        gsize sz;
        gchar *ptr;
        GError *error = nullptr;

        if (!g_file_get_contents(filePath.c_str(), &ptr, &sz, &error))
        {
            g_warning("failed to open user script '%s' - %s",
                      filePath.c_str(), error->message);
            g_error_free(error);
        }
        else
        {
            scripts.emplace_back(std::string(ptr, sz));
            g_free(ptr);
        }
    }

    return scripts;
}

/*!
    Creates the user style sheets to inject into the browser.

 */
std::vector<std::string> WpeWebKitConfig::userStyleSheets() const
{
    std::vector<std::string> userStyleSheets;

    for (const auto& filePath : m_launchConfig->userStyleSheets())
    {
        gsize sz;
        gchar *ptr;
        GError *error = nullptr;
        if (!g_file_get_contents(filePath.c_str(), &ptr, &sz, &error))
        {
            g_warning("failed to open user stylesheet '%s' - %s",
                      filePath.c_str(), error->message);
            g_error_free(error);
        }
        else
        {
            userStyleSheets.emplace_back(std::string(ptr, sz));
            g_free(ptr);
        }
    }

    return userStyleSheets;
}

/*!
    \internal

    Generates the user agent for the browser instance based on user config.

 */
std::string WpeWebKitConfig::userAgent(const std::string &existing) const
{
    g_info("creating WPE user agent string");

    std::string userAgent;
    userAgent.reserve(256);

    if (!m_launchConfig->customUserAgent().empty())
    {
        userAgent = m_launchConfig->customUserAgent();
    }
    else
    {
        // use either the existing UA as a base or if a custom one was configured
        const std::string customBase = m_launchConfig->customUserAgentBase();
        if (!customBase.empty())
            userAgent = customBase;
        else
            userAgent = existing;

        // append "WPE" if not already in the string
        if (userAgent.find("WPE") == std::string::npos)
        {
            if (WpeWebKitUtils::webkitVersion() >= VersionNumber(2, 38, 0))
                userAgent += " WPE/1.0";
            else
                userAgent += " WPE";
        }
    }

    g_message("user agent: %s", userAgent.c_str());
    return userAgent;
}

/*!
    Generates the webkit settings / preferences based on the internal config.

    It is the callers responsibility to free the returned WebKitSettings object
    by calling g_object_unref(...).

 */
WebKitSettings* WpeWebKitConfig::webKitSettings() const
{
    const VersionNumber webKitVersion = WpeWebKitUtils::webkitVersion();

    WebKitSettings *preferences = webkit_settings_new();

    webkit_settings_set_enable_page_cache(preferences, FALSE);
    webkit_settings_set_enable_directory_upload(preferences, FALSE);
    webkit_settings_set_enable_html5_local_storage(preferences, m_launchConfig->enableLocalStorage() ? TRUE : FALSE);

    // always enable MSE / EME
    webkit_settings_set_enable_encrypted_media(preferences, TRUE);
    webkit_settings_set_enable_mediasource(preferences, TRUE);
    // webkit_settings_set_enable_mock_capture_devices(preferences, TRUE);

    // always allow an app to close its window
    webkit_settings_set_allow_scripts_to_close_windows(preferences, TRUE);

    // (required?)
    // webkit_settings_set_media_content_types_requiring_hardware_support(preferences, "");

    // always enable webgl
    webkit_settings_set_enable_webgl(preferences, TRUE);

    // turn on/off file URLs Cross Access
    webkit_settings_set_allow_file_access_from_file_urls(preferences, m_launchConfig->allowFileURLsCrossAccess() ? TRUE : FALSE);
    webkit_settings_set_allow_universal_access_from_file_urls(preferences, m_launchConfig->allowFileURLsCrossAccess() ? TRUE : FALSE);

    // turn on/off spatial navigation
    webkit_settings_set_enable_spatial_navigation(preferences, m_launchConfig->enableSpatialNavigation() ? TRUE : FALSE);
    webkit_settings_set_enable_tabs_to_links(preferences, m_launchConfig->enableSpatialNavigation() ? TRUE : FALSE);

    // enable non-composited webgl (ie. for lightning apps)
    webkit_settings_set_enable_non_composited_webgl(preferences, m_launchConfig->enableNonCompositedWebGL() ? TRUE : FALSE);

    // enable media-stream support
    webkit_settings_set_enable_media_stream(preferences, m_launchConfig->enableMediaStream() ? TRUE : FALSE);
    if (webKitVersion >= VersionNumber(2, 38, 0))
    {
        // enable webrtc
        webkit_settings_set_enable_webrtc(preferences, m_launchConfig->enableMediaStream() ? TRUE : FALSE);

        if (webKitVersion >= VersionNumber(2, 46, 0))
        {
            webkit_settings_set_enable_ice_candidate_filtering(preferences, FALSE);
        }
    }

    // enable web audio support
    webkit_settings_set_enable_webaudio(preferences, m_launchConfig->enableWebAudio() ? TRUE : FALSE);

    // enable indexeddb support
    if (m_launchConfig->enableIndexedDB())
    {
        if (webKitVersion < VersionNumber(2, 38, 0))
            g_warning("IndexedDB API is not supported on this version of WPEWebKit");

        webkit_settings_set_enable_html5_database(preferences, TRUE);
    }
    else
    {
        webkit_settings_set_enable_html5_database(preferences, FALSE);
    }

    // set the user agent
    const std::string existingUA = webkit_settings_get_user_agent(preferences);
    webkit_settings_set_user_agent(preferences, userAgent(existingUA).c_str());

    // enable media caps
    if (webKitVersion >= VersionNumber(2, 38, 0))
    {
        webkit_settings_set_enable_media_capabilities(preferences, TRUE);
    }

    // enable / disable web security
    if (m_launchConfig->disableWebSecurity())
    {
        if (webKitVersion >= VersionNumber(2, 38, 0))
        {
            g_object_set(G_OBJECT(preferences),
                         "disable-web-security", TRUE,
                         nullptr);
        }
        else
        {
            g_object_set(G_OBJECT(preferences),
                         "enable-websecurity", FALSE,
                         nullptr);
        }
    }

    if (m_launchConfig->allowMixedContent())
    {
        // allow mixed content
        g_object_set(G_OBJECT(preferences),
                     "allow-running-of-insecure-content", TRUE,
                     "allow-display-of-insecure-content", TRUE, nullptr);
    }

    g_object_set(G_OBJECT(preferences),
                 // enable / disable service worker support
                 "enable-service-worker", m_launchConfig->enableServiceWorker() ? TRUE : FALSE,
                 // disable ice candidate filtering
                 "enable-ice-candidate-filtering", FALSE,
                 nullptr);

    if (g_object_class_find_property(G_OBJECT_GET_CLASS(preferences), "enable-page-lifecycle"))
    {
        g_object_set(G_OBJECT(preferences),
                     "enable-page-lifecycle", m_launchConfig->enableLifecycle2() ? TRUE : FALSE,
                     nullptr);
    }
    else if (m_launchConfig->enableLifecycle2())
    {
        g_warning("Page Lifecycle V2 is not supported");
    }

    const char kOpportunisticSweepingAndGCPropName[] = "opportunistic-sweeping-and-gc";
    if (webKitVersion >= VersionNumber(2, 46, 0) &&
        g_object_class_find_property(G_OBJECT_GET_CLASS(preferences), kOpportunisticSweepingAndGCPropName))
    {
        g_object_set(G_OBJECT(preferences), kOpportunisticSweepingAndGCPropName,
                     m_launchConfig->opportunisticSweepingAndGC() ? TRUE : FALSE, nullptr);
    }

    return preferences;
}

/*!
    Returns the cookie accept policy for the browser.

 */
WebKitCookieAcceptPolicy WpeWebKitConfig::cookieAcceptPolicy() const
{
    std::string policy = m_launchConfig->cookieAcceptPolicy();

    std::transform(policy.begin(), policy.end(),
                   policy.begin(), ::tolower);

    if (policy == "always")
        return WEBKIT_COOKIE_POLICY_ACCEPT_ALWAYS;
    else if (policy == "never")
        return WEBKIT_COOKIE_POLICY_ACCEPT_NEVER;
    else if (policy == "no-third-party")
        return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
    else
    {
        g_warning("unknown cookie accept policy '%s', defaulting to 'no-third-party'",
                  policy.c_str());
        return WEBKIT_COOKIE_POLICY_ACCEPT_NO_THIRD_PARTY;
    }
}

/*!
    Returns the path to a directory that has the extensions to load.

    Because we now have a lot of extensions and most aren't needed from one app
    to another, we now create a directory in /tmp and symlink to the extensions
    we needed for the app.

 */
std::string WpeWebKitConfig::extensionsDirectory() const
{
    return m_extTmpDirectory;
}

/*!
    Returns a common set of settings for an extension.  Mostly this is about
    logging.

 */
GVariantRef WpeWebKitConfig::commonExtensionSettings() const
{
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    return GVariantRef { g_variant_builder_end(&builder) };
}


/*!
    Returns an html page contents to display when an load-failure error occurs.
    This should only be used if loadFailurePolicy() returns
    LoadFailurePolicy::Display.

    This will return the contents of the user defined error page if set in the
    app config, or the default error page at wpewebkit/resources/error.html if
    not set.
 */
std::string WpeWebKitConfig::loadFailureErrorPage() const
{
    GError *error = nullptr;

    auto userErrorPagePath = m_launchConfig->loadFailureErrorPage();
    if (!userErrorPagePath.empty())
    {
        gsize sz;
        gchar *ptr;
        if (!g_file_get_contents(userErrorPagePath.c_str(), &ptr, &sz, &error))
        {
            g_warning("failed to open user error page '%s' - %s",
                      userErrorPagePath.c_str(), error->message);
            g_error_free(error); error = nullptr;
        }
        else
        {
            std::string result(reinterpret_cast<const char*>(ptr), sz);
            g_free(ptr);
            return result;
        }
    }

    GBytes *bytes = g_resources_lookup_data("/org/rdk/browser/error.html", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (bytes)
    {
        gsize sz;
        const void *ptr = g_bytes_get_data(bytes, &sz);
        if (ptr && sz)
        {
            std::string result(reinterpret_cast<const char*>(ptr), sz);
            g_bytes_unref(bytes);
            return result;
        }
        g_bytes_unref(bytes);
    }
    else if (error)
    {
        g_warning("failed to load error page from resources, %s", error->message);
        g_error_free(error); error = nullptr;
    }

    return "<html><body>Error</body></html>";
}
