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

#include "cache.h"  // NOLINT: Silence relative path warning.

namespace {
// Accessor type info templated based on C++ primitive type.
// kComponentType: The Gltf::Accessor::ComponentType for the templated type.
// IsDirectType(component_type): Returns true if component_type values can be
// directly referenced as the templated type.
template <typename T>
struct AccessorTypeInfo;

template <> struct AccessorTypeInfo<int8_t> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentByte;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentByte ||
           component_type == Gltf::Accessor::kComponentUnsignedByte;
  }
};
template <> struct AccessorTypeInfo<uint8_t> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentUnsignedByte;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentByte ||
           component_type == Gltf::Accessor::kComponentUnsignedByte;
  }
};
template <> struct AccessorTypeInfo<int16_t> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentShort;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentShort ||
           component_type == Gltf::Accessor::kComponentUnsignedShort;
  }
};
template <> struct AccessorTypeInfo<uint16_t> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentUnsignedShort;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentShort ||
           component_type == Gltf::Accessor::kComponentUnsignedShort;
  }
};
template <> struct AccessorTypeInfo<uint32_t> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentUnsignedInt;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentUnsignedInt;
  }
};
template <> struct AccessorTypeInfo<int32_t> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentUnsignedInt;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentUnsignedInt;
  }
};
template <> struct AccessorTypeInfo<float> {
  static constexpr Gltf::Accessor::ComponentType kComponentType =
      Gltf::Accessor::kComponentFloat;
  static bool IsDirectType(Gltf::Accessor::ComponentType component_type) {
    return component_type == Gltf::Accessor::kComponentFloat;
  }
};

template <typename Src, typename Dst>
inline void ReformatComponent(Src src, Dst* dst) {
  *dst = static_cast<Dst>(src);
}

template <typename Src, typename Dst>
inline void ReformatComponentNormalized(Src src, Dst* dst) {
  *dst = static_cast<Dst>(src);
}
inline void ReformatComponentNormalized(int8_t src, float* dst) {
  *dst = src * (2.0f / 255.0f) + (1.0f / 255.0f);
}
inline void ReformatComponentNormalized(uint8_t src, float* dst) {
  *dst = src * (1.0f / 255.0f);
}
inline void ReformatComponentNormalized(int16_t src, float* dst) {
  *dst = src * (2.0f / 65535.0f) + (1.0f / 65535.0f);
}
inline void ReformatComponentNormalized(uint16_t src, float* dst) {
  *dst = src * (1.0f / 65535.0f);
}
inline void ReformatComponentNormalized(uint32_t src, float* dst) {
  *dst = src * (1.0f / 4294967295.0f);
}

template <typename Src, typename Dst>
void ReformatVectorsT(
    const Src* src, size_t src_stride, size_t vec_count, size_t component_count,
    Dst* dst) {
  const uint8_t* src_it = reinterpret_cast<const uint8_t*>(src);
  for (size_t vi = 0; vi != vec_count; ++vi, src_it += src_stride) {
    const Src* src = reinterpret_cast<const Src*>(src_it);
    for (size_t ci = 0; ci != component_count; ++ci, ++src, ++dst) {
      ReformatComponent(*src, dst);
    }
  }
}

template <typename Src>
void ReformatVectorsNormalized(
    const Src* src, size_t src_stride, size_t vec_count, size_t component_count,
    float* dst) {
  const uint8_t* src_it = reinterpret_cast<const uint8_t*>(src);
  for (size_t vi = 0; vi != vec_count; ++vi, src_it += src_stride) {
    const Src* src = reinterpret_cast<const Src*>(src_it);
    for (size_t ci = 0; ci != component_count; ++ci, ++src, ++dst) {
      ReformatComponentNormalized(*src, dst);
    }
  }
}

