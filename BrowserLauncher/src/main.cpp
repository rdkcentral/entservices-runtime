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
#include "browserinterface.h"
#include "browsercontroller.h"
#include "launchconfig.h"

#include <glib-unix.h>
#include <glib.h>
#include <gio/gio.h>
#include <gmodule.h>
#include <dlfcn.h>
#include <glob.h>
#include <csignal>

struct GFreeDeleter
{
  void operator()(gchar* v) { g_free(v); }
};

using gchar_ptr = std::unique_ptr<gchar, GFreeDeleter>;

static bool preLoadLib(const char *pattern)
{
    glob_t globBuf;
    memset(&globBuf, 0x00, sizeof(glob_t));

    // do a glob search for any library that starts with given pattern
    if (glob(pattern, 0, nullptr, &globBuf) != 0)
    {
        g_critical("failed to find any WPEWebKit libraries matching pattern '%s'",
                  pattern);
        return false;
    }

    // pick the highest number library
    for (ssize_t i = static_cast<ssize_t>(globBuf.gl_pathc) - 1; i >= 0; i--)
    {
        const char *libPath = globBuf.gl_pathv[i];
        if (dlopen(libPath, RTLD_LAZY | RTLD_GLOBAL | RTLD_NODELETE) == nullptr)
        {
            g_critical("failed to dlopen '%s' - %s", libPath, dlerror());
        }
        else
        {
            g_message("loaded library @ '%s'", libPath);
            globfree(&globBuf);
            return true;
        }
    }

    g_critical("failed to find and / or dlopen a library");
    globfree(&globBuf);
    return false;
}

static void preLoadWPE(const std::string& runtimeDir)
{
    const std::array<std::string, 3> webkitVersions {
        "wpe-webkit-2.0",
        "wpe-webkit-1.1",
        "wpe-webkit-1.0"
    };

    // try runtime dir
    for (const auto& version : webkitVersions)
    {
        gchar_ptr execDir {g_build_filename(runtimeDir.c_str(), "wpewebkit", "libexec", version.c_str(), nullptr)};
        if (g_file_test(execDir.get(), G_FILE_TEST_EXISTS))
        {
            gchar_ptr libDir { g_build_filename(runtimeDir.c_str(), "wpewebkit", "lib", nullptr) };
            gchar_ptr bundleDir { g_build_filename(libDir.get(), version.c_str(), "injected-bundle", nullptr) };
            gchar_ptr webkitLibFilePattern { g_strdup_printf("%s/%s", libDir.get(), "libWPEWebKit-[0-9]*.so.*") };
            if (!preLoadLib(webkitLibFilePattern.get()))
            {
                g_error("Could not preload %s from %s/...", version.c_str(), webkitLibFilePattern.get());
            }
            else
            {
                g_message("Preloaded %s from %s...", version.c_str(), webkitLibFilePattern.get());
                g_setenv("WEBKIT_EXEC_PATH", execDir.get(), false);
                g_setenv("WEBKIT_INJECTED_BUNDLE_PATH", bundleDir.get(), false);
                g_message("WEBKIT_EXEC_PATH = %s", g_getenv("WEBKIT_EXEC_PATH"));
                g_message("WEBKIT_INJECTED_BUNDLE_PATH = %s", g_getenv("WEBKIT_INJECTED_BUNDLE_PATH"));
                return;
            }
        }
    }

    // fallback to system one
    preLoadLib("/usr/lib/libWPEWebKit-[0-9]*.so.*");
}

static BrowserInterface *createBrowserInstance(const std::string& runtimeDir)
{
    gchar_ptr libPath {g_build_filename(runtimeDir.c_str(), "wpewebkit", "libWpeWebKitBrowser.so", nullptr)};
    GModule* backendModule = g_module_open(libPath.get(), G_MODULE_BIND_LAZY);

    if (!backendModule)
    {
        backendModule = g_module_open("./wpewebkit/libWpeWebKitBrowser.so", G_MODULE_BIND_LAZY);
        if (!backendModule)
        {
            backendModule = g_module_open("libWpeWebKitBrowser.so", G_MODULE_BIND_LAZY);
            if (!backendModule)
            {
                g_error("Error loading libWpeWebKitBrowser.so: %s", g_module_error());
                return nullptr;
            }
        }
    }

    g_module_make_resident(backendModule);

    CreateBrowserInterfaceFunctionPtr factory = 0;
    if (!g_module_symbol(backendModule, "CreateBrowserInterface", reinterpret_cast<void**>(&factory))) {
        g_critical("Error loading CreateBrowserInterface symbol from backend bundle.");
        return nullptr;
    }

    return factory();
}

static gboolean parseRDKConfigOption(
    const char *option_name, const char *value,
    gpointer data, GError **error)
{
    if (option_name[0] != '-' || option_name[1] != '-') {
        g_set_error (error,
                     G_OPTION_ERROR,
                     G_OPTION_ERROR_FAILED,
                     "Invalid option '%s'",
                     option_name);
        return FALSE;
    }
    option_name += 2; // skip '--'
    auto& options = *reinterpret_cast<std::map<std::string, std::string>*>(data);
    options[std::string(option_name)] = std::string(value ?: "");
    return TRUE;
}

