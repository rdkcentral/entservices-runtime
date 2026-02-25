/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2020 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <tuple>

#define VERSION_FORMAT "v%u.%u.%u"
#define VERSION_ARGS(x)  std::get<0>(x),std::get<1>(x),std::get<2>(x)

using VersionNumber = std::tuple<unsigned, unsigned, unsigned>;

class WpeWebKitUtils
{
public:
    // returns current WebKit version in runtime
    static VersionNumber webkitVersion();
};
