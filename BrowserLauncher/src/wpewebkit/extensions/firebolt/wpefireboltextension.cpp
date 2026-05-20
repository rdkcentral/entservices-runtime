/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2026 RDK Management.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
 #include <wpe/webkit-web-extension.h>
 #include <cstring>
 #include <cstdarg>
 #include <optional>
 #include <string>
 #include <glib.h>


static gboolean read_file_to_string(const char* path, gchar** out, gsize* out_len)
{
    GError* err = NULL;
    if (!g_file_get_contents(path, out, out_len, &err)) {
        g_warning("JS read failed (%s): %s", path, err ? err->message : "unknown");
        if (err) g_error_free(err);
        return FALSE;
    }
    return TRUE;
}

// -----------------------------------------------------------------------------
/*!
    \internal

    Called when the JS code calls 'window.ServiceManager.getService('firebolt')'.

 */
static JSCValue* getFireboltObject(gpointer userData)
{
    auto page = reinterpret_cast<WebKitWebPage*>(userData);

    // get the firebolt endpoint url from the page's data
    gpointer fireboltEndpointPtr = g_object_get_data(G_OBJECT(page), "fireboltEndpoint");
    if (!fireboltEndpointPtr) {
        g_warning("Firebolt endpoint not found in page data");
        return nullptr;
    }

    
}


// -----------------------------------------------------------------------------
/*!
    \internal

    (An) Entry point of the extension.

 */
static void onWindowObjectCleared(WebKitScriptWorld *world,
                                  WebKitWebPage *page,
                                  WebKitFrame *frame,
                                  gpointer userData)
{
    // We only want to inject our JS code into the main frame, not into iframes
    if (webkit_frame_is_main_frame(frame) == FALSE)
        return;

    JSCContext *jsContext = webkit_frame_get_js_context_for_script_world(frame, world);
    if (!jsContext)
    {
        g_warning("failed to get the JS context");
        return;
    }

    GVariant* settings = (GVariant*) user_data;

    gchar *fireboltEndpoint = nullptr;
    gchar *fireboltUserScript = nullptr;
    
    if (settings) {
        g_variant_lookup(settings, "fireboltEndpoint", "s", &fireboltEndpoint);
        g_variant_lookup(settings, "fireboltUserScript", "s", &fireboltUserScript);
        g_variant_unref(settings); // Unref the settings after using it
    }

    if (!fireboltUserScript || (strlen(fireboltUserScript) == 0))
    {
        g_warning("firebolt extension enabled, but no injected script URL set, "
                   "disabling firebolt bridge support");
    }
    else
    {
        // inject a javascript file into the page
        g_print("Injecting Firebolt bridge script from URL: %s\n", fireboltUserScript);
        
        gchar* js_source = NULL;
        gsize js_len = 0;

        if (!read_file_to_string(fireboltUserScript, &js_source, &js_len)) {
            return;
        } else {
            JSCValue* result = jsc_context_evaluate(jsContext, js_source, js_len, fireboltUserScript);
            if (!result) {
                g_warning("failed to evaluate the injected JS code");
            } else {
                g_print("Successfully injected Firebolt bridge script.\n");
            }
            g_object_unref(result);
            g_free(js_source);

            // store the firebolt endpoint url in the page's data, so we can access it later when needed
            g_object_set_data(G_OBJECT(page), "fireboltEndpoint", g_strdup(fireboltEndpoint));
            
            // create the binding for ServiceManager, so the injected JS code can call into it to get the service URLs
            JSCValue* *serviceManager = jsc_context_get_value(jsContext, "ServiceManager");
            if (!serviceManager) {
                // service manager doesn't exist yet, log error and return
                g_warning("ServiceManager object not found in JS context, creating it.");
                return;
            } else {
                JSCValue* *getFireboltObjectCallback = jsc_value_new_function(jsContext, nullptr, G_CALLBACK(getFireboltObject), page, nullptr, JSC_TYPE_VALUE,0,G_TYPE_NONE);
                jsc_value_object_set_property(jsObject, "getFireboltObject", getFireboltObjectCallback);
                g_object_unref(getFireboltObjectCallback);
                g_print("Successfully set up ServiceManager.getFireboltObject binding.\n");
            }
            g_object_unref(serviceManager);
        }
    }
}


 extern "C"
{
    // -------------------------------------------------------------------------
    /*!
        Entry point for the WPEWebKit extension.

        \see  https://webkitgtk.org/reference/webkit2gtk/stable/WebKitWebExtension.html

     */
    G_MODULE_EXPORT void webkit_web_extension_initialize_with_user_data(WebKitWebExtension *extension,
                                                                        GVariant *userData)
    {
        gboolean enabled = FALSE;
        gchar *fireboltEndpoint = nullptr;
        gchar *fireboltUserScript = nullptr;
        // check if the firebolt extension should be enabled and if so get the firebolt endpoint url
        GVariant *settings = g_variant_lookup_value(userData, "firebolt", G_VARIANT_TYPE_VARDICT);

        if (settings) {
            g_variant_lookup(settings, "wpeFireboltEnabled", "b", &enabled);
            if (enabled) {
                g_variant_lookup(settings, "fireboltEndpoint", "s", &fireboltEndpoint);
                g_variant_lookup(settings, "fireboltUserScript", "s", &fireboltUserScript);
            }
        }

        if (enabled && fireboltEndpoint && fireboltUserScript) {
            g_variant_ref(settings); // Ref the settings so we can pass it to the callback
            g_print("WPE Firebolt Extension enabled with Firebolt Endpoint: %s\n", fireboltEndpoint);
            // Here you would initialize your extension's functionality, e.g., set up IPC, hooks, etc.
            // hook the following signal, so we can inject JS code into the page
            g_signal_connect(webkit_script_world_get_default(),
                             "window-object-cleared",
                             G_CALLBACK(onWindowObjectCleared),
                             settings);
            g_variant_unref(settings); // Unref the settings after connecting the signal

        } else {
            g_print("WPE Firebolt Extension is disabled or missing configuration.\n");
        }
    }
}