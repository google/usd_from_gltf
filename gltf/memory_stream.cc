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

#include "memory_stream.h"  // NOLINT: Silence relative path warning.

#include <string.h>

GltfMemoryStream::GltfMemoryStream(
    GltfLogger* logger, const void* data, size_t size)
    : GltfStream(logger),
      data_(static_cast<const uint8_t*>(data)),
      size_(size),
      pos_(0) {}

bool GltfMemoryStream::GlbOpen(const char* path) {
  pos_ = 0;
  return true;
}

bool GltfMemoryStream::GlbIsOpen() const {
  return true;
}

size_t GltfMemoryStream::GlbGetFileSize() const {
  return size_;
}

size_t GltfMemoryStream::GlbRead(size_t size, void* out_data) {
  const size_t space = size_ - pos_;
  const size_t read_size = std::min(space, size);
  memcpy(out_data, data_ + pos_, read_size);
  const size_t new_pos = pos_ + size;
  pos_ = std::min(new_pos, size_);
  return read_size;
}

bool GltfMemoryStream::GlbSeekRelative(size_t size) {
  const size_t new_pos = pos_ + size;
  pos_ = std::min(new_pos, size_);
  return new_pos <= size_;
}

void GltfMemoryStream::GlbClose() {
}
