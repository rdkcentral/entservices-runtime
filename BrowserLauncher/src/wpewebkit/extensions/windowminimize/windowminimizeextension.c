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

static void onWindowMinimize(GPtrArray *args, gpointer userData)
{
    WebKitWebPage *page = WEBKIT_WEB_PAGE(userData);
    g_info("window.minimize: %s", webkit_web_page_get_uri(page));

    WebKitUserMessage *message = webkit_user_message_new("Window.minimize", NULL);
    webkit_web_page_send_message_to_view(page, message, NULL, NULL, NULL);
}

static void onWindowObjectCleared(WebKitScriptWorld *world,
                                  WebKitWebPage *page,
                                  WebKitFrame *frame,
                                  gpointer userData)
{
    if (!webkit_frame_is_main_frame(frame))
        return;

    JSCContext *ctx = webkit_frame_get_js_context_for_script_world(frame, world);
    JSCValue *fn = jsc_value_new_function_variadic(ctx, NULL,
                                                   G_CALLBACK(onWindowMinimize),
                                                   page, NULL, G_TYPE_NONE);
    jsc_context_set_value(ctx, "minimize", fn);
    g_object_unref(fn);
    g_object_unref(ctx);
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
        g_signal_connect(webkit_script_world_get_default(),
                         "window-object-cleared",
                         G_CALLBACK(onWindowObjectCleared), NULL);
    }
G_END_DECLS
