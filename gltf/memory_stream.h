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

#ifndef GLTF_MEMORY_STREAM_H_
#define GLTF_MEMORY_STREAM_H_

#include "stream.h"  // NOLINT: Silence relative path warning.

// GltfStream implementation that reads from memory.
class GltfMemoryStream : public GltfStream {
 public:
  GltfMemoryStream(GltfLogger* logger, const void* data, size_t size);
  bool GlbOpen(const char* path) override;
  bool GlbIsOpen() const override;
  size_t GlbGetFileSize() const override;
  size_t GlbRead(size_t size, void* out_data) override;
  bool GlbSeekRelative(size_t size) override;
  void GlbClose() override;

 private:
  const uint8_t* data_;
  size_t size_;
  size_t pos_;
};

#endif  // GLTF_MEMORY_STREAM_H_
