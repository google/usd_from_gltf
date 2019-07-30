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

find_package(json)
if (NOT json_FOUND)
  message(FATAL_ERROR "JSON package not found.")
endif (NOT json_FOUND)

include_directories(
  ${json_INCLUDE_DIR}
)

add_library(gltf
  internal_util.cc
  internal_util.h
  load.cc
  load.h
  memory_stream.cc
  memory_stream.h
  message.cc
  message.h
  messages.inl
  stream.cc
  stream.h
  validate.cc
  validate.h
  cache.cc
  cache.h
  disk_stream.cc
  disk_stream.h
  disk_util.cc
  disk_util.h
  glb_stream.cc
  glb_stream.h
  gltf.cc
  gltf.h
  image_parsing.cc
  image_parsing.h
)

file(GLOB GLTF_HEADERS "*.h")
set_target_properties(gltf PROPERTIES PUBLIC_HEADER "${GLTF_HEADERS}")

install(TARGETS gltf EXPORT ufglib DESTINATION lib/ufg PUBLIC_HEADER DESTINATION include/ufg/gltf)
