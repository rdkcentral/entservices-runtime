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
# - Find Westeros SimpleShell Client
#
# This module defines
#  SIMPLESHELL_CLIENT_FOUND - whether the westeros simpleshell client library was found
#

find_path(
        SIMPLESHELL_CLIENT_INCLUDE_DIR
        NAMES simpleshell-client-protocol.h
)

find_library(
        SIMPLESHELL_CLIENT_LIBRARY
        NAMES westeros_simpleshell_client libwesteros_simpleshell_client
)

if( SIMPLESHELL_CLIENT_INCLUDE_DIR AND SIMPLESHELL_CLIENT_LIBRARY )
    add_library(Westeros::SimpleShellClient UNKNOWN IMPORTED)

    set_target_properties(
            Westeros::SimpleShellClient PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${SIMPLESHELL_CLIENT_INCLUDE_DIR}"
            IMPORTED_LINK_INTERFACE_LANGUAGES "C"
            IMPORTED_LOCATION "${SIMPLESHELL_CLIENT_LIBRARY}"
    )
endif()


include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
        WesterosSimpleShellClient
        REQUIRED_VARS SIMPLESHELL_CLIENT_LIBRARY SIMPLESHELL_CLIENT_INCLUDE_DIR
)

mark_as_advanced(
        SIMPLESHELL_CLIENT_INCLUDE_DIR
        SIMPLESHELL_CLIENT_LIBRARY
)
