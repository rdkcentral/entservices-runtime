# If not stated otherwise in this file or this component's license file the
# following copyright and licenses apply:
#
# Copyright 2020 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the License);
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an AS IS BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# - Find WaylandClient
#
# This module defines
#  WAYLAND_CLIENT_FOUND - whether the wayland-client library was found
#

find_path(
        WAYLAND_CLIENT_INCLUDE_DIR
        NAMES wayland-client.h
)

find_library(
        WAYLAND_CLIENT_LIBRARY
        NAMES wayland-client libwayland-client
)

if(WAYLAND_CLIENT_INCLUDE_DIR AND WAYLAND_CLIENT_LIBRARY)
    add_library(Wayland::Client UNKNOWN IMPORTED)

    set_target_properties(
            Wayland::Client PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${WAYLAND_CLIENT_INCLUDE_DIR}"
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "${WAYLAND_CLIENT_LIBRARY}"
    )
endif()


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
        WaylandClient
        REQUIRED_VARS WAYLAND_CLIENT_LIBRARY WAYLAND_CLIENT_INCLUDE_DIR
)

mark_as_advanced(
        WAYLAND_CLIENT_INCLUDE_DIR
        WAYLAND_CLIENT_LIBRARY
)
