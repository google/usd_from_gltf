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

#ifndef UFG_PROCESS_IMAGE_JPG_H_
#define UFG_PROCESS_IMAGE_JPG_H_

#include "process/image.h"

// Read/write JPG files via libjpeg-turbo.
namespace ufg {
bool HasJpgHeader(const void* src, size_t src_size);
bool JpgRead(
    const void* src, size_t src_size,
    uint32_t* out_width, uint32_t* out_height, uint8_t* out_channel_count,
    std::vector<Image::Component>* out_buffer, Logger* logger);

// * quality: JPG compression quality [1=worst, 100=best].
// * subsamp: JPG chroma subsampling method [0=best, 2=worst].
bool JpgWrite(
    const char* path, uint32_t width, uint32_t height, uint8_t channel_count,
    const Image::Component* data, int quality, int subsamp, Logger* logger);
}  // namespace ufg

#endif  // UFG_PROCESS_IMAGE_JPG_H_
