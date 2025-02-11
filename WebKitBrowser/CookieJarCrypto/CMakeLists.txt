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

set(PLUGIN_WEBKITBROWSER_COOKIE_JAR_CRYPTO_IMPLEMENTATION "CookieJarCryptoExample.h" CACHE STRING "Implementation of cookie jar encryption/decryption routines." )
set(PLUGIN_WEBKITBROWSER_COOKIE_JAR_CRYPTO_LIBS "" CACHE STRING "Additional linker options that are needed to build specified cookie jar crypto implementation.")

target_include_directories(${PLUGIN_WEBKITBROWSER_IMPLEMENTATION}
    PRIVATE
        CookieJarCrypto)

target_compile_definitions(${PLUGIN_WEBKITBROWSER_IMPLEMENTATION}
    PRIVATE
        COOKIE_JAR_CRYPTO_IMPLEMENTATION="${PLUGIN_WEBKITBROWSER_COOKIE_JAR_CRYPTO_IMPLEMENTATION}")

if(PLUGIN_WEBKITBROWSER_COOKIE_JAR_CRYPTO_LIBS)
    target_link_libraries(${PLUGIN_WEBKITBROWSER_IMPLEMENTATION}
        PRIVATE
            ${PLUGIN_WEBKITBROWSER_COOKIE_JAR_CRYPTO_LIBS})
endif()