template <typename Dst>
void ReformatVectorsNonNormalized(Gltf::Accessor::ComponentType component_type,
                                  const void* src, size_t src_stride,
                                  size_t vec_count, size_t component_count,
                                  Dst* dst) {
  switch (component_type) {
    case Gltf::Accessor::kComponentByte:
      ReformatVectorsT(static_cast<const int8_t*>(src), src_stride, vec_count,
                       component_count, dst);
      break;
    case Gltf::Accessor::kComponentUnsignedByte:
      ReformatVectorsT(static_cast<const uint8_t*>(src), src_stride, vec_count,
                       component_count, dst);
      break;
    case Gltf::Accessor::kComponentShort:
      ReformatVectorsT(static_cast<const int16_t*>(src), src_stride, vec_count,
                       component_count, dst);
      break;
    case Gltf::Accessor::kComponentUnsignedShort:
      ReformatVectorsT(static_cast<const uint16_t*>(src), src_stride, vec_count,
                       component_count, dst);
      break;
    case Gltf::Accessor::kComponentUnsignedInt:
      ReformatVectorsT(static_cast<const uint32_t*>(src), src_stride, vec_count,
                       component_count, dst);
      break;
    case Gltf::Accessor::kComponentFloat:
      ReformatVectorsT(static_cast<const float*>(src), src_stride, vec_count,
                       component_count, dst);
      break;
    default:
      break;
  }
}

template <typename Dst>
void ReformatVectors(Gltf::Accessor::ComponentType component_type,
                     const void* src, size_t src_stride, size_t vec_count,
                     size_t component_count, bool normalized, Dst* dst) {
  // Normalization doesn't make sense for integer destination types, so just
  // ignore 'normalized'. If it's set, it's a problem in the source glTF, but
  // it's recoverable and we don't have a good way to log it here, so this
  // comment must suffice.
  ReformatVectorsNonNormalized(component_type, src, src_stride, vec_count,
                               component_count, dst);
}

void ReformatVectors(Gltf::Accessor::ComponentType component_type,
                     const void* src, size_t src_stride, size_t vec_count,
                     size_t component_count, bool normalized, float* dst) {
  if (normalized) {
    switch (component_type) {
      case Gltf::Accessor::kComponentByte:
        ReformatVectorsNormalized(static_cast<const int8_t*>(src), src_stride,
                                  vec_count, component_count, dst);
        break;
      case Gltf::Accessor::kComponentUnsignedByte:
        ReformatVectorsNormalized(static_cast<const uint8_t*>(src), src_stride,
                                  vec_count, component_count, dst);
        break;
      case Gltf::Accessor::kComponentShort:
        ReformatVectorsNormalized(static_cast<const int16_t*>(src), src_stride,
                                  vec_count, component_count, dst);
        break;
      case Gltf::Accessor::kComponentUnsignedShort:
        ReformatVectorsNormalized(static_cast<const uint16_t*>(src), src_stride,
                                  vec_count, component_count, dst);
        break;
      case Gltf::Accessor::kComponentUnsignedInt:
        ReformatVectorsNormalized(static_cast<const uint32_t*>(src), src_stride,
                                  vec_count, component_count, dst);
        break;
      case Gltf::Accessor::kComponentFloat:
        ReformatVectorsT(static_cast<const float*>(src), src_stride,
                         vec_count, component_count, dst);
        break;
      default:
        break;
    }
  } else {
    ReformatVectorsNonNormalized(component_type, src, src_stride,
                                 vec_count, component_count, dst);
  }
}
}  // namespace

void GltfCache::Reset(const Gltf* gltf, GltfStream* stream) {
  gltf_ = gltf;
  stream_ = stream;
  buffer_entries_.clear();
  image_entries_.clear();
  accessor_entries_.clear();
  if (gltf) {
    buffer_entries_.resize(gltf->buffers.size());
    image_entries_.resize(gltf->images.size());
    accessor_entries_.resize(gltf->accessors.size());
  }
}

