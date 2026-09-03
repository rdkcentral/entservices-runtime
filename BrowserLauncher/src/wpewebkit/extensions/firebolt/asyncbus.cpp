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
 #include "asyncbus.h"

struct EmitData {
    AsyncBus* bus;
    char* payload;
};

static gboolean deliver_cb(gpointer data)
{
    auto* d = static_cast<EmitData*>(data);
    d->bus->deliverOnJsThread(d->payload);
    g_free(d->payload);
    delete d;
    return G_SOURCE_REMOVE;
}

AsyncBus::AsyncBus(GMainContext* jsContext)
    : m_lock()
    , m_listeners()
    , m_nextId(1)
    , m_jsContext(jsContext
        ? g_main_context_ref(jsContext)
        : g_main_context_ref(g_main_context_default()))
{
}

AsyncBus::~AsyncBus()
{
    cleanup();
}

void AsyncBus::cleanup()
{
    std::lock_guard<std::mutex> lock(m_lock);
    for (auto& [id, l] : m_listeners) {
        g_object_unref(l.ctx);
        g_object_unref(l.cb);
    }
    m_listeners.clear();
    g_main_context_unref(m_jsContext);
}

guint AsyncBus::addListener(JSCContext* ctx, JSCValue* cb)
{
    g_print("Adding listener to AsyncBus\n");
    std::lock_guard<std::mutex> lock(m_lock);
    guint id = m_nextId++;
    m_listeners.emplace(id, Listener{
        g_object_ref(ctx),
        g_object_ref(cb)
    });
    g_print("Listener added to AsyncBus with id: %u\n", id);
    return id;
}

void AsyncBus::removeListener(guint id)
{
    std::lock_guard<std::mutex> lock(m_lock);
    auto it = m_listeners.find(id);
    if (it != m_listeners.end()) {
        g_object_unref(it->second.ctx);
        g_object_unref(it->second.cb);
        m_listeners.erase(it);
    }
}

void AsyncBus::emit(const char* payload)
{
    auto* data = new EmitData{
        this,
        g_strdup(payload)
    };

    g_main_context_invoke(m_jsContext, deliver_cb, data);
}

void AsyncBus::deliverOnJsThread(char* payload)
{
    std::vector<Listener> snapshot;

    {
        std::lock_guard<std::mutex> lock(m_lock);
        for (auto& [id, l] : m_listeners) {
            snapshot.push_back({
                g_object_ref(l.ctx),
                g_object_ref(l.cb)
            });
        }
    }

    // Invoke callbacks without holding lock
    for (auto& l : snapshot) {
        JSCValue* arg = jsc_value_new_string(l.ctx, payload);
        JSCValue* ret = jsc_value_function_call(
            l.cb,
            JSC_TYPE_VALUE, arg,
            G_TYPE_NONE
        );

        if (ret) g_object_unref(ret);
        g_object_unref(arg);
        g_object_unref(l.cb);
        g_object_unref(l.ctx);
    }
}