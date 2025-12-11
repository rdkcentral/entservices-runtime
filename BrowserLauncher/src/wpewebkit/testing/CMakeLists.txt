# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

find_package( WaylandClient REQUIRED )
find_package( WesterosSimpleShellClient REQUIRED )

add_library(testRunner
        OBJECT
        tags.h
        focusmanager.h
        westerosfocusmanager.cpp
        testrunner.cpp
        testrunner.h
        )

target_compile_options(
        testRunner
        PRIVATE
        -fPIC
        )

target_include_directories(
        testRunner
        PRIVATE
        $<TARGET_PROPERTY:WPEBackend::WPEBackend,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:WPEWebKit::WPEWebKit,INTERFACE_INCLUDE_DIRECTORIES>
        )

target_compile_definitions(
        testRunner
        PRIVATE
        G_LOG_DOMAIN="TestRunner"
        G_LOG_USE_STRUCTURED=1
        )

target_link_libraries(
        testRunner
        PRIVATE
        # Wayland libraries
        Wayland::Client
        Westeros::SimpleShellClient
        )

add_subdirectory( extension )
