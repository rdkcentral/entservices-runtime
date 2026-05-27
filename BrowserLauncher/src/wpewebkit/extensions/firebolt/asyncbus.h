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
 
 #pragma once

#include <glib.h>
#include <jsc/jsc.h>
#include <mutex>
#include <unordered_map>
#include <vector>

class AsyncBus {
public:
    struct Listener {
        JSCContext* ctx;
        JSCValue*   cb;
    };

    explicit AsyncBus(GMainContext* jsContext);
    ~AsyncBus();

    guint addListener(JSCContext* ctx, JSCValue* cb);
    void  removeListener(guint id);

    // Safe from any thread
    void emit(const char* payload);

private:
    void deliverOnJsThread(char* payload);

    std::mutex m_lock;
    std::unordered_map<guint, Listener> m_listeners;
    guint m_nextId {1};

    GMainContext* m_jsContext;
};