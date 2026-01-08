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

pkg_check_modules(PC_ESSOS QUIET essos)

find_path(
        ESSOS_INCLUDE_DIR
        NAMES essos.h
        HINTS ${PC_ESSOS_INCLUDEDIR} ${PC_ESSOS_INCLUDE_DIRS}
)

find_library(
        ESSOS_LIBRARY
        NAMES essos
        HINTS ${PC_ESSOS_LIBDIR} ${PC_ESSOS_LIBRARY_DIRS}
)

if(ESSOS_INCLUDE_DIR AND ESSOS_LIBRARY)
    add_library(Essos::Essos SHARED IMPORTED)

    set_target_properties(
        Essos::Essos PROPERTIES
        IMPORTED_LOCATION "${ESSOS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ESSOS_INCLUDE_DIR}"
    )
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
    Essos
    REQUIRED_VARS ESSOS_LIBRARY ESSOS_INCLUDE_DIR
)
mark_as_advanced(
    ESSOS_INCLUDE_DIR
    ESSOS_LIBRARY
)
