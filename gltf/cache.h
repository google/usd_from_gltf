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

#ifndef GLTF_CACHE_H_
#define GLTF_CACHE_H_

#include <string>
#include <vector>
#include "gltf.h"  // NOLINT: Silence relative path warning.
#include "message.h"  // NOLINT: Silence relative path warning.
#include "stream.h"  // NOLINT: Silence relative path warning.

// Utility to load and cache bin and image files.
class GltfCache {
 public:
  explicit GltfCache(const Gltf* gltf = nullptr, GltfStream* stream = nullptr)
      : gltf_(nullptr), stream_(nullptr) {
    Reset(gltf, stream);
  }

  void Reset(const Gltf* gltf = nullptr, GltfStream* stream = nullptr);

  const uint8_t* GetBufferData(Gltf::Id buffer_id, size_t* out_size);
  const uint8_t* GetBufferData(Gltf::Id buffer_id) {
    size_t size;
    return GetBufferData(buffer_id, &size);
  }

  const uint8_t* GetViewData(Gltf::Id view_id, size_t* out_size);
  const uint8_t* GetImageData(Gltf::Id image_id, size_t* out_size,
                              Gltf::Image::MimeType* out_mime_type);

  bool ImageExists(Gltf::Id image_id) const {
    return stream_->ImageExists(*gltf_, image_id);
  }

  bool IsImageAtPath(Gltf::Id image_id, const char* dir,
                     const char* name) const {
    return stream_->IsImageAtPath(*gltf_, image_id, dir, name);
  }

  bool IsSourcePath(const char* path) const {
    return stream_->IsSourcePath(path);
  }

  bool CopyImage(Gltf::Id image_id, const std::string& dst_path);

  // Get accessor data as an array, reformatting if necessary.
  // * This returns an array of scalars with length
  //   out_vec_count*out_component_count.
  template <typename Dst>
  const Dst* Access(Gltf::Id accessor_id,
                    size_t* out_vec_count, size_t* out_component_count);

 private:
  struct BufferEntry {
    bool loaded = false;
    std::vector<uint8_t> data;
  };

  struct ImageEntry {
    bool loaded = false;
    Gltf::Image::MimeType mime_type = Gltf::Image::kMimeUnset;
    std::vector<uint8_t> data;
  };

  struct Content {
    enum State : uint8_t {
      kStateUncached,
      kStateNull,
      kStateDirect,
      kStateReformatted,
    };
    State state = kStateUncached;
    Gltf::Id direct_buffer_id = Gltf::Id::kNull;
    uint32_t direct_offset = 0;
    std::vector<uint8_t> reformatted;
  };

  struct AccessorEntry {
    Content contents[Gltf::Accessor::kComponentCount];
  };

  const Gltf* gltf_;
  GltfStream* stream_;
  std::vector<BufferEntry> buffer_entries_;
  std::vector<ImageEntry> image_entries_;
  std::vector<AccessorEntry> accessor_entries_;

  template <typename Dst>
  const Dst* GetContentAs(const Content& content) {
    const void* data;
    switch (content.state) {
    case Content::kStateDirect:
      data = GetBufferData(content.direct_buffer_id) + content.direct_offset;
      break;
    case Content::kStateReformatted:
      data = content.reformatted.data();
      break;
    case Content::kStateNull:
    default:
      data = nullptr;
    }
    return static_cast<const Dst*>(data);
  }

  template <typename Dst>
  const Dst* GetViewContent(
      Gltf::Id view_id, size_t offset,
      Gltf::Accessor::ComponentType component_type,
      size_t vec_count, size_t component_count,
      bool normalized, bool need_reformat,
      Content* out_content);
};

#endif  // GLTF_CACHE_H_
