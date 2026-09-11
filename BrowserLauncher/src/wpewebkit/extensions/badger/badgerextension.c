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
#include <wpe/webkit-web-extension.h>

#include "xrebridge.h"
#include "transport.h"

struct BadgerExtensionData
{
    char* fireboltUrl;
};

static void printException(JSCContext* context, JSCException* exception, gpointer data)
{
    g_warning("%s", jsc_exception_get_message(exception));
}

static void createBridgeObject(JSCValue* readyCallback, JSCValue* resultCallback, JSCValue* eventCallback, gpointer userData)
{
    struct BadgerExtensionData *extensionData = (struct BadgerExtensionData*)userData;
    JSCContext *jsContext = jsc_context_get_current();
    jsc_context_push_exception_handler(jsContext, printException, NULL, NULL);
    gboolean result = create_bridge_object(
        jsContext,
        readyCallback,
        resultCallback,
        eventCallback,
        extensionData->fireboltUrl);
    if (!result)
        jsc_context_throw(jsContext, "Couldn't create bridge object.");
    jsc_context_pop_exception_handler(jsContext);
}

static void onWindowObjectCleared(WebKitScriptWorld *world,
                                  WebKitWebPage *page,
                                  WebKitFrame *frame,
                                  gpointer userData)
{
    if (!webkit_frame_is_main_frame(frame))
        return;

    clear_transport();

    GError *error = NULL;
    GBytes *bytes = g_resources_lookup_data("/org/rdk/browser/servicemanager.js", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!bytes)
    {
        g_critical("Failed to load servicemanager.js from resources. Error: %s", error ? error->message : "unknown");
        g_clear_error(&error);
        return;
    }

    gsize sz;
    const void *ptr = g_bytes_get_data(bytes, &sz);
    if (ptr && sz)
    {
        g_debug("Eval %.*s", sz, (const char*)ptr);

        JSCContext* jsContext = webkit_frame_get_js_context_for_script_world(frame, world);
        jsc_context_push_exception_handler(jsContext, printException, NULL, NULL);

        JSCValue* script = jsc_context_evaluate(jsContext, (const char*)(ptr), sz);
        if (!jsc_value_is_function(script))
        {
            g_critical("Cannot inject servicemanager");
        }
        else
        {
            JSCValue* createBridgeObjectFn = jsc_value_new_function(
                jsContext,
                "createBridgeObject",
                G_CALLBACK(createBridgeObject),
                userData,
                NULL,
                G_TYPE_NONE,
                3,
                JSC_TYPE_VALUE,
                JSC_TYPE_VALUE,
                JSC_TYPE_VALUE);
            JSCValue* serviceManager = jsc_value_function_call(script, JSC_TYPE_VALUE, createBridgeObjectFn, G_TYPE_NONE);
            if (jsc_value_is_object(serviceManager))
            {
                jsc_context_set_value(jsContext, "ServiceManager", serviceManager);
            }
            else
            {
                g_critical("Unexpected type of ServiceManager value.");
            }
            g_object_unref(serviceManager);
            g_object_unref(createBridgeObjectFn);
        }
        g_object_unref(script);

        jsc_context_pop_exception_handler(jsContext);
        g_object_unref(jsContext);
    }
    else
    {
        g_critical("empty servicemanager.js");
    }
    g_bytes_unref(bytes);
}

G_BEGIN_DECLS
    // -------------------------------------------------------------------------
    /*!
        Entry point for the WPEWebKit extension.

        \see  https://webkitgtk.org/reference/webkit2gtk/stable/WebKitWebExtension.html

     */
    G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(WebKitWebExtension *extension,
                                                                        GVariant *userData)
    {
        const char* url = g_getenv("FIREBOLT_ENDPOINT") ?: g_getenv("BADGER_FIREBOLT_ENDPOINT") ?: "ws://127.0.0.1:9998/jsonrpc";
        struct BadgerExtensionData *data = g_new0(struct BadgerExtensionData, 1);
        data->fireboltUrl = g_strdup(url);

        g_signal_connect(webkit_script_world_get_default(),
                         "window-object-cleared",
                         G_CALLBACK(onWindowObjectCleared),
                         data);
    }
G_END_DECLS
