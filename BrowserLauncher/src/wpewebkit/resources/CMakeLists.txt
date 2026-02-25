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

find_program(
        GLIB_COMPILE_RESOURCES_EXECUTABLE
        NAMES glib-compile-resources
        HINTS ${GLIB2_PREFIX}
        REQUIRED
)

add_custom_command(
        OUTPUT  wpewebkitresourcebundle.c wpewebkitresourcebundle.c.deps
        DEPENDS wpewebkitresourcebundle.xml
        DEPFILE wpewebkitresourcebundle.c.deps
        COMMAND ${GLIB_COMPILE_RESOURCES_EXECUTABLE}
                --generate
                --target=wpewebkitresourcebundle.c
                --dependency-file=wpewebkitresourcebundle.c.deps
                --sourcedir=${CMAKE_CURRENT_SOURCE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/wpewebkitresourcebundle.xml
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        VERBATIM
)

add_library(
        wpewebkit_resourcebundle

        OBJECT
        wpewebkitresourcebundle.c

        )

target_include_directories(
        wpewebkit_resourcebundle

        PRIVATE
        ${GLIB_INCLUDE_DIRS}

        )
