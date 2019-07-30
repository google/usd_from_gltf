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

#include "process/image_png.h"

#include "stb_image.h"  // NOLINT: Silence relative path warning.

namespace ufg {
bool ImageFallbackRead(
    const void* src, size_t src_size,
    uint32_t* out_width, uint32_t* out_height, uint8_t* out_channel_count,
    std::vector<Image::Component>* out_buffer, Logger* logger) {
  const int len = static_cast<int>(src_size);
  UFG_ASSERT_FORMAT(static_cast<size_t>(len) == src_size);
  int width, height, channel_count;
  stbi_uc* const data = stbi_load_from_memory(
      static_cast<const stbi_uc*>(src), len,
      &width, &height, &channel_count, 0);
  if (!data) {
    Log<UFG_ERROR_IMAGE_FALLBACK_DECODE>(logger, "");
    return false;
  }
  const size_t buffer_size = width * height * channel_count;
  out_buffer->assign(data, data + buffer_size);
  stbi_image_free(data);
  *out_width = width;
  *out_height = height;
  *out_channel_count = channel_count;
  return true;
}
}  // namespace ufg
