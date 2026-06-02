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
 #include "asyncbus.h"
 #include "websocketclient.h"
 #include <memory>


struct PageState {
    std::unique_ptr<AsyncBus> messageBus;
    std::unique_ptr<AsyncBus> connectionBus;
    std::string fireboltEndpoint;
    bool connected = false;
    std::unique_ptr<WebSocketClient> wsClient;
};


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

static char* generate_client_id()
{
    guint64 r =
        ((guint64)g_random_int() << 32) |
         (guint64)g_random_int();

    return g_strdup_printf("%016" G_GINT64_MODIFIER "x", r);
}


static PageState* get_page_state(WebKitWebPage* page)
{
    return static_cast<PageState*>(
        g_object_get_data(G_OBJECT(page), "page-state")
    );
}

constexpr int PAGE_STATE_UNAVAILABLE = 1001;
constexpr int INVALID_PARAMETERS = 1002;
constexpr int PAGE_STATE_CLIENT_ID_MISSING = 1003;
constexpr int CLIENT_ID_MISMATCH = 1004;

static JSCValue* create_result(JSCContext* ctx,
                              bool success,
                              int errorCode)
{
    JSCValue* result = jsc_value_new_object(ctx, nullptr, nullptr);

    // success: boolean
    jsc_value_object_set_property(
        result,
        "success",
        jsc_value_new_boolean(ctx, success)
    );

    // errorCode only when failure
    if (!success) {
        jsc_value_object_set_property(
            result,
            "errorCode",
            jsc_value_new_number(ctx, errorCode)
        );
    }

    return result;
}

static JSCValue* connect_cb(JSCContext* ctx,
        JSCValue* function,
        JSCValue* this_val,
        size_t n_params,
        const JSCValue* params[],
        gpointer user_data)
{
    // Native implementation
    // params[] are JS arguments
    g_print("connect called\n");

    auto* state = get_page_state(static_cast<WebKitWebPage*>(user_data));
    // connect using websocket to the firebolt endpoint and set state->connected = true if successful
    // check page state for already connected
    if (state->connected) {
        g_print("Already connected, ignoring connect call\n");
    } else {
        state->wsClient = std::make_unique<WebSocketClient>(state->fireboltEndpoint.c_str());
        state->connected = state->wsClient->Connect(
            // onConnect callback
            [ctx, page=static_cast<WebKitWebPage*>(user_data)](const bool success) {
                auto* state = get_page_state(page);
                if (state) {
                    state->connected = success;
                    g_print("WebSocket connection %s\n", success ? "successful" : "failed");
                    state->connectionBus->emit(success? "connected" : "disconnected");
                }
            },
            // onMessage callback
            [ctx, page=static_cast<WebKitWebPage*>(user_data)](const std::string& message) {
                auto* state = get_page_state(page);
                if (state) {
                    g_print("Received message: %s\n", message.c_str());
                    state->messageBus->emit(message.c_str());
                }
            }
        );
    }

    return create_result(ctx, true, 0);
}

static JSCValue* disconnect_cb(JSCContext* ctx,
        JSCValue* function,
        JSCValue* this_val,
        size_t n_params,
        const JSCValue* params[],
        gpointer user_data)
{
    
    auto* state = get_page_state(static_cast<WebKitWebPage*>(user_data));
    if (state && state->connected && state->wsClient) {
        state->wsClient->Disconnect();
        state->connected = false;
    }

    return create_result(ctx, true, 0);
}

static JSCValue* send_cb(JSCContext* ctx,
        JSCValue* function,
        JSCValue* this_val,
        size_t n_params,
        const JSCValue* params[],
        gpointer user_data)
{
    // Native implementation
    // params[] are JS arguments
    

    if (n_params <1 || !jsc_value_is_string((JSCValue*)params[0])) {
        g_warning("send requires a message string parameter");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }

    auto* state = get_page_state(static_cast<WebKitWebPage*>(user_data));
    if (state && state->connected && state->wsClient) {
        char* jsMessage = jsc_value_to_string((JSCValue*)params[0]);
        g_print("send called with message: %s\n", jsMessage);
        if (jsMessage) {
            state->wsClient->SendMessage(jsMessage);
            g_free(jsMessage);
        }
    }

    return create_result(ctx, true, 0);
}

