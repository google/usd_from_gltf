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

add_library(common
  common.h
  common_util.h
  config.cc
  config.h
  logging.cc
  logging.h
  messages.inl
  platform.cc
  platform.h
)

file(GLOB COMMON_HEADERS "*.h")
set_target_properties(common PROPERTIES PUBLIC_HEADER "${COMMON_HEADERS}")

target_link_libraries(common gltf)

install(TARGETS common EXPORT ufglib DESTINATION lib/ufg PUBLIC_HEADER DESTINATION include/ufg/common)
