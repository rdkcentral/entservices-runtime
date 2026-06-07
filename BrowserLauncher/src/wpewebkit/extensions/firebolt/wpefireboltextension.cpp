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
 #include <mutex>


struct PageState {
    static constexpr uint32_t MAGIC = 0xDEADBEEF;
    
    uint32_t magic = MAGIC;
    std::unique_ptr<AsyncBus> messageBus;
    std::unique_ptr<AsyncBus> connectionBus;
    std::string fireboltEndpoint;
    bool connected = false;
    std::unique_ptr<WebSocketClient> wsClient;
};

// Single global page state - only one active page at a time in WebKit extension
static std::shared_ptr<PageState> g_current_page_state;
static std::mutex g_state_mutex;


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

constexpr int PAGE_STATE_UNAVAILABLE = 1001;
const char* INVALID_STATE_ERROR = "Invalid PageState pointer (magic number mismatch)";

static std::shared_ptr<PageState> validate_page_state(gpointer user_data)
{
    // Simply return the current global page state
    // The page pointer is just used for callback routing, not for lookup
    std::lock_guard<std::mutex> lock(g_state_mutex);
    return g_current_page_state;
}
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

    auto shared_state = validate_page_state(user_data);
    if (!shared_state) {
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    if (shared_state->magic != PageState::MAGIC) {
        g_warning("connect_cb: invalid state magic number");
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }

    // connect using websocket to the firebolt endpoint and set state->connected = true if successful
    // check page state for already connected
    if (shared_state->connected) {
        g_print("Already connected, ignoring connect call\n");
    } else {
        g_print("Connecting to Firebolt endpoint: %s\n", shared_state->fireboltEndpoint.c_str());
        shared_state->wsClient = std::make_unique<WebSocketClient>(shared_state->fireboltEndpoint.c_str());
        g_print("WebSocket client created, attempting to connect...\n");
        shared_state->connected = shared_state->wsClient->Connect(
            // onConnect callback
            [ctx, shared_state](const bool success) {
                if (shared_state && shared_state->magic == PageState::MAGIC) {
                    shared_state->connected = success;
                    g_print("WebSocket connection %s\n", success ? "successful" : "failed");
                    shared_state->connectionBus->emit(success? "connected" : "disconnected");
                } else {
                    g_warning("onConnect: invalid state object");
                }
            },
            // onMessage callback
            [ctx, shared_state](const std::string& message) {
                if (shared_state && shared_state->magic == PageState::MAGIC) {
                    g_print("Received message: %s\n", message.c_str());
                    shared_state->messageBus->emit(message.c_str());
                } else {
                    g_warning("onMessage: invalid state object");
                }
            }
        );
        g_print("WebSocket Connect method returned, connection state: %s\n", shared_state->connected ? "connected" : "not connected");
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
    g_print("disconnect called\n");
    auto shared_state = validate_page_state(user_data);
    if (!shared_state) {
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    if (shared_state->magic != PageState::MAGIC) {
        g_warning("disconnect_cb: invalid state magic number");
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    g_print("Page state obtained for disconnect\n");
    if (shared_state->connected && shared_state->wsClient) {
        shared_state->wsClient->Disconnect();
        shared_state->connected = false;
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
    g_print("send called\n");

    if (n_params < 1) {
        g_warning("send requires a message string parameter");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }
    if (!jsc_value_is_string(const_cast<JSCValue*>(params[0]))) {
        g_warning("send parameter is not a string");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }
    g_print("send parameter is valid\n");

    auto shared_state = validate_page_state(user_data);
    if (!shared_state) {
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    if (shared_state->magic != PageState::MAGIC) {
        g_warning("send_cb: invalid state magic number");
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    if (shared_state->connected && shared_state->wsClient) {
        char* jsMessage = jsc_value_to_string(const_cast<JSCValue*>(params[0]));
        g_print("send called with message: %s\n", jsMessage);
        if (jsMessage) {
            shared_state->wsClient->SendMessage(jsMessage);
            g_free(jsMessage);
        }
    } else {
        if (!shared_state->connected) {
            g_warning("send called but not connected to Firebolt endpoint");
        } else {
            g_warning("send called but WebSocket client is not available");
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
    g_print("onConnectionStatus callback called\n");
    auto shared_state = validate_page_state(user_data);
    if (!shared_state) {
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    if (shared_state->magic != PageState::MAGIC) {
        g_warning("on_connection_status_cb: invalid state magic number");
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    }
    if (n_params < 1) {
        g_warning("onConnectionStatus requires a callback function parameter");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }
    if (!jsc_value_is_function(const_cast<JSCValue*>(params[0]))) {
        g_warning("onConnectionStatus parameter is not a function");
        return create_result(ctx, false, INVALID_PARAMETERS);
    }
    g_print("Callback function parameter is valid\n");
    guint id = shared_state->connectionBus->addListener(
            ctx,
            params[0]
        );
    g_print("Listener added to connection bus with id: %u\n", id);
    // unsubscribe()
    auto* unsubscribe_conn_fn = +[](JSCContext* ctx,
                       JSCValue*,
                       JSCValue*,
                       size_t,
                       const JSCValue**,
                       gpointer data) -> JSCValue*
    {
        auto* pair_data = static_cast<std::pair<std::shared_ptr<PageState>, guint>*>(data);
        if (pair_data->first) {  // shared_ptr keeps PageState alive
            pair_data->first->connectionBus->removeListener(pair_data->second);
            g_print("Listener removed from connection bus with id: %u\n", pair_data->second);
        }
        return create_result(ctx, true, 0);
    };
    return jsc_value_new_function(
        ctx,
        "off",
        G_CALLBACK(unsubscribe_conn_fn),
        new std::pair<std::shared_ptr<PageState>, guint>(shared_state, id),
        [](gpointer p) {
            delete static_cast<std::pair<std::shared_ptr<PageState>, guint>*>(p);
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
    g_print("onMessage callback called\n");
    auto shared_state = validate_page_state(user_data);
    if (!shared_state) {
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    } else {
        g_print("Page state validated successfully in onMessage callback\n");
    }

    if (shared_state->magic != PageState::MAGIC) {
        g_warning("on_message_cb: invalid state magic number");
        return create_result(ctx, false, PAGE_STATE_UNAVAILABLE);
    } else {
        g_print("Page state magic number validated successfully in onMessage callback\n");
    }
    
    if (n_params < 1) {
        g_warning("onMessage requires a callback function parameter");
        return create_result(ctx, false, INVALID_PARAMETERS);
    } else {
        g_print("onMessage callback parameter count is valid: %zu\n", n_params);
    }

    if (!params) {
        g_warning("onMessage callback parameters array is null");
        return create_result(ctx, false, INVALID_PARAMETERS);
    } else {
        g_print("onMessage callback parameters array is not null\n");
    }

    if (!params[0]) {
        g_warning("onMessage callback parameter is null");
        return create_result(ctx, false, INVALID_PARAMETERS);
    } else {
        g_print("onMessage callback parameter is not null\n");
    }
    
    if (!jsc_value_is_function(const_cast<JSCValue*>(params[0]))) {
        g_warning("onMessage parameter is not a function");
        return create_result(ctx, false, INVALID_PARAMETERS);
    } else {
        g_print("Callback function parameter is valid in onMessage callback\n");
    }
    
    guint id = shared_state->messageBus->addListener(
            ctx,
            params[0]
        );

    g_print("Listener added to message bus with id: %u\n", id);
    
    // unsubscribe()
    auto* unsubscribe_msg_fn = +[](JSCContext* ctx,
                       JSCValue*,
                       JSCValue*,
                       size_t,
                       const JSCValue**,
                       gpointer data) -> JSCValue*
    {
        auto* pair_data = static_cast<std::pair<std::shared_ptr<PageState>, guint>*>(data);
        if (pair_data->first) {  // shared_ptr keeps PageState alive
            pair_data->first->messageBus->removeListener(pair_data->second);
        }
        return create_result(ctx, true, 0);
    };
    g_print("Creating unsubscribe function for message bus listener with id: %u\n", id);
    return jsc_value_new_function(
        ctx,
        "off",
        G_CALLBACK(unsubscribe_msg_fn),
        new std::pair<std::shared_ptr<PageState>, guint>(shared_state, id),
        [](gpointer p) {
            delete static_cast<std::pair<std::shared_ptr<PageState>, guint>*>(p);
        },
        JSC_TYPE_VALUE,
        0
    );

}

static bool inject_wpe_firebolt_transport(JSCContext *ctx, WebKitWebPage* page, std::shared_ptr<PageState> state)
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

    // Pass page pointer as user_data (state ID will be retrieved from page object)
    auto page_ptr = reinterpret_cast<gpointer>(page);

    // Create connect() function - pass page pointer as user_data
    JSCValue *connect_fn = jsc_value_new_function(
        ctx,
        "connect",
        G_CALLBACK(connect_cb),
        page_ptr,
        nullptr,  // No destructor needed since we're not allocating
        JSC_TYPE_VALUE,
        0
    );
    jsc_value_object_set_property(platform, "connect", connect_fn);
    g_object_unref(connect_fn);

    // onConnectionStatus() - pass page pointer as user_data
    JSCValue *on_conn_status_fn = jsc_value_new_function(
      ctx, "onConnectionStatus", G_CALLBACK(on_connection_status_cb),
      page_ptr,
      nullptr,
      JSC_TYPE_VALUE, 0);
    jsc_value_object_set_property(platform, "onConnectionStatus", on_conn_status_fn);
    g_object_unref(on_conn_status_fn);
    

    JSCValue *send_fn = jsc_value_new_function(
        ctx,
        "send",
        G_CALLBACK(send_cb),
        page_ptr,
        nullptr,
        JSC_TYPE_VALUE,
        0
    );
    jsc_value_object_set_property(platform, "send", send_fn);
    g_object_unref(send_fn);

    // onMessage() - pass page pointer as user_data
    JSCValue *on_message_fn = jsc_value_new_function(
      ctx, "onMessage", G_CALLBACK(on_message_cb),
      page_ptr,
      nullptr,
      JSC_TYPE_VALUE, 0);
    jsc_value_object_set_property(platform, "onMessage", on_message_fn);
    g_object_unref(on_message_fn);

    // disconnect() function - pass page pointer as user_data
    JSCValue *disconnect_fn = jsc_value_new_function(
        ctx,
        "disconnect",
        G_CALLBACK(disconnect_cb),
        page_ptr,
        nullptr,
        JSC_TYPE_VALUE,
        0
    );
    jsc_value_object_set_property(platform, "disconnect", disconnect_fn);
    g_object_unref(disconnect_fn);


    bool finalResult = false;
    JSCValue *serviceManagerTransportResult = jsc_value_function_call(serviceManagerTransport, JSC_TYPE_VALUE, platform, G_TYPE_NONE);
    if (!serviceManagerTransportResult) {
        g_warning("failed to call FireboltServiceManager.transport");
    } else {
        g_print("Firebolt transport injected successfully\n");
        g_object_unref(serviceManagerTransportResult);
        finalResult = true;
    }

    if (serviceManagerTransport) g_object_unref(serviceManagerTransport);
    if (platform) g_object_unref(platform);

    return finalResult;
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
    g_print("onWindowObjectCleared called for frame\n");
    // We only want to inject our JS code into the main frame, not into iframes
    if (webkit_frame_is_main_frame(frame) == FALSE)
        return;
    g_print("onWindowObjectCleared is injecting into main frame\n");

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
    std::shared_ptr<PageState> state;

    if (settings) {
        g_variant_lookup(settings, "fireboltEndpoint", "s", &fireboltEndpoint);
        g_variant_lookup(settings, "fireboltUserScript", "s", &fireboltUserScript);
        g_variant_unref(settings); // Unref the settings after using it
    } else {
        g_warning("no settings found for firebolt extension");
        goto cleanup;
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

    state = std::make_shared<PageState>();
    state->messageBus = std::make_unique<AsyncBus>(g_main_context_default());
    state->connectionBus = std::make_unique<AsyncBus>(g_main_context_default());
    state->fireboltEndpoint = fireboltEndpoint;
    g_free(fireboltEndpoint);
    fireboltEndpoint = nullptr;
    state->connected = false;

    // Scope block to avoid 'goto cleanup' crossing initialization
    {
        // Create new page state and atomically replace the old one
        // Old state auto-deletes when shared_ptr refcount drops to 0
        {
            std::lock_guard<std::mutex> lock(g_state_mutex);
            g_current_page_state = state;
            g_print("Replaced page state with new instance\n");
        }

        if (!inject_wpe_firebolt_transport(jsContext, page, state)) {
            g_warning("failed to inject the transport into the page");
            // Clear state on failure
            {
                std::lock_guard<std::mutex> lock(g_state_mutex);
                g_current_page_state = nullptr;
            }
            goto cleanup;
        }
    }

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
        g_print("Initializing WPE Firebolt Extension\n");
        gboolean enabled = FALSE;
        gchar *fireboltEndpoint = nullptr;
        gchar *fireboltUserScript = nullptr;
        // check if the firebolt extension should be enabled and if so get the firebolt endpoint url
        GVariant *settings = g_variant_lookup_value(userData, "firebolt", G_VARIANT_TYPE_VARDICT);

        if (settings) {
            g_print("Firebolt extension settings found\n");
            g_variant_lookup(settings, "wpeFireboltEnabled", "b", &enabled);
            if (enabled) {
                g_variant_lookup(settings, "fireboltEndpoint", "s", &fireboltEndpoint);
                g_variant_lookup(settings, "fireboltUserScript", "s", &fireboltUserScript);

                if (fireboltEndpoint && fireboltUserScript) {
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
                    if (!fireboltEndpoint) g_print("WPE Firebolt Extension enabled but no Firebolt Endpoint set.\n");
                    if (!fireboltUserScript) g_print("WPE Firebolt Extension enabled but no injected script URL set.\n");
                }

                if (fireboltEndpoint) g_free(fireboltEndpoint);
                if (fireboltUserScript) g_free(fireboltUserScript);
            }
        } else {
            g_print("No firebolt extension settings found, extension will be disabled\n");
        }
    }
}