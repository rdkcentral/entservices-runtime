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
#include <glib.h>
#include <libsoup/soup.h>

#include <string>
#include <vector>
#include <regex>
#include <set>
#include <algorithm>
#include <memory>

constexpr const char* kEntryPageUrl = "file://" DEFAULT_LOCAL_FILE_DIR "/index.html";

using HeaderList = std::vector<std::pair<std::string, std::string>>;

struct PendingRequest
{
    std::string url;
    HeaderList headers;
};

static std::unique_ptr<PendingRequest> g_state;

static void resetPendingRequest()
{
    g_state.reset();
}

static void throwJSException(JSCContext *ctx, const std::string &msg)
{
    JSCException *exc = jsc_exception_new(ctx, msg.c_str());
    jsc_context_throw_exception(ctx, exc);
    g_object_unref(exc);
}

static bool checkUrl(const std::string &url, std::string &error)
{
    if (url.empty())
    {
        error = "Missing url parameter";
        return false;
    }
    if (!(g_str_has_prefix(url.c_str(), "http://") || g_str_has_prefix(url.c_str(), "https://")))
    {
        error = "Invalid url (must be http(s)://)";
        return false;
    }
    return true;
}

static bool parseHeaders(JSCValue *jsMap, HeaderList &headers, std::string &error)
{
    // fairly restrictive header format rules; using rules from
    // https://developers.cloudflare.com/rules/transform/request-header-modification/reference/header-format/
    static const std::regex nameRegex(R"regex(^[a-zA-Z0-9_-]+$)regex", std::regex::ECMAScript);
    static const std::regex valueRegex(R"regex(^[a-zA-Z0-9_\- \:\;\.\,\\\/\"\'\?\!\(\)\{\}\[\]\@\<\>\=\+\*\#\$\&\`\|\~\^\%]*$)regex", std::regex::ECMAScript);

    static const std::set<std::string> forbiddenHeaders = {
        "accept-charset",
        "accept-encoding",
        "access-control-request-headers",
        "access-control-request-method",
        "connection",
        "content-length",
        "cookie",
        "cookie2",
        "date",
        "dnt",
        "expect",
        "host",
        "keep-alive",
        "origin",
        "referer",
        "set-cookie",
        "te",
        "trailer",
        "transfer-encoding",
        "upgrade",
        "via",
    };

    if (jsc_value_is_null(jsMap))
    {
        return true; // null headers are valid
    }

    if (!jsc_value_is_object(jsMap))
    {
        error = "WebRuntime.LoadUrl headers argument must be a JS object or null";
        return false;
    }

    gchar **properties = jsc_value_object_enumerate_properties(jsMap);
    if (!properties)
    {
        return true; // empty object is allowed
    }

    guint n = g_strv_length(properties);
    for (guint i = 0; i < n; i++)
    {
        const gchar *name = properties[i];

        if (!name || !std::regex_match(name, nameRegex))
        {
            error = "Invalid header name: '" + std::string(name ? name : "<null>") +
                     "'. Only alphanumeric, underscore (_) and dash (-) are allowed";
            break;
        }

        std::string lowerName(name);
        std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);

        if (forbiddenHeaders.count(lowerName) > 0 ||
            lowerName.rfind("proxy-", 0) == 0 ||
            lowerName.rfind("sec-", 0) == 0)
        {
            error = "Forbidden header: '" + std::string(name) +
                     "'. This header cannot be set via WebRuntime.LoadUrl";
            break;
        }

        JSCValue *value = jsc_value_object_get_property(jsMap, name);
        if (!value)
        {
            error = "Header '" + std::string(name) + "' has no value or is invalid";
            break;
        }

        if (!jsc_value_is_string(value))
        {
            error = "Header '" + std::string(name) + "' must be a string value";
            g_object_unref(value);
            break;
        }

        char *valueStr = jsc_value_to_string(value);
        g_object_unref(value);

        if (!valueStr || strlen(valueStr) == 0)
        {
            error = "Header '" + std::string(name) + "' has empty value";
            g_free(valueStr);
            break;
        }

        if (!std::regex_match(valueStr, valueRegex))
        {
            error = "Header '" + std::string(name) + "' has invalid characters in value: '" + valueStr + "'";
            g_free(valueStr);
            break;
        }

        headers.emplace_back(name, valueStr);
        g_free(valueStr);
    }

    g_strfreev(properties);
    return error.empty();
}

static GVariant* parseOptions(JSCValue *options, std::string &error)
{
    if (!options || jsc_value_is_null(options) || !jsc_value_is_object(options))
        return nullptr;

    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    gchar **properties = jsc_value_object_enumerate_properties(options);
    if (!properties)
        return g_variant_builder_end(&builder);

    guint n = g_strv_length(properties);
    bool invalid = false;

    for (guint i = 0; i < n; i++) {
        const gchar *name = properties[i];
        JSCValue *value = jsc_value_object_get_property(options, name);
        if (!value) {
            error = "Invalid options field '" + std::string(name) + "'";
            invalid = true;
            break;
        }

        GVariant *var = nullptr;
        if (jsc_value_is_string(value)) {
            char *str = jsc_value_to_string(value);
            if (!str) {
                error = "Invalid string value for options field '" + std::string(name) + "'";
                g_object_unref(value);
                invalid = true;
                break;
            }
            var = g_variant_new_take_string(str);
        } else if (jsc_value_is_boolean(value)) {
            var = g_variant_new_boolean(jsc_value_to_boolean(value));
        } else if (jsc_value_is_number(value)) {
            var = g_variant_new_double(jsc_value_to_double(value));
        } else if (jsc_value_is_null(value)) {
            var = g_variant_new_boolean(FALSE);
        } else {
            g_warning("Option field '%s' is not a basic JS type, ignoring", name);
        }

        g_object_unref(value);
        if (var)
            g_variant_builder_add(&builder, "{sv}", name, var);
    }

    g_strfreev(properties);

    GVariant *result = g_variant_builder_end(&builder);
    if (invalid) {
        g_variant_unref(result);
        return nullptr;
    }
    return result;
}

