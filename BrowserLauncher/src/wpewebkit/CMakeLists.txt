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

option( ENABLE_TESTRUNNER "Enable test runner framework (core and JS extensions)" ON )

find_package( WPEBackend REQUIRED )
find_package( WPEWebKit REQUIRED )

add_subdirectory( resources )
add_subdirectory( extensions )

add_library( WpeWebKitBrowser
        SHARED

        wpewebkitbrowser.cpp
        wpewebkitbrowser.h
        wpewebkitview.cpp
        wpewebkitview.h
        wpewebkitconfig.cpp
        wpewebkitconfig.h
        wpewebkitutils.cpp
        wpewebkitutils.h
        wpewebkit_2.38.h
        wpewebkit_2.46.h

        $<TARGET_OBJECTS:wpewebkit_resourcebundle>
        )

target_include_directories( WpeWebKitBrowser
        PRIVATE
        $<TARGET_PROPERTY:WPEBackend::WPEBackend,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:WPEWebKit::WPEWebKit,INTERFACE_INCLUDE_DIRECTORIES>
        )

target_compile_definitions( WpeWebKitBrowser
        PRIVATE
        G_LOG_DOMAIN="WpeWebKitBrowser"
        G_LOG_USE_STRUCTURED=1
        $<$<BOOL:${ENABLE_TESTRUNNER}>:ENABLE_TESTRUNNER>
        )

target_compile_options( WpeWebKitBrowser
        PRIVATE
        $<TARGET_PROPERTY:WPEBackend::WPEBackend,INTERFACE_COMPILE_OPTIONS>
        )

target_link_libraries( WpeWebKitBrowser
        PRIVATE
        )

if( ENABLE_TESTRUNNER )
    add_subdirectory( testing )
    target_link_libraries( WpeWebKitBrowser
        PRIVATE
        testRunner
        )
endif()

install(
        TARGETS WpeWebKitBrowser
        DESTINATION wpewebkit
        COMPONENT ${PROJECT_NAME}
        )
