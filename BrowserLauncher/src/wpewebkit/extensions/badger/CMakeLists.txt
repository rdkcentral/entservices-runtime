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

find_package( GLIB REQUIRED COMPONENTS gio )

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSOUP REQUIRED IMPORTED_TARGET libsoup-3.0)

find_program(
        GLIB_COMPILE_RESOURCES_EXECUTABLE
        NAMES glib-compile-resources
        HINTS ${GLIB2_PREFIX}
        REQUIRED
)

add_custom_command(
        OUTPUT  badgerresourcebundle.c badgerresourcebundle.c.deps
        DEPENDS badgerresourcebundle.xml
        DEPFILE badgerresourcebundle.c.deps
        COMMAND ${GLIB_COMPILE_RESOURCES_EXECUTABLE}
                --generate
                --target=badgerresourcebundle.c
                --dependency-file=badgerresourcebundle.c.deps
                --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/badgerresourcebundle.xml
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
)

add_library( BadgerExtension
        SHARED
        badgerextension.c
        badgerresourcebundle.c
        transport.h
        transport.cpp
        wsclient.h
        wsclient.cpp
        xrebridge.h
        xrebridge.cpp
        )

target_compile_definitions( BadgerExtension
        PRIVATE
        G_LOG_DOMAIN="BadgerExtension"
        G_LOG_USE_STRUCTURED=1
        )

target_include_directories( BadgerExtension
        PRIVATE
        $<TARGET_PROPERTY:WPEBackend::WPEBackend,INTERFACE_INCLUDE_DIRECTORIES>
        $<TARGET_PROPERTY:WPEWebKit::WPEWebKit,INTERFACE_INCLUDE_DIRECTORIES>
        )

target_link_libraries( BadgerExtension
        PRIVATE
        PkgConfig::LIBSOUP
        )

install(
        TARGETS BadgerExtension
        DESTINATION wpewebkit/extensions
        COMPONENT ${PROJECT_NAME}
        )
