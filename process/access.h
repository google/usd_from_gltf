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

#ifndef UFG_PROCESS_ACCESS_H_
#define UFG_PROCESS_ACCESS_H_

#include <vector>
#include "common/common.h"
#include "common/logging.h"
#include "gltf/cache.h"
#include "gltf/gltf.h"
#include "process/math.h"
#include "pxr/base/vt/array.h"

namespace ufg {
using PXR_NS::VtArray;

template <typename In, typename Out>
void ToVtArray(const std::vector<In>& in, VtArray<Out>* out) {
  const size_t count = in.size();
  out->resize(count);
  for (size_t i = 0; i != count; ++i) {
    (*out)[i] = Out(in[i]);
  }
}

template <typename SrcComponent, typename DstVec> struct CopyAccessorHandler;

template <> struct CopyAccessorHandler<float, float> {
  static constexpr size_t kComponentCount = 1;
  static constexpr Gltf::Accessor::Type kGltfType = Gltf::Accessor::kTypeScalar;
  static void CopyVec(const float* src, float* dst) { *dst = src[0]; }
};

template <> struct CopyAccessorHandler<float, GfVec3f> {
  static constexpr size_t kComponentCount = 3;
  static constexpr Gltf::Accessor::Type kGltfType = Gltf::Accessor::kTypeVec3;
  static void CopyVec(const float* src, GfVec3f* dst) {
    *dst = GfVec3f(src[0], src[1], src[2]);
  }
};

template <> struct CopyAccessorHandler<float, GfQuatf> {
  static constexpr size_t kComponentCount = 4;
  static constexpr Gltf::Accessor::Type kGltfType = Gltf::Accessor::kTypeVec4;
  static void CopyVec(const float* src, GfQuatf* dst) {
    *dst = GfQuatf(src[3], src[0], src[1], src[2]);
  }
};

template <> struct CopyAccessorHandler<float, GfMatrix4f> {
  static constexpr size_t kComponentCount = 16;
  static constexpr Gltf::Accessor::Type kGltfType = Gltf::Accessor::kTypeMat4;
  static void CopyVec(const float* src, GfMatrix4f* dst) {
    *dst = ToMatrix4f(src);
  }
};

template <typename Dst>
size_t CopyAccessorToScalars(
    const Gltf& gltf, Gltf::Id accessor_id, const std::vector<bool>& used,
    GltfCache* gltf_cache, std::vector<Dst>* out_scalars) {
  size_t src_vec_count, component_count;
  const Dst* src =
      gltf_cache->Access<Dst>(accessor_id, &src_vec_count, &component_count);
  if (!src) {
    return 0;
  }
  UFG_ASSERT_FORMAT(src_vec_count == used.size());
  std::vector<Dst> scalars(src_vec_count * component_count);
  std::vector<Dst> dst_buffer(src_vec_count * component_count);
  Dst* dst = scalars.data();
  for (size_t i = 0; i != src_vec_count; ++i, src += component_count) {
    if (!used[i]) {
      continue;
    }
    std::copy(src, src + component_count, dst);
    dst += component_count;
  }
  scalars.resize(dst - scalars.data());
  if (scalars.empty()) {
    return 0;
  }
  out_scalars->swap(scalars);
  return component_count;
}

template <typename Dst>
size_t CopyAccessorToScalars(
    const Gltf& gltf, const Gltf::Mesh::AttributeSet& attrs,
    const Gltf::Mesh::Attribute& key, const std::vector<bool>& used,
    GltfCache* gltf_cache, std::vector<Dst>* out_scalars) {
  const auto found = attrs.find(key);
  if (found == attrs.end()) {
    return 0;
  }
  return CopyAccessorToScalars(
      gltf, found->accessor, used, gltf_cache, out_scalars);
}

template <typename DstVec>
void CopyAccessorToVectors(
    const Gltf& gltf, Gltf::Id accessor_id,
    GltfCache* gltf_cache, std::vector<DstVec>* out_vecs) {
  using Handler = CopyAccessorHandler<float, DstVec>;
  size_t vec_count, component_count;
  const float* const scalars = gltf_cache->Access<float>(
      accessor_id, &vec_count, &component_count);
  UFG_ASSERT_FORMAT(component_count == Handler::kComponentCount);
  out_vecs->resize(vec_count);
  const float* src = scalars;
  DstVec* dst = out_vecs->data();
  for (size_t i = 0; i != vec_count; ++i) {
    Handler::CopyVec(src, dst);
    src += component_count;
    ++dst;
  }
}

template <typename Array>
void CopyAccessorToVectors(
    const Gltf& gltf, Gltf::Id accessor_id, const std::vector<bool>& used,
    GltfCache* gltf_cache, Array* out_vecs) {
  using Vec = typename Array::value_type;
  using Scalar = typename Vec::ScalarType;
  static_assert(sizeof(Vec) / sizeof(Scalar) == Vec::dimension, "");
  size_t src_vec_count, component_count;
  const Scalar* const scalars = gltf_cache->Access<Scalar>(
      accessor_id, &src_vec_count, &component_count);
  UFG_ASSERT_LOGIC(component_count == Vec::dimension);
  const Scalar* src = scalars;
  out_vecs->resize(src_vec_count);
  Scalar* dst = out_vecs->data()->data();
  for (size_t i = 0; i != src_vec_count; ++i, src += component_count) {
    if (!used[i]) {
      continue;
    }
    std::copy(src, src + component_count, dst);
    dst += component_count;
  }
  const size_t dst_scalar_count = dst - out_vecs->data()->data();
  const size_t dst_vec_count = dst_scalar_count / Vec::dimension;
  out_vecs->resize(dst_vec_count);
}

template <typename Array>
bool CopyAccessorToVectors(
    const Gltf& gltf, const Gltf::Mesh::AttributeSet& attrs,
    const Gltf::Mesh::Attribute& key, const std::vector<bool>& used,
    GltfCache* gltf_cache, Array* out_vecs) {
  const auto found = attrs.find(key);
  if (found == attrs.end()) {
    return false;
  }
  Array vecs;
  CopyAccessorToVectors(gltf, found->accessor, used, gltf_cache, &vecs);
  if (vecs.empty()) {
    return false;
  }
  out_vecs->swap(vecs);
  return true;
}

inline size_t GetAttributeComponentCount(
    const Gltf& gltf, const Gltf::Mesh::AttributeSet& attrs,
    const Gltf::Mesh::Attribute& key) {
  const auto found = attrs.find(key);
  if (found == attrs.end()) {
    return 0;
  }
  const Gltf::Accessor* const accessor =
      Gltf::GetById(gltf.accessors, found->accessor);
  return accessor ? Gltf::GetComponentCount(accessor->type) : 0;
}
}  // namespace ufg

#endif  // UFG_PROCESS_ACCESS_H_
