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

#ifndef UFG_PROCESS_MESH_H_
#define UFG_PROCESS_MESH_H_

#include <limits>
#include "common/common.h"
#include "common/logging.h"
#include "gltf/cache.h"
#include "gltf/gltf.h"
#include "process/math.h"
#include "pxr/base/vt/array.h"

namespace ufg {
using PXR_NS::VtArray;

constexpr uint32_t kNoIndex = std::numeric_limits<uint32_t>::max();

size_t GetUsedPoints(
    size_t pos_count, const uint32_t* indices, size_t count,
    std::vector<bool>* out_used);

void GetTriIndices(
    const std::vector<uint32_t>& pos_to_point_map,
    const uint32_t* indices, size_t count,
    VtArray<int>* out_vert_counts, VtArray<int>* out_vert_indices);

template <typename T>
void ReverseTriWinding(T* indices, size_t count) {
  UFG_ASSERT_LOGIC(count % 3 == 0);
  T* const end = indices + count;
  for (T* it = indices; it != end; it += 3) {
    const T i1 = it[1];
    const T i2 = it[2];
    it[1] = i2;
    it[2] = i1;
  }
}

struct PrimInfo {
  using Uvset = VtArray<GfVec2f>;
  using UvsetMap = std::map<Gltf::Mesh::Attribute::Number, Uvset>;
  VtArray<int> tri_vert_counts;
  VtArray<int> tri_vert_indices;
  VtArray<GfVec3f> pos;
  VtArray<GfVec3f> norm;
  UvsetMap uvs;
  uint8_t color_stride = 0;
  uint8_t skin_index_stride = 0;
  uint8_t skin_weight_stride = 0;
  VtArray<GfVec3f> color3;
  VtArray<GfVec4f> color4;
  std::vector<int> skin_indices;
  std::vector<float> skin_weights;

  void Swap(PrimInfo* other);
};

struct MeshInfo {
  std::vector<PrimInfo> prims;
};

void GetMeshInfo(const Gltf& gltf, Gltf::Id mesh_id, GltfCache* gltf_cache,
                 MeshInfo* out_info, Logger* logger);
}  // namespace ufg

#endif  // UFG_PROCESS_MESH_H_
