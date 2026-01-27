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

/* New WPEWebKit 2.38 APIs that are not present in previous versions.
 *
 * All functions are weak symbols to be replaced by actual implementation in runtime
 * to support previous WebKit versions (without those symbols).
 *
 * Use WpeWebKitUtils::webkitVersion() >= VersionNumber(2, 38, 0) to check
 * current WebKit version (at runtime) before use.
 *
 * Because of "C" linkage no params overload possible.
 */

extern "C" {

/* WebKitMemoryPressureSettings */
typedef struct _WebKitMemoryPressureSettings WebKitMemoryPressureSettings;

WebKitMemoryPressureSettings* webkit_memory_pressure_settings_new(void)
    __attribute__((weak));

void webkit_memory_pressure_settings_free(WebKitMemoryPressureSettings *settings)
    __attribute__((weak));

void webkit_memory_pressure_settings_set_memory_limit(WebKitMemoryPressureSettings *settings,
                                                      guint memory_limit)
    __attribute__((weak));

void webkit_memory_pressure_settings_set_poll_interval(WebKitMemoryPressureSettings *settings,
                                                       gdouble value)
    __attribute__((weak));


/* WebKitWebsiteDataManager */
void webkit_website_data_manager_set_memory_pressure_settings(WebKitMemoryPressureSettings *settings)
    __attribute__((weak));


/* WebKitPreferences */
void webkit_settings_set_enable_webrtc(WebKitSettings*, gboolean)
    __attribute__((weak));
void webkit_settings_set_enable_ice_candidate_filtering(WebKitSettings*, gboolean)
    __attribute__((weak));


/* WebKitWebView */
void webkit_web_view_send_memory_pressure_event(WebKitWebView*, gboolean)
    __attribute__((weak));


/* WebKitURISchemeResponse */
typedef struct _WebKitURISchemeResponse WebKitURISchemeResponse;

WebKitURISchemeResponse* webkit_uri_scheme_response_new(GInputStream* input_stream,
                                                        gint64 stream_length)
    __attribute__((weak));

void webkit_uri_scheme_response_set_http_headers(WebKitURISchemeResponse *response,
                                                 SoupMessageHeaders *headers)
    __attribute__((weak));

void webkit_uri_scheme_response_set_status(WebKitURISchemeResponse* response,
                                           guint status_code,
                                           const gchar* reason_phrase)
    __attribute__((weak));

void webkit_uri_scheme_response_set_content_type(WebKitURISchemeResponse* response,
                                                 const gchar* content_type)
    __attribute__((weak));

void webkit_uri_scheme_request_finish_with_response(WebKitURISchemeRequest* request,
                                                    WebKitURISchemeResponse* response)
    __attribute__((weak));


/* Page Lifecyle API */
gboolean
webkit_web_view_hide_plc                             (WebKitWebView             *web_view,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data)
    __attribute__((weak));

gboolean
webkit_web_view_hide_plc_finish                      (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error)
    __attribute__((weak));

gboolean
webkit_web_view_show_plc                             (WebKitWebView             *web_view,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data)
    __attribute__((weak));

gboolean
webkit_web_view_show_plc_finish                      (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error)
    __attribute__((weak));

gboolean
webkit_web_view_focus_plc                            (WebKitWebView             *web_view,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data)
    __attribute__((weak));


gboolean
webkit_web_view_focus_plc_finish                     (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error)
    __attribute__((weak));

gboolean
webkit_web_view_blur_plc                             (WebKitWebView             *web_view,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data)
    __attribute__((weak));

gboolean
webkit_web_view_blur_plc_finish                      (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error)
    __attribute__((weak));

gboolean
webkit_web_view_freeze_plc                           (WebKitWebView             *web_view,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data)
    __attribute__((weak));

gboolean
webkit_web_view_freeze_plc_finish                    (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error)
    __attribute__((weak));

gboolean
webkit_web_view_resume_plc                           (WebKitWebView             *web_view,
                                                      GAsyncReadyCallback       callback,
                                                      gpointer                  user_data)
    __attribute__((weak));

gboolean
webkit_web_view_resume_plc_finish                    (WebKitWebView             *web_view,
                                                      GAsyncResult              *result,
                                                      GError                    **error)
    __attribute__((weak));

}
