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

/* New WPEWebKit 2.46 APIs that are not present in previous versions.
 *
 * All functions are weak symbols to be replaced by actual implementation in runtime
 * to support previous WebKit versions (without those symbols).
 *
 * Use WpeWebKitUtils::webkitVersion() >= VersionNumber(2, 46, 0) to check
 * current WebKit version (at runtime) before use.
 *
 * Because of "C" linkage no params overload possible.
 */

extern "C" {

/* WebKitMemoryPressureSettings */
void webkit_web_view_set_local_universal_access_allowlist(WebKitWebView* webView, const gchar* const* allowList)
    __attribute__((weak));


void webkit_web_view_evaluate_javascript(WebKitWebView* web_view, const char* script, gssize length, const char* world_name,
                                         const char* source_uri, GCancellable* cancellable, GAsyncReadyCallback callback,
                                         gpointer user_data)
    __attribute__((weak));

JSCValue* webkit_web_view_evaluate_javascript_finish(WebKitWebView* web_view, GAsyncResult* result, GError** error)
    __attribute__((weak));

}
