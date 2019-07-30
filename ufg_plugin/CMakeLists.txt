# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

include_directories(
  ..
  ${USD_INCLUDE_DIRS}
)

add_library(ufg_plugin SHARED
  ufg_plugin.cc
  ufg_plugin.h
)

if (MSVC)
target_link_libraries(ufg_plugin
  convert
  ${USD_LIBS}
  arch.lib
)
elseif (APPLE)
target_link_libraries(ufg_plugin
  convert
  ${USD_LIBS}
  libarch.dylib
)
else ()
target_link_libraries(ufg_plugin
  convert
  ${USD_LIBS}
  libarch.so
)
endif ()

# Name it .dll for all platforms to match the reference in plugInfo.json.
# This is simpler than copying platform-specific plugInfo.json files.
set_target_properties(ufg_plugin PROPERTIES PREFIX "" SUFFIX ".dll")

set_target_properties(ufg_plugin PROPERTIES RESOURCE plugInfo.json)

install(TARGETS ufg_plugin DESTINATION bin/ufg_plugin)
