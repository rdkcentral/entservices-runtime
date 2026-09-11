#include "xrebridge.h"
#include "transport.h"

#include <gio/gio.h>

G_BEGIN_DECLS

gboolean create_bridge_object(
  JSCContext* jsContext,
  JSCValue* readyCallback,
  JSCValue* resultCallback,
  JSCValue* eventCallback,
  const char* url)
{
    GError*  error  = NULL;
    GBytes*  bytes  = g_resources_lookup_data(
      "/org/rdk/browser/xrebridge.js", G_RESOURCE_LOOKUP_FLAGS_NONE, &error);
    if (!bytes || error)
    {
      g_critical("Failed to load xrebridge.js from resources. Error: %s", error ? error->message : "unknown");
      g_clear_error(&error);
      return FALSE;
    }

    gsize sz;
    const void *ptr = g_bytes_get_data(bytes, &sz);
    if (!ptr || !sz)
    {
      g_bytes_unref(bytes);
      return FALSE;
    }

    g_debug("Eval %.*s", sz, (const char*)ptr);
    JSCValue *script = jsc_context_evaluate(jsContext, (const char*)(ptr), sz);
    if (!jsc_value_is_function(script) || jsc_context_get_exception(jsContext))
    {
      g_critical("Cannot inject xrebridge.js");
    }
    else
    {
      g_message("Url: %s", url);
      JSCValue* transport = create_transport(jsContext, url);
      JSCValue* tmp = jsc_value_function_call(
        script,
        JSC_TYPE_VALUE, transport,
        JSC_TYPE_VALUE, readyCallback,
        JSC_TYPE_VALUE, resultCallback,
        JSC_TYPE_VALUE, eventCallback,
        G_TYPE_NONE);
      g_object_unref(tmp);
      g_object_unref(transport);
    }
    g_object_unref(script);
    g_bytes_unref(bytes);
    return TRUE;
}

G_END_DECLS
