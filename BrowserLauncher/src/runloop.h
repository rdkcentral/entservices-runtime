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

#include <glib.h>
#include <gio/gio.h>

#include <functional>
#include <optional>
#include <memory>

// A helper for dispatching c++ callable objects on glib's main loop
class RunLoop
{
protected:
    GMainContext *m_mainContext { nullptr };
    std::shared_ptr<char> m_token { std::make_shared<char> (37) };

public:
    RunLoop()
        : m_mainContext (g_main_context_ref_thread_default())
    {
    }

    explicit RunLoop(GMainContext *context)
        : m_mainContext (g_main_context_ref(context))
    {
    }

    ~RunLoop()
    {
        g_main_context_unref (m_mainContext);
    }

    void InvokeTask(std::function<void()> && fn, const std::optional<uint32_t> delay = { })
    {
        if (!m_token)
        {
            return;
        }
        if (g_main_context_is_owner (m_mainContext) && !delay.has_value())
        {
            fn();
            return;
        }
        struct CallContext
        {
            std::function<void()> fn;
            std::weak_ptr<char> token_ptr;
        };
        GSource *source = g_timeout_source_new (delay.value_or(0));
        g_source_set_callback (
            source,
            G_SOURCE_FUNC(+[](CallContext* ctx) {
                if (auto token = ctx->token_ptr.lock())
                    ctx->fn();
                return G_SOURCE_REMOVE;
            }),
            new CallContext(std::move(fn), m_token),
            reinterpret_cast<GDestroyNotify>(+[](CallContext* ctx) {
                delete ctx;
            }));
        g_source_attach (source, m_mainContext);
        g_source_unref (source);
    }

    void Disable()
    {
        m_token.reset();
    }
};
