#pragma once

#include <jsc/jsc.h>

G_BEGIN_DECLS

gboolean create_bridge_object(
  JSCContext* jsContext,
  JSCValue* readyCallback,
  JSCValue* resultCallback,
  JSCValue* eventCallback,
  const char* url);

G_END_DECLS
