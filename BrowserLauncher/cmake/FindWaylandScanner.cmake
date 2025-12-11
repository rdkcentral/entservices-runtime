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
# - Find Wayland Scanner
#
# This module defines
#  WAYLAND_SCANNER_TOOL_FOUND - the wayland-scanner tool was found


find_program(
        WAYLAND_SCANNER_EXECUTABLE
        NAMES wayland-scanner
        HINTS "${CMAKE_FIND_ROOT_PATH}/bin"
        PATH_SUFFIXES bin usr/bin
)

if(NOT TARGET Wayland::Scanner AND WAYLAND_SCANNER_TOOL_FOUND)
    add_executable(Wayland::Scanner IMPORTED)

    set_target_properties(
            Wayland::Scanner PROPERTIES
            IMPORTED_LOCATION "${WAYLAND_SCANNER_EXECUTABLE}"
    )
endif()


include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(
        WAYLAND_SCANNER_TOOL
        REQUIRED_VARS WAYLAND_SCANNER_EXECUTABLE
)

mark_as_advanced(
        WAYLAND_SCANNER_TOOL
)


# The following adds the functions to generate the client and server side code
# using the wayland scanner
include(CMakeParseArguments)

function(generate_wayland_client_protocol out_var)
    # Parse arguments
    set(oneValueArgs PROTOCOL BASENAME WAYLAND_SCANNER)
    cmake_parse_arguments(ARGS "" "${oneValueArgs}" "" ${ARGN})

    if(ARGS_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "Unknown keywords given to generate_wayland_client_protocol(): \"${ARGS_UNPARSED_ARGUMENTS}\"")
    endif()

    if(ARGS_WAYLAND_SCANNER)
        set(_wayland_scanner "${ARGS_WAYLAND_SCANNER}")
    elseif(WAYLAND_SCANNER_TOOL_FOUND)
        set(_wayland_scanner "${WAYLAND_SCANNER_EXECUTABLE}")
    else()
        message(FATAL_ERROR "Missing wayland-scanner tool, you may need to set -DWAYLAND_SCANNER=<path> on cmake line if building use SDK")
    endif()

    get_filename_component(_infile ${ARGS_PROTOCOL} ABSOLUTE)
    set(_client_header "${CMAKE_CURRENT_BINARY_DIR}/wayland-${ARGS_BASENAME}-client-protocol.h")
    set(_code "${CMAKE_CURRENT_BINARY_DIR}/wayland-${ARGS_BASENAME}-protocol.c")

    set_source_files_properties(${_client_header} GENERATED)
    set_source_files_properties(${_code} GENERATED)
    set_property(SOURCE ${_client_header} PROPERTY SKIP_AUTOMOC ON)

    add_custom_command(OUTPUT "${_client_header}"
            COMMAND ${_wayland_scanner} client-header < ${_infile} > ${_client_header}
            DEPENDS ${_infile} VERBATIM)

    add_custom_command(OUTPUT "${_code}"
            COMMAND ${_wayland_scanner} code < ${_infile} > ${_code}
            DEPENDS ${_infile} ${_client_header} VERBATIM)

    list(APPEND ${out_var} "${_client_header}" "${_code}")
    set(${out_var} ${${out_var}} PARENT_SCOPE)
endfunction()