static GOptionGroup* createRDKConfigOptionGroup(
    GOptionArgFunc callback, gpointer data)
{
    GOptionEntry entries[] = {
#define DECLARE_OPTION_ENTRY(type_, name_, init_, help_)                \
        { #name_, 0, 0, G_OPTION_ARG_CALLBACK, (gpointer)callback, help_, G_STRINGIFY(type_)},
        FOR_EACH_RDK_CONFIG_OPTION(DECLARE_OPTION_ENTRY)
#undef DECLARE_OPTION_ENTRY
        { nullptr }
    };
    qsort(&entries[0], G_N_ELEMENTS(entries) - 1, sizeof(GOptionEntry), [](const void *p1, const void *p2) -> int {
        GOptionEntry *e1 = (GOptionEntry *) p1;
        GOptionEntry *e2 = (GOptionEntry *) p2;
        return g_strcmp0 (e1->long_name, e2->long_name);
    });
    GOptionGroup* group = g_option_group_new("config", "RDK Config Options", "Show RDK config options", data, nullptr);
    g_option_group_add_entries (group, entries);
    return group;
}

int main(int argc, char *argv[])
{
    GError *error = nullptr;
    gchar *url = nullptr;
    gchar *configPath = nullptr;
    std::map<std::string, std::string> configOptions;

    // parse arguments
    {
        GOptionContext *context;
        GOptionEntry entries[] = {
            { "url", 'u', 0, G_OPTION_ARG_STRING, &url, "Package uri", "file://" DEFAULT_LOCAL_FILE_DIR "/index.html" },
            { "config", 'c', 0, G_OPTION_ARG_STRING, &configPath, "Config path", DEFAULT_LOCAL_FILE_DIR "/rdk.config" },
            { nullptr }
        };
        context = g_option_context_new (nullptr);
        g_option_context_add_main_entries (context, entries, nullptr);
        g_option_context_add_group (context, createRDKConfigOptionGroup (&parseRDKConfigOption, &configOptions));
        if (!g_option_context_parse (context, &argc, &argv, &error))
        {
            g_printerr ("Option parsing failed: %s\n", error->message);
            g_error_free (error); error = nullptr;
        }
        g_option_context_free (context);
        if (!url)
            url = g_strdup("file://" DEFAULT_LOCAL_FILE_DIR "/index.html");
    }

    g_message("starting BrowserLauncher v" BROWSER_LAUNCHER_VERSION ", package url %s", url);

    GApplication* application = g_application_new("org.rdk.BrowserLauncher", G_APPLICATION_NON_UNIQUE);

    // setup signal handlers
    signal(SIGPIPE, SIG_IGN);
    for (const auto signum : {SIGTERM, SIGINT})
    {
        g_unix_signal_add(
            signum,
            [](gpointer userData) {
                g_message("got signal %d", GPOINTER_TO_INT(userData));
                GApplication* application = g_application_get_default();
                BrowserController* controller =
                    reinterpret_cast<BrowserController*> (g_object_get_data(G_OBJECT(application), "browsercontroller"));
                if (controller)
                    controller->close();
                return G_SOURCE_REMOVE;
            },
            GINT_TO_POINTER(signum));
    }

    // process the launch config (rdk.config and environment variables), we do
    // this early primarily so can determine if headless mode or not
    auto launchconfig = LaunchConfig::create(configPath ?: "");
    launchconfig->applyCmdLineOptions(std::move(configOptions));
    launchconfig->printConfig();

    // preload dependencies
    preLoadWPE(launchconfig->runtimeDir());

    // create the browser instance
    std::unique_ptr<BrowserInterface> browser { createBrowserInstance(launchconfig->runtimeDir()) };

    // create the browser controller
    BrowserController controller(browser.get(), std::move(launchconfig), std::string(url));

    // launch browser on main event loop
    g_signal_connect(application, "activate", G_CALLBACK(+[](GApplication* application, BrowserController* controller) {
        g_application_hold(application);
        controller->launch();
    }), &controller);

    g_object_set_data(G_OBJECT(application), "browsercontroller", &controller);

    // run the event loop
    g_message("starting main event loop");
    int status = g_application_run(application, 0, nullptr);
    g_message("stopped main event loop");

    g_object_set_data(G_OBJECT(application), "browsercontroller", nullptr);

    // terminate the browser instance
    browser->dispose();

    // Dispatch pending tasks(ClosePage, IPC shutdown, etc) scheduled to run after termination
    for(int i = 0; i < 10; ++i)
    {
        if (!g_main_context_iteration(nullptr, FALSE))
            break;
    }

    // cleanup
    g_object_unref(application);
    g_free(url);
    g_free(configPath);

    g_message("done");
    return status;
}
