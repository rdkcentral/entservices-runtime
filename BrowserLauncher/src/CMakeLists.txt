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

set( BROWSER_LAUNCHER_VERSION "0.0.0" CACHE STRING "The version string to display in the logs" )

# Disable exceptions
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-exceptions")
# Enable warnings
add_compile_options( -Wall -Wno-psabi )

find_package( nlohmann_json CONFIG REQUIRED )
find_package( Threads REQUIRED )
find_package( GLIB REQUIRED gio gio-unix gmodule gobject )
find_package( FireboltClient CONFIG REQUIRED )
find_package( FireboltTransport CONFIG REQUIRED )

# We require C++20
set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

# Find includes in corresponding build directories
set( CMAKE_INCLUDE_CURRENT_DIR ON )

# Hidden symbols by default
set(CMAKE_C_VISIBILITY_PRESET "hidden")
set(CMAKE_CXX_VISIBILITY_PRESET "hidden")

add_definitions( -DBROWSER_LAUNCHER_VERSION="${BROWSER_LAUNCHER_VERSION}" )

add_subdirectory( wpewebkit )

# Build the executable
add_executable(
        BrowserLauncher

        main.cpp
        browserinterface.h
        launchconfiginterface.h
        browsercontroller.cpp
        browsercontroller.h
        launchconfig.cpp
        launchconfig.h
        runloop.h
        simplesignalslot.h
        )

target_compile_definitions( BrowserLauncher
        PRIVATE
        G_LOG_DOMAIN="BrowserLauncher"
        G_LOG_USE_STRUCTURED=1
        )

target_link_libraries(
        BrowserLauncher

        ${GLIB_GIO_UNIX_LIBRARIES}
        ${GLIB_GIO_LIBRARIES}
        ${GLIB_GOBJECT_LIBRARIES}
        ${GLIB_GMODULE_LIBRARIES}
        ${GLIB_LIBRARIES}
        ${CMAKE_DL_LIBS}
        Threads::Threads
        nlohmann_json::nlohmann_json
        FireboltClient::FireboltClient
        FireboltTransport::FireboltTransport
        )

target_include_directories(
        BrowserLauncher
        PRIVATE
        ${GIO_UNIX_INCLUDEDIR}
        ${GIO_UNIX_INCLUDE_DIRS}
        )

install(
        TARGETS BrowserLauncher
        DESTINATION .
        COMPONENT ${PROJECT_NAME}
        )
