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

add_library( WebRuntimeExtension
        SHARED
        webruntimeextension.cpp
        )

target_compile_definitions( WebRuntimeExtension
        PRIVATE
        G_LOG_DOMAIN="WebRuntimeExtension"
        G_LOG_USE_STRUCTURED=1
        )

target_include_directories( WebRuntimeExtension
        PRIVATE
        $<TARGET_PROPERTY:WPEWebKit::WPEWebKit${WEBKIT_API_VERSION},INTERFACE_INCLUDE_DIRECTORIES>
        )

target_link_libraries( WebRuntimeExtension
        PRIVATE
        )

install(
        TARGETS WebRuntimeExtension
        DESTINATION wpewebkit/extensions
        COMPONENT ${PROJECT_NAME}
        )
