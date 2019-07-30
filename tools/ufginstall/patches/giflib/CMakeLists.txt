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
project(giflib)

include_directories(.)

add_library(giflib
  dgif_lib.c
  egif_lib.c
  gif_err.c
  gif_font.c
  gif_hash.c
  gifalloc.c
  openbsd-reallocarray.c
  quantize.c
)

install(TARGETS giflib EXPORT giflib DESTINATION lib)
install(EXPORT giflib DESTINATION lib)
install(FILES giflib-config.cmake DESTINATION .)
install(FILES gif_lib.h DESTINATION include)
