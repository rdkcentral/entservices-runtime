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

find_package( GLIB REQUIRED COMPONENTS gio gio-unix gmodule gobject )
find_package( GTest REQUIRED )
find_package( nlohmann_json CONFIG REQUIRED )
find_package( LibSoup "3.0" REQUIRED )
find_package( WesterosCompositor )

set( CMAKE_CXX_STANDARD 20 )
set( CMAKE_CXX_STANDARD_REQUIRED ON )

add_compile_options( -Wall )
add_subdirectory( resources )

add_executable(
  browser_launcher_tests
    browserlauncher_test.h
    browserlauncher_test.cc
    lifecycle_tests.cc
    media_capabilities_tests.cc
    $<TARGET_OBJECTS:tests_resourcebundle>
)

target_compile_definitions(
  browser_launcher_tests PRIVATE
    G_LOG_DOMAIN="BrowserLauncherTests"
    G_LOG_USE_STRUCTURED=1
)

target_link_libraries(
  browser_launcher_tests
    ${GLIB_GIO_UNIX_LIBRARIES}
    ${GLIB_GIO_LIBRARIES}
    ${GLIB_GOBJECT_LIBRARIES}
    ${GLIB_LIBRARIES}
    ${LIBSOUP_LIBRARIES}
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

if (WesterosCompositor_FOUND)
    find_package( Essos REQUIRED )
    target_compile_definitions(
      browser_launcher_tests PRIVATE
        HAVE_WESTEROS_COMPOSITOR=1
    )
    target_link_libraries(
      browser_launcher_tests
        WesterosCompositor::WesterosCompositor
        Essos::Essos
    )
endif()

target_include_directories(
  browser_launcher_tests PRIVATE
    ${GLIB_INCLUDE_DIRS}
    ${LIBSOUP_INCLUDE_DIRS}
)

install(
  TARGETS browser_launcher_tests
  DESTINATION tests
  COMPONENT Tests
)