static JSCValue* on_connection_status_cb(JSCContext* ctx,
              JSCValue*,
              JSCValue*,
              size_t n_params,
              const JSCValue* params[],
              gpointer user_data)
{
    
    auto* state = get_page_state(static_cast<WebKitWebPage*>(user_data));
    if (!state)
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);

    
    if (n_params <1 || !jsc_value_is_function((JSCValue*)params[0])) {
        g_warning("onConnectionStatus requires a callback function parameter");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }
    
    guint id = state->connectionBus->addListener(
            ctx,
            (JSCValue*)params[0]
        );
    
    // unsubscribe()
    auto* unsubscribe_conn_fn = +[](JSCContext* ctx,
                       JSCValue*,
                       JSCValue*,
                       size_t,
                       const JSCValue**,
                       gpointer data) -> JSCValue*
    {
        auto* tuple = static_cast<std::pair<PageState*, guint>*>(data);
        tuple->first->connectionBus->removeListener(tuple->second);
        return create_result(ctx, true, 0);
    };
    return jsc_value_new_function(
        ctx,
        "off",
        G_CALLBACK(unsubscribe_conn_fn),
        new std::pair<PageState*, guint>(state, id),
        [](gpointer p) {
            delete static_cast<std::pair<PageState*, guint>*>(p);
        },
        JSC_TYPE_VALUE,
        0
    );

}

static JSCValue* on_message_cb(JSCContext* ctx,
              JSCValue*,
              JSCValue*,
              size_t n_params,
              const JSCValue* params[],
              gpointer user_data)
{
    auto* page = static_cast<WebKitWebPage*>(user_data);
    auto* state = get_page_state(page);
    if (!state)
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    
    if (n_params <1 || !jsc_value_is_function((JSCValue*)params[0])) {
        g_warning("onMessage requires a callback function parameter");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }
    
    guint id = state->messageBus->addListener(
            ctx,
            (JSCValue*)params[0]
        );
    
    // unsubscribe()
    auto* unsubscribe_msg_fn = +[](JSCContext* ctx,
                       JSCValue*,
                       JSCValue*,
                       size_t,
                       const JSCValue**,
                       gpointer data) -> JSCValue*
    {
        auto* tuple = static_cast<std::pair<PageState*, guint>*>(data);
        tuple->first->messageBus->removeListener(tuple->second);
        return create_result(ctx, true, 0);
    };
    return jsc_value_new_function(
        ctx,
        "off",
        G_CALLBACK(unsubscribe_msg_fn),
        new std::pair<PageState*, guint>(state, id),
        [](gpointer p) {
            delete static_cast<std::pair<PageState*, guint>*>(p);
        },
        JSC_TYPE_VALUE,
        0
    );

}

