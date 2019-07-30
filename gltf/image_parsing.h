/*
 * Copyright 2019 Google LLC
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef GLTF_IMAGE_PARSING_H_
#define GLTF_IMAGE_PARSING_H_

#include "stream.h"  // NOLINT: Silence relative path warning.

// Parse image type and dimensions from a header in memory.
Gltf::Image::MimeType GltfParseImage(
    const void* data, size_t data_size, const char* name, GltfLogger* logger,
    uint32_t* out_width, uint32_t* out_height);

// Parse image type and dimensions from a file.
Gltf::Image::MimeType GltfParseImage(
    FILE* fp, const char* name, GltfLogger* logger,
    uint32_t* out_width, uint32_t* out_height);

// Parse image type and dimensions from a read callback.
using GltfParseReadFP = bool (*)(void* user_context, size_t start, size_t limit,
                                 std::vector<uint8_t>* out_data);
Gltf::Image::MimeType GltfParseImage(
    GltfParseReadFP read, void* user_context, const char* name,
    GltfLogger* logger, uint32_t* out_width, uint32_t* out_height);

#endif  // GLTF_IMAGE_PARSING_H_
