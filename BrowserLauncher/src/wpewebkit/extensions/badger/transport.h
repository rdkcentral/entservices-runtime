#pragma once

#include <jsc/jsc.h>

G_BEGIN_DECLS

JSCValue* create_transport(JSCContext* jsContext, const char* url);
void clear_transport();

G_END_DECLS
