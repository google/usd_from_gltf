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

cmake_minimum_required(VERSION 3.0)
project(stb_image)

add_library(stb_image
  stb_image.cc
)

install(TARGETS stb_image EXPORT stb_image DESTINATION lib)
install(EXPORT stb_image DESTINATION lib)
install(FILES stb_image-config.cmake DESTINATION .)
install(FILES stb_image.h DESTINATION include)