const uint8_t* GltfCache::GetBufferData(Gltf::Id buffer_id, size_t* out_size) {
  const Gltf::Buffer* const buffer = Gltf::GetById(gltf_->buffers, buffer_id);
  if (!buffer) {
    *out_size = 0;
    return nullptr;
  }
  BufferEntry& entry = buffer_entries_[Gltf::IdToIndex(buffer_id)];
  if (!entry.loaded) {
    stream_->ReadBuffer(*gltf_, buffer_id, 0, 0, &entry.data);
    entry.loaded = true;
  }
  *out_size = entry.data.size();
  return entry.data.empty() ? nullptr : entry.data.data();
}

const uint8_t* GltfCache::GetViewData(Gltf::Id view_id, size_t* out_size) {
  *out_size = 0;
  const Gltf::BufferView* const view =
      Gltf::GetById(gltf_->bufferViews, view_id);
  if (!view) {
    return nullptr;
  }
  size_t buffer_size;
  const uint8_t* const buffer_data = GetBufferData(view->buffer, &buffer_size);
  if (!buffer_data) {
    return nullptr;
  }
  *out_size = view->byteLength;
  return buffer_data + view->byteOffset;
}

const uint8_t* GltfCache::GetImageData(Gltf::Id image_id, size_t* out_size,
                                       Gltf::Image::MimeType* out_mime_type) {
  *out_size = 0;

  const Gltf::Image* const image = Gltf::GetById(gltf_->images, image_id);
  if (!image) {
    return nullptr;
  }

  if (image->bufferView == Gltf::Id::kNull) {
    // Image referenced by URI.
    ImageEntry& entry = image_entries_[Gltf::IdToIndex(image_id)];
    if (!entry.loaded) {
      stream_->ReadImage(*gltf_, image_id, &entry.data, &entry.mime_type);
      entry.loaded = true;
    }
    *out_size = entry.data.size();
    *out_mime_type = entry.mime_type;
    return entry.data.empty() ? nullptr : entry.data.data();
  } else {
    // Image is stored in a buffer.
    if (image->mimeType == Gltf::Image::kMimeUnset) {
      return nullptr;
    }
    const Gltf::BufferView* const view =
        Gltf::GetById(gltf_->bufferViews, image->bufferView);
    if (!view) {
      return nullptr;
    }
    size_t buffer_size;
    const uint8_t* buffer_data = GetBufferData(view->buffer, &buffer_size);
    if (!buffer_data) {
      return nullptr;
    }
    const size_t image_end = view->byteOffset + view->byteLength;
    if (image_end > buffer_size) {
      return nullptr;
    }
    *out_size = view->byteLength;
    *out_mime_type = image->mimeType;
    return buffer_data + view->byteOffset;
  }
}

bool GltfCache::CopyImage(Gltf::Id image_id, const std::string& dst_path) {
  const Gltf::Image* const image = Gltf::GetById(gltf_->images, image_id);
  if (!image) {
    return false;
  }
  if (image->bufferView == Gltf::Id::kNull) {
    return stream_->CopyImage(*gltf_, image_id, dst_path.c_str());
  } else {
    size_t size;
    Gltf::Image::MimeType mime_type;
    const uint8_t* const data = GetImageData(image_id, &size, &mime_type);
    if (!data) {
      return false;
    }
    return stream_->WriteBinary(dst_path, data, size);
  }
}

