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

find_package(PkgConfig)

pkg_check_modules(PC_WESTEROS_COMPOSITOR QUIET westeros-compositor)

find_path(
        WESTEROS_COMPOSITOR_INCLUDE_DIR
        NAMES westeros-compositor.h
        HINTS ${PC_WESTEROS_COMPOSITOR_INCLUDEDIR} ${PC_WESTEROS_COMPOSITOR_INCLUDE_DIRS}
)

find_library(
        WESTEROS_COMPOSITOR_LIBRARY
        NAMES westeros_compositor
        HINTS ${PC_WESTEROS_COMPOSITOR_LIBDIR} ${PC_WESTEROS_COMPOSITOR_LIBRARY_DIRS}
)

if(WESTEROS_COMPOSITOR_INCLUDE_DIR AND WESTEROS_COMPOSITOR_LIBRARY)
    add_library(WesterosCompositor::WesterosCompositor SHARED IMPORTED)

    set_target_properties(
        WesterosCompositor::WesterosCompositor PROPERTIES
        IMPORTED_LOCATION "${WESTEROS_COMPOSITOR_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${WESTEROS_COMPOSITOR_INCLUDE_DIR}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    WesterosCompositor
    REQUIRED_VARS WESTEROS_COMPOSITOR_LIBRARY WESTEROS_COMPOSITOR_INCLUDE_DIR
)
mark_as_advanced(
    WESTEROS_COMPOSITOR_INCLUDE_DIR
    WESTEROS_COMPOSITOR_LIBRARY
)
