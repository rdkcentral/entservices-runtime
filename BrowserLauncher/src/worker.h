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

#include "runloop.h"

#include <thread>

class Worker
{
    GMainContext *m_mainContext { g_main_context_new() };
    GMainLoop *m_loop { g_main_loop_new(m_mainContext, FALSE) };
    RunLoop m_runLoop { m_mainContext };
    std::thread m_thread;
public:
    Worker()
    {
        m_thread = std::thread([this] {
            g_main_context_push_thread_default(m_mainContext);
            g_main_loop_run(m_loop);
            g_main_context_pop_thread_default(m_mainContext);
        });
    }

    ~Worker()
    {
        m_runLoop.Disable();

        g_main_loop_quit(m_loop);
        if (m_thread.joinable())
            m_thread.join();

        g_clear_pointer(&m_loop, g_main_loop_unref);
        g_clear_pointer(&m_mainContext, g_main_context_unref);
    }

    RunLoop& runLoop()
    {
        return m_runLoop;
    }
};
