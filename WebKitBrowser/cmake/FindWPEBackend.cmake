# - Try to find WpeBackend
# Once done this will define
#  WPE_BACKEND_FOUND - System has libwpe
#  WPE_BACKEND_INCLUDE_DIRS - The libwpe include directories
#  WPE_BACKEND_LIBRARIES - The libraries needed to use libwpe
#
###
# If not stated otherwise in this file or this component's LICENSE
# file the following copyright and licenses apply:
#
# Copyright 2024 RDK Management
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
###

find_package(PkgConfig)

if(NOT PC_WPE_BACKEND_FOUND)
pkg_search_module(PC_WPE_BACKEND wpe-1.0)
endif()

if(NOT PC_WPE_BACKEND_FOUND)
pkg_search_module(PC_WPE_BACKEND wpe-0.2)
endif()

if(PC_WPE_BACKEND_FOUND)
    find_path(WPE_BACKEND_INCLUDE_DIR
        NAMES wpe/wpe.h
        HINTS ${PC_WPE_BACKEND_INCLUDEDIR} ${PC_WPE_BACKEND_INCLUDE_DIRS}
    )

    find_library(WPE_BACKEND_LIBRARY
        NAMES ${PC_WPE_BACKEND_LIBRARIES}
        HINTS ${PC_WPE_BACKEND_LIBDIR} ${PC_WPE_BACKEND_LIBRARY_DIRS}
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WPEBackend
    VERSION_VAR PC_WPE_BACKEND_VERSION
    REQUIRED_VARS WPE_BACKEND_LIBRARY WPE_BACKEND_INCLUDE_DIR)
    mark_as_advanced(WPE_BACKEND_INCLUDE_DIR WPE_BACKEND_LIBRARY)

if(WPEBackend_FOUND AND NOT TARGET WPEBackend::WPEBackend)
    add_library(WPEBackend::WPEBackend SHARED IMPORTED)
    set_target_properties(WPEBackend::WPEBackend PROPERTIES
        IMPORTED_LOCATION "${WPE_BACKEND_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WPE_BACKEND_INCLUDE_DIR}"
    )
endif()
