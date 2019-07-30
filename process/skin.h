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

#ifndef UFG_PROCESS_SKIN_H_
#define UFG_PROCESS_SKIN_H_

#include <vector>
#include "common/common.h"
#include "process/access.h"
#include "process/mesh.h"

namespace ufg {
using PXR_NS::TfToken;

struct SkinInfluence {
  static constexpr uint16_t kUnused = 0xffffu;

  // GCC work-around. Some STL functions are taking a reference to kUnused
  // causing 'ODR-use', which means the value needs to be externally defined.
  // But since it's constexpr, we can't externally define it. So for these
  // cases we use a wrapper function to prevent the variable reference.
  // See: https://en.cppreference.com/w/cpp/language/definition#ODR-use
  // And: https://stackoverflow.com/questions/40690260/undefined-reference-error-for-static-constexpr-member
  static uint16_t Unused() {
    return kUnused;
  }

  uint16_t index;
  float weight;
};

struct SkinBinding {
  static constexpr size_t kInfluenceMax = 4;
  SkinInfluence influences[kInfluenceMax];

  // Sort weights greatest-to-least.
  void SortInfluencesByWeight();

  void Normalize(size_t joint_count);
  void Assign(
      const int* indices, size_t index_count,
      const float* weights, size_t weight_count, size_t joint_count);
  size_t CountUsed() const;
};

// Get skin information from glTF.
struct SkinInfo {
  std::string name;
  Gltf::Id root_node_id;
  std::vector<Gltf::Id> ujoint_to_node_map;
  VtArray<TfToken> ujoint_names;
  VtArray<GfMatrix4d> rest_mats;
  VtArray<GfMatrix4d> bind_mats;
};
void GetSkinInfo(
    const Gltf& gltf, const std::vector<MeshInfo>& mesh_infos, Gltf::Id skin_id,
    const Gltf::Id* node_parents, const std::vector<Gltf::Id>& scene_nodes,
    const std::vector<bool>* force_nodes_used, GltfCache* gltf_cache,
    SkinInfo* out_skin_info,
    std::vector<uint16_t>* out_gjoint_to_ujoint_map);

// Get the set of skin information for skins used in the scene, merging if
// necessary.
struct SkinSrc {
  uint32_t used_skin_index = ~0u;
  std::vector<uint16_t> gjoint_to_ujoint_map;
};
void GetUsedSkinInfos(
    const Gltf& gltf, const std::vector<MeshInfo>& mesh_infos,
    const Gltf::Id* node_parents, const std::vector<Gltf::Id>& scene_nodes,
    const std::vector<bool>* force_nodes_used, bool merge,
    GltfCache* gltf_cache,
    std::vector<SkinInfo>* out_used_skin_infos,
    std::vector<SkinSrc>* out_gltf_skin_srcs);

// Get skin data for a mesh.
struct SkinData {
  uint8_t influence_count;
  bool is_rigid;
  std::vector<SkinBinding> bindings;
};
bool GetSkinData(
    const int* indices, size_t index_stride,
    const float* weights, size_t weight_stride,
    size_t node_count, size_t vert_count,
    const uint16_t* gjoint_to_ujoint_map, size_t gjoint_count,
    SkinData* out_skin_data);

// Get matrices used to animate skinned mesh normals. These transform vertices
// from the bind-pose to object-space for the given animation joint rotations
// and scales.
void GetSkinJointMatricesForNormals(
    const SkinInfo& skin_info, size_t node_count, const Gltf::Id* node_parents,
    size_t ujoint_count, const GfQuatf* rots, const GfVec3f* scales,
    GfMatrix3f* out_norm_mats);

// Skin vertex normals.
// * norm_joint_mats is contains per-joint skinning matrices, inverse-transposed
//   for normal vector transformation.
void SkinNormals(
    const GfMatrix3f* norm_joint_mats, const GfVec3f* norms, size_t norm_count,
    const SkinBinding* skin_bindings,
    GfVec3f* out_norms);

}  // namespace ufg

#endif  // UFG_PROCESS_SKIN_H_