template <typename Dst>
const Dst* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_vec_count, size_t* out_component_count) {
  constexpr Gltf::Accessor::ComponentType kDstComponentType =
      AccessorTypeInfo<Dst>::kComponentType;
  AccessorEntry* const accessor_entry =
      Gltf::GetById(accessor_entries_, accessor_id);
  if (!accessor_entry) {
    return nullptr;
  }
  const Gltf::Accessor& accessor =
      *Gltf::GetById(gltf_->accessors, accessor_id);
  const Gltf::Accessor::ComponentType component_type = accessor.componentType;
  const size_t component_count = Gltf::GetComponentCount(accessor.type);
  const size_t vec_count = accessor.count;
  *out_vec_count = vec_count;
  *out_component_count = component_count;

  Content& content = accessor_entry->contents[kDstComponentType];
  if (content.state == Content::kStateUncached) {
    const size_t sparse_count = accessor.sparse.count;
    const bool is_sparse = sparse_count != 0;
    GetViewContent<Dst>(accessor.bufferView, accessor.byteOffset,
                        component_type, vec_count, component_count,
                        accessor.normalized, is_sparse, &content);

    // Apply deltas for sparse buffers.
    if (is_sparse) {
      Content indices_content;
      const uint32_t* const indices = GetViewContent<uint32_t>(
          accessor.sparse.indices.bufferView,
          accessor.sparse.indices.byteOffset,
          accessor.sparse.indices.componentType, sparse_count, 1, false, false,
          &indices_content);
      Content deltas_content;
      const Dst* const deltas = GetViewContent<Dst>(
          accessor.sparse.values.bufferView, accessor.sparse.values.byteOffset,
          component_type, sparse_count, component_count, accessor.normalized,
          false, &deltas_content);
      if (indices && deltas) {
        if (content.state == Content::kStateNull) {
          // Default zero-fill the buffer.
          content.state = Content::kStateReformatted;
          content.reformatted.clear();
          content.reformatted.resize(
              vec_count * component_count * sizeof(Dst), 0);
        }
        Dst* const dst = reinterpret_cast<Dst*>(content.reformatted.data());
        const Dst* delta = deltas;
        for (size_t i = 0; i != sparse_count; ++i, delta += component_count) {
          const size_t vec_index = indices[i];
          Dst* const vec = dst + vec_index * component_count;
          std::copy(delta, delta + component_count, vec);
        }
      }
    }
  }

  return GetContentAs<Dst>(content);
}

// Instantiate the template for the only types we support, so we don't have to
// put all this code in the header.
template const   int8_t* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);
template const  uint8_t* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);
template const  int16_t* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);
template const uint16_t* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);
template const  int32_t* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);
template const uint32_t* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);
template const    float* GltfCache::Access(
    Gltf::Id accessor_id, size_t* out_count, size_t* out_component_count);

template <typename Dst>
const Dst* GltfCache::GetViewContent(
    Gltf::Id view_id, size_t offset,
    Gltf::Accessor::ComponentType component_type, size_t vec_count,
    size_t component_count, bool normalized, bool need_reformat,
    Content* out_content) {
  const Gltf::BufferView* const view =
      Gltf::GetById(gltf_->bufferViews, view_id);
  if (!view) {
    out_content->state = Content::kStateNull;
    return nullptr;
  }
  const uint8_t* const buffer_data = GetBufferData(view->buffer);
  if (!buffer_data) {
    out_content->state = Content::kStateNull;
    return nullptr;
  }
  const size_t src_stride =
      view->byteStride
          ? view->byteStride
          : component_count * Gltf::GetComponentSize(component_type);
  const size_t dst_stride = component_count * sizeof(Dst);
  const bool is_direct = view && src_stride == dst_stride && !need_reformat &&
                         AccessorTypeInfo<Dst>::IsDirectType(component_type);
  const uint32_t src_offset = view->byteOffset + static_cast<uint32_t>(offset);
  if (is_direct) {
    // Reference buffer data directly.
    out_content->state = Content::kStateDirect;
    out_content->direct_buffer_id = view->buffer;
    out_content->direct_offset = src_offset;
  } else {
    // Reformat.
    out_content->state = Content::kStateReformatted;
    out_content->reformatted.resize(vec_count * component_count * sizeof(Dst));
    const void* const src = buffer_data + src_offset;
    Dst* const dst = reinterpret_cast<Dst*>(out_content->reformatted.data());
    ReformatVectors(component_type, src, src_stride, vec_count, component_count,
                    normalized, dst);
  }
  return GetContentAs<Dst>(*out_content);
}
