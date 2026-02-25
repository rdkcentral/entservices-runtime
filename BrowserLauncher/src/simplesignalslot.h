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

#include <functional>
#include <vector>

template <typename... Args>
class Signal
{
public:
    using Slot = std::function<void(Args...)>;

    Signal() = default;
    ~Signal() = default;
    
    // Connects a callable (slot) to the Signal
    void connect(Slot&& slot)
    {
        m_slots.emplace_back(std::move(slot));
    }

    // Removes all slots
    void disconnectAll()
    {
        m_slots.clear();
    }

    // Emits the signal, invoking all connected slots
    void emit(Args... args)
    {
        for (const auto& slot : m_slots)
            slot(args...);
    }

private:
    Signal(const Signal &) = delete;
    Signal(Signal &&) = delete;
    Signal &operator=(const Signal &) = delete;
    
    std::vector<Slot> m_slots;
};
