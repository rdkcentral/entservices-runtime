/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
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
 **/
#pragma once

#include <com/Communicator.h>
#include <core/Proxy.h>
#include <plugins/IShell.h>

#include "UtilsLogging.h"

namespace WPEFramework {
namespace PluginHost {
    class IShell;
}

namespace Plugin {

    template <typename INTERFACE>
    class PluginInterfaceRef {
        INTERFACE* _interface;
        PluginHost::IShell* _controller;

    public:
        PluginInterfaceRef()
            : _interface(nullptr)
            , _controller(nullptr)
        {
        }

        PluginInterfaceRef(INTERFACE* interface, PluginHost::IShell* controller)
            : _interface(interface)
            , _controller(controller)
        {
        }

        ~PluginInterfaceRef()
        {
            Reset();
        }

        // avoid copies
        PluginInterfaceRef(const PluginInterfaceRef&) = delete;
        PluginInterfaceRef& operator=(const PluginInterfaceRef&) = delete;

        // use move
        PluginInterfaceRef(PluginInterfaceRef&& other)
            : _interface(other._interface)
            , _controller(other._controller)
        {
            other._interface = nullptr;
            other._controller = nullptr;
        }

        PluginInterfaceRef& operator=(PluginInterfaceRef&& other)
        {
            if (this != &other) {
                _interface = other._interface;
                _controller = other._controller;
                other._interface = nullptr;
                other._controller = nullptr;
            }
            return *this;
        }

        operator bool() const
        {
            return _interface != nullptr;
        }

        INTERFACE* operator->() const
        {
            return _interface;
        }

        inline PluginHost::IShell* controller()
        {
            return _controller;
        }

        void Reset()
        {
            if (_interface) {
                _interface->Release();
                _interface = nullptr;
            }

            if (_controller) {
                _controller->Release();
                _controller = nullptr;
            }
        }
    };

    template <typename INTERFACE>
    class PluginInterfaceBuilder;

    // default impl
    template <typename INTERFACE>
    INTERFACE* createInterface(PluginInterfaceBuilder<INTERFACE>& builder)
    {
        WPEFramework::PluginHost::IShell* controller = builder.controller();

        if (!controller) {
            LOGERR("Invalid controller");
            return nullptr;
        }

        auto pluginInterface = controller->QueryInterface<INTERFACE>();

        if (pluginInterface) {
            pluginInterface->AddRef();
        }

        return pluginInterface;
    }

    template <typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args)
    {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }

    template <typename INTERFACE>
    class PluginInterfaceBuilder {

        Core::ProxyType<RPC::CommunicatorClient>& _communicatorClient;
        const std::string _callsign;
        PluginHost::IShell* _controller;
        uint32_t _version;
        uint32_t _timeout;

    public:
        PluginInterfaceBuilder(Core::ProxyType<RPC::CommunicatorClient>& communicatorClient, const char* callsign)
            : _communicatorClient(communicatorClient)
            , _callsign(callsign)
            , _controller(nullptr)
            , _version(static_cast<uint32_t>(~0))
            , _timeout(3000)
        {
        }

        // won't take ownership of ref members
        ~PluginInterfaceBuilder() = default;

        inline PluginInterfaceBuilder& withVersion(uint32_t version)
        {
            _version = version;
            return *this;
        }

        inline PluginInterfaceBuilder& withTimeout(uint32_t timeoutMs)
        {
            _timeout = timeoutMs;
            return *this;
        }

        PluginInterfaceRef<INTERFACE> createInterface(void)
        {
            auto* interface = ::WPEFramework::Plugin::createInterface<INTERFACE>(*this);

            if (!interface) {
                LOGERR("Failed to create plugin interface for %s", _callsign.c_str());
            }

            // pass on the ownership of controller to interfaceRef
            return std::move(PluginInterfaceRef<INTERFACE>(interface, _controller));
        }

        inline Core::ProxyType<RPC::CommunicatorClient>& communicator()
        {
            return _communicatorClient;
        }

        WPEFramework::PluginHost::IShell* controller()
        {
            do {
                if (!_communicatorClient.IsValid()) {
                    LOGWARN("Invalid _communicatorClient");
                    break;
                }
                LOGINFO("Connect to COM-RPC socket for %s", _callsign.c_str());
                if (!_controller) {
                    _controller = _communicatorClient->Open<PluginHost::IShell>(_callsign.c_str(), _version, _timeout);
                }

                if (!_controller) {
                    LOGERR("Failed to create controller for %s", _callsign.c_str());
                    break;
                }

                // success case
                return _controller;
            } while (false);

            // failure case
            return nullptr;
        }
    };

} // Plugin
} // WPEFramework