static void onWebRuntimeLoad(GPtrArray *args, gpointer userData)
{
    WebKitWebPage *page = WEBKIT_WEB_PAGE(userData);
    JSCContext *ctx = jsc_context_get_current();

    if (args->len < 2)
    {
        throwJSException(ctx, "Usage: WebRuntime.LoadUrl(headers, url, [options])");
        return;
    }

    // Parse headers
    HeaderList headers;
    std::string err;
    if (!parseHeaders(JSC_VALUE(g_ptr_array_index(args, 0)), headers, err))
    {
        throwJSException(ctx, err);
        return;
    }

    // Parse url
    gchar *urlStr = jsc_value_to_string(JSC_VALUE(g_ptr_array_index(args, 1)));
    std::string url = urlStr ? urlStr : "";
    g_free(urlStr);
    if (!checkUrl(url, err))
    {
        throwJSException(ctx, err);
        return;
    }

    // Options (optional)
    GVariant *optionsDict = nullptr;
    if (args->len > 2) {
        if (!(optionsDict = parseOptions(JSC_VALUE(g_ptr_array_index(args, 2)), err))) {
            throwJSException(ctx, err);
            return;
        }
    }

    // Store pending headers that will be applied on the next matching request
    if (!headers.empty()) {
        g_state = std::make_unique<PendingRequest>();
        g_state->url = url;
        g_state->headers = std::move(headers);
    }

    g_info("WebRuntime.LoadUrl: navigating to %s", url.c_str());

    // Send URL and options to the browser UI process to perform the navigation
    GVariant *vars[2] = {
        g_variant_new_string(url.c_str()),
        optionsDict ? optionsDict : g_variant_new_array(G_VARIANT_TYPE("{sv}"), nullptr, 0)
    };

    WebKitUserMessage *message = webkit_user_message_new("WebRuntime.LoadUrl",
                                                         g_variant_new_tuple(vars, 2));
    webkit_web_page_send_message_to_view(page, message, nullptr, nullptr, nullptr);
}

static void onWindowObjectCleared(WebKitScriptWorld *world,
                                  WebKitWebPage *page,
                                  WebKitFrame *frame,
                                  gpointer)
{
    if (!webkit_frame_is_main_frame(frame))
        return;

    resetPendingRequest();

    // Only install the WebRuntime.LoadUrl API on the entry page
    const gchar *uri = webkit_web_page_get_uri(page);
    if (!uri || g_strcmp0(uri, kEntryPageUrl) != 0)
        return;

    JSCContext *ctx = webkit_frame_get_js_context_for_script_world(frame, world);
    JSCValue *obj = jsc_value_new_object(ctx, nullptr, nullptr);
    JSCValue *fn = jsc_value_new_function_variadic(ctx, nullptr,
                                                   G_CALLBACK(onWebRuntimeLoad),
                                                   page, nullptr, G_TYPE_NONE);
    jsc_value_object_set_property(obj, "load", fn);
    jsc_context_set_value(ctx, "WebRuntime", obj);
    g_object_unref(fn);
    g_object_unref(obj);
    g_object_unref(ctx);
}

static gboolean onSendRequest(WebKitWebPage *, WebKitURIRequest *req,
                              WebKitURIResponse *, gpointer)
{
    // apply any pending headers if the URL matches
    if (!g_state)
        return FALSE;

    const gchar *uri = webkit_uri_request_get_uri(req);
    if (!uri)
        return FALSE;

    if (g_state->url == uri)
    {
        SoupMessageHeaders *hdrs = webkit_uri_request_get_http_headers(req);
        for (auto &h : g_state->headers) {
            g_info("WebRuntime.LoadUrl: adding header '%s: %s' for URL %s",
                   h.first.c_str(), h.second.c_str(), uri);
            soup_message_headers_replace(hdrs, h.first.c_str(), h.second.c_str());
        }

        // clear the pending request
        resetPendingRequest();
    }
    return FALSE;
}

static void onPageCreated(WebKitWebExtension *, WebKitWebPage *page, gpointer)
{
    g_signal_connect(page, "send-request", G_CALLBACK(onSendRequest), nullptr);
}

extern "C"
{
    G_MODULE_EXPORT void
    webkit_web_extension_initialize_with_user_data(WebKitWebExtension *ext,
                                                   GVariant *userData)
    {
        // check 'enable' setting
        GVariant *settings = g_variant_lookup_value(userData, "webruntime", G_VARIANT_TYPE_VARDICT);
        if (!settings)
        {
            g_warning("missing webruntimeload extension settings, disabling extension");
            return;
        }
        gboolean enabled = FALSE;
        g_variant_lookup(settings, "enable", "b", &enabled);
        g_variant_unref(settings);
        if (!enabled)
        {
            g_info("webruntimeload extension disabled via settings");
            return;
        }
        g_signal_connect(webkit_script_world_get_default(),
                         "window-object-cleared",
                         G_CALLBACK(onWindowObjectCleared), nullptr);
        g_signal_connect(ext, "page-created", G_CALLBACK(onPageCreated), nullptr);
        g_info("webruntimeload extension initialized");
    }
}