static bool inject_wpe_firebolt_transport(JSCContext *ctx)
{
    JSCValue *global = jsc_context_get_global_object(ctx);
    JSCValue *serviceManager = jsc_value_object_get_property(global, "FireboltServiceManager");
    g_object_unref(global);

    if (!serviceManager || !jsc_value_is_object(serviceManager)) {
        g_warning("failed to get the FireboltServiceManager object");
        return false;
    }

    // check if FireboltServiceManager has a transport function, if not exit
    JSCValue *serviceManagerTransport = jsc_value_object_get_property(serviceManager, "transport");
    if (!serviceManagerTransport || !jsc_value_is_function(serviceManagerTransport)) {
        g_warning("FireboltServiceManager.transport is not a function");
        if (serviceManagerTransport){
            g_object_unref(serviceManagerTransport);
        }
        g_object_unref(serviceManager);
        return false;
    }
    g_object_unref(serviceManager);


    // Create platform object
    JSCValue *platform = jsc_value_new_object(ctx, NULL, NULL);

    // Create connect() function
    JSCValue *connect_fn = jsc_value_new_function(
        ctx,
        "connect",
        G_CALLBACK(connect_cb),
        NULL,
        NULL,
        JSC_TYPE_VALUE,
        0
    );
    jsc_value_object_set_property(platform, "connect", connect_fn);
    g_object_unref(connect_fn);

    // onConnectionStatus()
    JSCValue *on_conn_status_fn = jsc_value_new_function(
      ctx, "onConnectionStatus", G_CALLBACK(on_connection_status_cb),
      NULL, NULL, JSC_TYPE_VALUE, 0);
    jsc_value_object_set_property(platform, "onConnectionStatus", on_conn_status_fn);
    g_object_unref(on_conn_status_fn);
    

    JSCValue *send_fn = jsc_value_new_function(
        ctx,
        "send",
        G_CALLBACK(send_cb),
        NULL,
        NULL,
        JSC_TYPE_VALUE,
        0
    );
    jsc_value_object_set_property(platform, "send", send_fn);
    g_object_unref(send_fn);

    // onMessage()
    JSCValue *on_message_fn = jsc_value_new_function(
      ctx, "onMessage", G_CALLBACK(on_message_cb),
      NULL, NULL, JSC_TYPE_VALUE, 0);
    jsc_value_object_set_property(platform, "onMessage", on_message_fn);
    g_object_unref(on_message_fn);

    // disconnect() function
    JSCValue *disconnect_fn = jsc_value_new_function(
        ctx,
        "disconnect",
        G_CALLBACK(disconnect_cb),
        NULL,
        NULL,
        JSC_TYPE_VALUE,
        0
    );
    jsc_value_object_set_property(platform, "disconnect", disconnect_fn);
    g_object_unref(disconnect_fn);


    JSCValue *serviceManagerTransportResult = jsc_value_function_call(serviceManagerTransport, JSC_TYPE_VALUE, &platform, G_TYPE_NONE);
    if (!serviceManagerTransportResult) {
        g_warning("failed to call FireboltServiceManager.configure");
        g_object_unref(serviceManagerTransport);
        return false;
    } else {
        g_object_unref(serviceManagerTransportResult);
    }
    
    g_object_unref(serviceManagerTransport);
    g_object_unref(platform);
    g_object_unref(global);

    return true;
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

    GVariant* settings = (GVariant*) userData;

    gchar *fireboltEndpoint = nullptr;
    gchar *fireboltUserScript = nullptr;
    gchar *js_source = nullptr;
    gsize js_len = 0;
    JSCValue *result = nullptr;
    PageState *state = nullptr;

    if (settings) {
        g_variant_lookup(settings, "fireboltEndpoint", "s", &fireboltEndpoint);
        g_variant_lookup(settings, "fireboltUserScript", "s", &fireboltUserScript);
        g_variant_unref(settings); // Unref the settings after using it
    }

    if (!fireboltUserScript || (strlen(fireboltUserScript) == 0))
    {
        g_warning("firebolt extension enabled, but no injected script URL set, "
                   "disabling firebolt bridge support");
        goto cleanup;
    }

    // evaluate the firebolt inject script
    result = jsc_context_evaluate(jsContext, fireboltUserScript, -1);

    if (!result) {
        g_warning("failed to evaluate the injected JS code");
        goto cleanup;
    }
    g_object_unref(result);
    result = nullptr;
    g_free(js_source);
    js_source = nullptr;

    if (!inject_wpe_firebolt_transport(jsContext)) {
        g_warning("failed to inject the transport into the page");
        goto cleanup;
    }
        
    state = new PageState();
    state->messageBus = std::make_unique<AsyncBus>(g_main_context_default());
    state->connectionBus = std::make_unique<AsyncBus>(g_main_context_default());
    state->fireboltEndpoint = fireboltEndpoint;
    g_free(fireboltEndpoint);
    fireboltEndpoint = nullptr;

    state->connected = false;

    g_object_set_data_full(
            G_OBJECT(page),
            "page-state",
            state,
            [](gpointer p) {
                delete static_cast<PageState*>(p);
            }
        );
    
    inject_wpe_firebolt_transport(jsContext);

    cleanup:
    if (jsContext) g_object_unref(jsContext);
    if (js_source) g_free(js_source);
    if (fireboltEndpoint) g_free(fireboltEndpoint);
    if (fireboltUserScript) g_free(fireboltUserScript);
    return;
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