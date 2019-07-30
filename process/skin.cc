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

#include "process/skin.h"

#include <algorithm>
#include "common/common_util.h"
#include "common/config.h"
#include "process/mesh.h"
#include "process/process_util.h"

namespace ufg {
namespace {
std::vector<bool> GetJointsUsed(
    const Gltf& gltf, const std::vector<MeshInfo>& mesh_infos,
    Gltf::Id mesh_id, size_t joint_count, GltfCache* gltf_cache) {
  const MeshInfo& mesh_info = *UFG_VERIFY(Gltf::GetById(mesh_infos, mesh_id));
  std::vector<bool> joints_used(joint_count, false);
  for (const PrimInfo& prim_info : mesh_info.prims) {
    const size_t used_vert_count = prim_info.pos.size();
    const size_t skin_index_stride = prim_info.skin_index_stride;
    if (used_vert_count == 0 || skin_index_stride == 0) {
      continue;
    }
    const size_t skin_weight_stride = prim_info.skin_weight_stride;
    const size_t influence_count =
        std::min(skin_index_stride, skin_weight_stride);
    const int* index_it = prim_info.skin_indices.data();
    const float* weight_it = prim_info.skin_weights.data();
    for (size_t used_vi = 0; used_vi != used_vert_count; ++used_vi) {
      for (size_t i = 0; i != influence_count; ++i) {
        const size_t joint_index = index_it[i];
        if (joint_index < joint_count && weight_it[i] > kSkinWeightZeroTol) {
          joints_used[joint_index] = true;
        }
      }
      index_it += skin_index_stride;
      weight_it += skin_weight_stride;
    }
  }
  return joints_used;
}

// Joint names in USD encode relative hierarchy information, so they must be
// fully qualified up to the skeleton root.
std::string GetJointName(
    const Gltf::Id* node_parents, Gltf::Id root_node_id, Gltf::Id node_id) {
  std::string joint_name;
  do {
    UFG_ASSERT_LOGIC(node_id != Gltf::Id::kNull);
    // Use a short name to prevent very large full names, to reduce file size
    // and make usda joint listings more legible.
    const std::string name = AppendNumber("n", Gltf::IdToIndex(node_id));
    joint_name = joint_name.empty() ? name : name + "/" + joint_name;
    node_id = node_parents[Gltf::IdToIndex(node_id)];
  } while (node_id != root_node_id);
  return joint_name;
}

// Mark the set of skins referenced at or under the given nodes.
size_t MarkSkinsUsed(
    const std::vector<Gltf::Id>& node_ids, const std::vector<Gltf::Node>& nodes,
    std::vector<bool>* skins_used) {
  size_t total = 0;
  for (const Gltf::Id node_id : node_ids) {
    const Gltf::Node& node = *UFG_VERIFY(Gltf::GetById(nodes, node_id));
    if (node.skin != Gltf::Id::kNull) {
      const size_t skin_index = Gltf::IdToIndex(node.skin);
      if (!(*skins_used)[skin_index]) {
        (*skins_used)[skin_index] = true;
        ++total;
      }
    }
    total += MarkSkinsUsed(node.children, nodes, skins_used);
  }
  return total;
}

// Merge skin infos into a single shared skeleton.
void MergeSkinInfos(
    const Gltf& gltf, const Gltf::Id* node_parents,
    const SkinInfo* used_skin_infos, size_t used_skin_count,
    SkinSrc* gltf_skin_srcs, size_t gltf_skin_count,
    SkinInfo* out_merged_skin_info) {
  // Get the common ancestor node for all skins.
  Gltf::Id root_node_id = used_skin_infos[0].root_node_id;
  for (size_t i = 1; i != used_skin_count; ++i) {
    root_node_id = GetCommonAncestor(node_parents, root_node_id,
                                     used_skin_infos[i].root_node_id);
  }

  // Get the set of nodes used by all skins.
  const size_t node_count = gltf.nodes.size();
  std::vector<bool> nodes_used(node_count, false);
  for (size_t i = 0; i != used_skin_count; ++i) {
    const SkinInfo& skin_info = used_skin_infos[i];
    for (const Gltf::Id node_id : skin_info.ujoint_to_node_map) {
      nodes_used[Gltf::IdToIndex(node_id)] = true;
    }
  }

  // Generate new ujoint_to_node_map from the set of used nodes.
  std::vector<Gltf::Id> ujoint_to_node_map;
  std::vector<uint16_t> node_to_ujoint_map(
      node_count, SkinInfluence::Unused());
  for (size_t node_i = 0; node_i != node_count; ++node_i) {
    if (nodes_used[node_i]) {
      const size_t ujoint_index = ujoint_to_node_map.size();
      ujoint_to_node_map.push_back(Gltf::IndexToId(node_i));
      node_to_ujoint_map[node_i] = static_cast<uint16_t>(ujoint_index);
    }
  }
  const size_t ujoint_count = ujoint_to_node_map.size();

  // Merge joint parameters.
  std::vector<bool> ujoint_inited(ujoint_count, false);
  VtArray<TfToken> ujoint_names(ujoint_count);
  VtArray<GfMatrix4d> rest_mats(ujoint_count);
  VtArray<GfMatrix4d> bind_mats(ujoint_count);
  size_t ujoint_init_total = 0;
  for (size_t uskin_i = 0; uskin_i != used_skin_count; ++uskin_i) {
    const SkinInfo& skin_info = used_skin_infos[uskin_i];
    const size_t used_ujoint_count = skin_info.ujoint_to_node_map.size();
    for (size_t ujoint_i = 0; ujoint_i != used_ujoint_count; ++ujoint_i) {
      const Gltf::Id node_id = skin_info.ujoint_to_node_map[ujoint_i];
      const size_t ujoint_index =
          node_to_ujoint_map[Gltf::IdToIndex(node_id)];
      if (ujoint_inited[ujoint_index]) {
        continue;
      }
      ujoint_inited[ujoint_index] = true;
      const std::string joint_name =
          GetJointName(node_parents, root_node_id, node_id);
      ujoint_names[ujoint_index] = TfToken(joint_name);
      rest_mats[ujoint_index] = skin_info.rest_mats[ujoint_i];
      bind_mats[ujoint_index] = skin_info.bind_mats[ujoint_i];
      ++ujoint_init_total;
    }
  }
  UFG_ASSERT_LOGIC(ujoint_init_total == ujoint_count);

  // Fixup indices per gltf skin.
  for (size_t gskin_i = 0; gskin_i != gltf_skin_count; ++gskin_i) {
    SkinSrc& skin_src = gltf_skin_srcs[gskin_i];
    const SkinInfo& skin_info = used_skin_infos[skin_src.used_skin_index];
    skin_src.used_skin_index = 0;  // It now references the single merged skin.
    const size_t gjoint_count = skin_src.gjoint_to_ujoint_map.size();
    for (size_t gjoint_i = 0; gjoint_i != gjoint_count; ++gjoint_i) {
      const uint16_t used_ujoint_index =
          skin_src.gjoint_to_ujoint_map[gjoint_i];
      if (used_ujoint_index == SkinInfluence::kUnused) {
        continue;
      }
      const Gltf::Id node_id =
          skin_info.ujoint_to_node_map[used_ujoint_index];
      const uint16_t ujoint_index =
          node_to_ujoint_map[Gltf::IdToIndex(node_id)];
      skin_src.gjoint_to_ujoint_map[gjoint_i] =
          static_cast<uint16_t>(ujoint_index);
    }
  }

  out_merged_skin_info->name = used_skin_infos[0].name;
  out_merged_skin_info->root_node_id = root_node_id;
  out_merged_skin_info->ujoint_to_node_map.swap(ujoint_to_node_map);
  out_merged_skin_info->ujoint_names.swap(ujoint_names);
  out_merged_skin_info->rest_mats.swap(rest_mats);
  out_merged_skin_info->bind_mats.swap(bind_mats);
}
}  // namespace

void SkinBinding::SortInfluencesByWeight() {
  if (influences[0].weight < influences[1].weight)
    std::swap(influences[0], influences[1]);
  if (influences[2].weight < influences[3].weight)
    std::swap(influences[2], influences[3]);
  if (influences[0].weight < influences[2].weight)
    std::swap(influences[0], influences[2]);
  if (influences[1].weight < influences[3].weight)
    std::swap(influences[1], influences[3]);
  if (influences[1].weight < influences[2].weight)
    std::swap(influences[1], influences[2]);
}

void SkinBinding::Normalize(size_t joint_count) {
  // Clear indices that have 0 weight or are out-of-range.
  // * The latter probably shouldn't happen for a valid GLTF, but it doesn't
  //   hurt to be sure.
  float weight_total = 0.0f;
  for (SkinInfluence& influence : influences) {
    if (influence.weight <= kSkinWeightZeroTol ||
        influence.index >= joint_count) {
      influence.index = SkinInfluence::kUnused;
      influence.weight = 0.0f;
    } else {
      weight_total += influence.weight;
    }
  }

  // Sort weights greatest-to-least.
  SortInfluencesByWeight();

  // Scale weights so they sum to 1.0.
  const float weight_scale = weight_total == 0.0f ? 1.0f : 1.0f / weight_total;
  for (SkinInfluence& influence : influences) {
    if (influence.index != SkinInfluence::kUnused) {
      influence.weight *= weight_scale;
    }
  }
}

void SkinBinding::Assign(
    const int* indices, size_t index_count,
    const float* weights, size_t weight_count, size_t joint_count) {
  for (size_t i = 0; i != kInfluenceMax; ++i) {
    SkinInfluence& influence = influences[i];
    influence.index = i < index_count ? indices[i] : SkinInfluence::kUnused;
    influence.weight = i < weight_count ? weights[i] : 0.0f;
  }
  Normalize(joint_count);
}

size_t SkinBinding::CountUsed() const {
  size_t amount = 0;
  for (const SkinInfluence& influence : influences) {
    if (influence.index != SkinInfluence::kUnused) {
      ++amount;
    }
  }
  return amount;
}

void GetSkinInfo(
    const Gltf& gltf, const std::vector<MeshInfo>& mesh_infos, Gltf::Id skin_id,
    const Gltf::Id* node_parents, const std::vector<Gltf::Id>& scene_nodes,
    const std::vector<bool>* force_nodes_used, GltfCache* gltf_cache,
    SkinInfo* out_skin_info,
    std::vector<uint16_t>* out_gjoint_to_ujoint_map) {
  const Gltf::Skin& skin = *UFG_VERIFY(Gltf::GetById(gltf.skins, skin_id));
  const size_t gjoint_count = skin.joints.size();

  // Determine which joints are referenced by any mesh.
  std::vector<bool> gjoints_used(gjoint_count, false);
  for (const Gltf::Id node_id : scene_nodes) {
    const Gltf::Node& node = *UFG_VERIFY(Gltf::GetById(gltf.nodes, node_id));
    if (node.mesh != Gltf::Id::kNull && node.skin == skin_id) {
      const std::vector<bool> mesh_used = GetJointsUsed(
          gltf, mesh_infos, node.mesh, gjoint_count, gltf_cache);
      for (size_t gjoint_i = 0; gjoint_i != gjoint_count; ++gjoint_i) {
        gjoints_used[gjoint_i] = gjoints_used[gjoint_i] | mesh_used[gjoint_i];
      }
    }
  }

  // Flag joints as used for certain nodes.
  if (force_nodes_used) {
    for (size_t gjoint_i = 0; gjoint_i != gjoint_count; ++gjoint_i) {
      const size_t node_i = Gltf::IdToIndex(skin.joints[gjoint_i]);
      if ((*force_nodes_used)[node_i]) {
        gjoints_used[gjoint_i] = true;
      }
    }
  }

  // Get the subset of nodes referenced by both the skin and the animation.
  // Also choose skeleton root as the lowest node common to both the joints and
  // the node at which the skin is referenced (ref_node_id).
  const size_t node_count = gltf.nodes.size();
  std::vector<bool> nodes_used(node_count, false);

  std::vector<uint16_t> node_to_gjoint_map(
      node_count, SkinInfluence::Unused());
  for (size_t gjoint_i = 0; gjoint_i != gjoint_count; ++gjoint_i) {
    const size_t node_i = Gltf::IdToIndex(skin.joints[gjoint_i]);
    node_to_gjoint_map[node_i] = static_cast<uint16_t>(gjoint_i);
  }

  // Find the common ancestor of all used GLTF joints for use as the skeleton
  // root.
  // * Note, we can't use skin.skeleton for this because it's unreliable (some
  //   GLTFs don't set it or set it incorrectly).
  constexpr Gltf::Id kIdUnset =
      static_cast<Gltf::Id>(static_cast<int>(Gltf::Id::kNull) - 1);
  Gltf::Id root_node_id = kIdUnset;
  for (size_t gjoint_i = 0; gjoint_i != gjoint_count; ++gjoint_i) {
    const Gltf::Id node_id = skin.joints[gjoint_i];
    const size_t node_i = Gltf::IdToIndex(node_id);
    if (gjoints_used[gjoint_i] && !nodes_used[node_i]) {
      nodes_used[node_i] = true;
      root_node_id = root_node_id == kIdUnset ?
          node_id : GetCommonAncestor(node_parents, root_node_id, node_id);
    }
  }
  if (root_node_id == kIdUnset) {
    root_node_id = Gltf::Id::kNull;
  }
  if (root_node_id != Gltf::Id::kNull) {
    // If the top level ancestor of all joints is itself an used joint, move the
    // root up the hierarchy one level so the skeleton is anchored under a
    // non-joint transform.  This prevents us from doubly applying the transform
    // for the top-level joint.
    const size_t root_node_index = Gltf::IdToIndex(root_node_id);
    const size_t gjoint_i = node_to_gjoint_map[root_node_index];
    if (gjoint_i != SkinInfluence::kUnused && gjoints_used[gjoint_i]) {
      root_node_id = node_parents[root_node_index];
    }
  }

  // If a joint is used, flag all of its ancestors as used too so there are no
  // gaps in the hierarchy.
  for (size_t node_i = 0; node_i != node_count; ++node_i) {
    if (!nodes_used[node_i]) {
      continue;
    }
    Gltf::Id parent_node_id = Gltf::IndexToId(node_i);
    while (parent_node_id != root_node_id) {
      nodes_used[Gltf::IdToIndex(parent_node_id)] = true;
      parent_node_id = node_parents[Gltf::IdToIndex(parent_node_id)];
    }
  }

  // Get mapping from USD-joints to nodes.
  std::vector<Gltf::Id> ujoint_to_node_map;
  for (size_t node_i = 0; node_i != node_count; ++node_i) {
    if (nodes_used[node_i]) {
      ujoint_to_node_map.push_back(Gltf::IndexToId(node_i));
    }
  }
  const size_t ujoint_count = ujoint_to_node_map.size();

  // Sort USD-joints by hierarchy (for whatever reason, GLTF nodes aren't
  // already sorted this way). This is required by the iOS viewer (and is likely
  // to improve compatibility in general).
  std::sort(ujoint_to_node_map.begin(), ujoint_to_node_map.end(),
            NodeIdTreeLess(node_parents, node_count));

  // Get mapping from nodes to USD-joints.
  std::vector<uint16_t> node_to_ujoint_map(
      node_count, SkinInfluence::Unused());
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    const Gltf::Id node_id = ujoint_to_node_map[ujoint_i];
    node_to_ujoint_map[Gltf::IdToIndex(node_id)] =
        static_cast<uint16_t>(ujoint_i);
  }

  // Get a mapping between original GLTF-joint indices and USD-joint indices.
  std::vector<uint16_t> gjoint_to_ujoint_map(
      gjoint_count, SkinInfluence::Unused());
  std::vector<uint16_t> ujoint_to_gjoint_map(
      ujoint_count, SkinInfluence::Unused());
  for (size_t gjoint_i = 0; gjoint_i != gjoint_count; ++gjoint_i) {
    const Gltf::Id node_id = skin.joints[gjoint_i];
    const uint16_t ujoint_i =
        *UFG_VERIFY(Gltf::GetById(node_to_ujoint_map, node_id));
    gjoint_to_ujoint_map[gjoint_i] = ujoint_i;
    if (ujoint_i != SkinInfluence::kUnused) {
      ujoint_to_gjoint_map[ujoint_i] = static_cast<uint16_t>(gjoint_i);
    }
  }

  // Get fully-qualified USD-joint names relative to the root. USD uses these to
  // infer hierarchy.
  VtArray<TfToken> ujoint_names(ujoint_count);
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    Gltf::Id node_id = ujoint_to_node_map[ujoint_i];
    const std::string joint_name =
        GetJointName(node_parents, root_node_id, node_id);
    ujoint_names[ujoint_i] = TfToken(joint_name);
  }

  // Get rest-pose matrices.
  VtArray<GfMatrix4d> rest_mats(ujoint_count);
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    const Gltf::Id node_id = ujoint_to_node_map[ujoint_i];
    const Gltf::Node& node = *UFG_VERIFY(Gltf::GetById(gltf.nodes, node_id));
    rest_mats[ujoint_i] = node.is_matrix
        ? ToMatrix4d(node.matrix)
        : SrtToMatrix4d(node.scale, node.rotation, node.translation);
  }

  // Get bind-pose matrices.
  std::vector<GfMatrix4f> gjoint_inv_bind_mats;
  if (skin.inverseBindMatrices != Gltf::Id::kNull) {
    CopyAccessorToVectors(gltf, skin.inverseBindMatrices, gltf_cache,
                          &gjoint_inv_bind_mats);
    UFG_ASSERT_FORMAT(gjoint_inv_bind_mats.size() == skin.joints.size());
  } else {
    gjoint_inv_bind_mats.resize(gjoint_count);
    for (GfMatrix4f& inv_bind_mat : gjoint_inv_bind_mats) {
      inv_bind_mat.SetIdentity();
    }
  }
  VtArray<GfMatrix4d> bind_mats(ujoint_count);
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    const size_t gjoint_i = ujoint_to_gjoint_map[ujoint_i];
    if (gjoint_i == SkinInfluence::kUnused) {
      // This USD joint has no mapping to a GLTF joint, which means it is not
      // directly referenced in the skin (it is only indirectly referenced due
      // to hierarchy). Just use the identity because the flattened joint
      // transform is not needed for rendering.
      bind_mats[ujoint_i].SetIdentity();
    } else {
      bind_mats[ujoint_i] =
          GfMatrix4d(gjoint_inv_bind_mats[gjoint_i]).GetInverse();
    }
  }

  out_skin_info->name = skin.name;
  out_skin_info->root_node_id = root_node_id;
  out_skin_info->ujoint_to_node_map.swap(ujoint_to_node_map);
  out_skin_info->ujoint_names.swap(ujoint_names);
  out_skin_info->rest_mats.swap(rest_mats);
  out_skin_info->bind_mats.swap(bind_mats);
  out_gjoint_to_ujoint_map->swap(gjoint_to_ujoint_map);
}

void GetUsedSkinInfos(
    const Gltf& gltf, const std::vector<MeshInfo>& mesh_infos,
    const Gltf::Id* node_parents, const std::vector<Gltf::Id>& scene_nodes,
    const std::vector<bool>* force_nodes_used, bool merge,
    GltfCache* gltf_cache,
    std::vector<SkinInfo>* out_used_skin_infos,
    std::vector<SkinSrc>* out_gltf_skin_srcs) {
  // Determine which skins are referenced by any node in the exported hierarchy.
  const size_t gltf_skin_count = gltf.skins.size();
  std::vector<bool> gltf_skins_used(gltf_skin_count, false);
  const size_t used_skin_count =
      MarkSkinsUsed(scene_nodes, gltf.nodes, &gltf_skins_used);

  // Populate per-skin info.
  std::vector<SkinSrc> gltf_skin_srcs(gltf_skin_count);
  std::vector<SkinInfo> used_skin_infos(used_skin_count);
  size_t used_skin_index = 0;
  for (size_t gskin_i = 0; gskin_i != gltf_skin_count; ++gskin_i) {
    if (gltf_skins_used[gskin_i]) {
      SkinSrc& skin_src = gltf_skin_srcs[gskin_i];
      skin_src.used_skin_index = static_cast<uint32_t>(used_skin_index);
      SkinInfo& used_skin_info = used_skin_infos[used_skin_index];
      GetSkinInfo(gltf, mesh_infos, Gltf::IndexToId(gskin_i), node_parents,
                  scene_nodes, force_nodes_used, gltf_cache,
                  &used_skin_info, &skin_src.gjoint_to_ujoint_map);
      ++used_skin_index;
    }
  }

  // Optionally merge skins.
  if (merge && used_skin_count > 1) {
    std::vector<SkinInfo> merged_skin_infos(1);
    MergeSkinInfos(gltf, node_parents, used_skin_infos.data(), used_skin_count,
                   gltf_skin_srcs.data(), gltf_skin_count,
                   &merged_skin_infos[0]);
    used_skin_infos.swap(merged_skin_infos);
  }

  out_used_skin_infos->swap(used_skin_infos);
  out_gltf_skin_srcs->swap(gltf_skin_srcs);
}

bool GetSkinData(
    const int* indices, size_t index_stride,
    const float* weights, size_t weight_stride,
    size_t node_count, size_t vert_count,
    const uint16_t* gjoint_to_ujoint_map, size_t gjoint_count,
    SkinData* out_skin_data) {
  UFG_ASSERT_FORMAT(index_stride > 0 &&
                    index_stride <= SkinBinding::kInfluenceMax);
  UFG_ASSERT_FORMAT(weight_stride > 0 &&
                    weight_stride <= SkinBinding::kInfluenceMax);

  // Copy to skin bindings and keep track of which nodes are actually
  // referenced.
  std::vector<SkinBinding> bindings(vert_count);
  size_t influence_count = 0;
  {
    const int* index_it = indices;
    const float* weight_it = weights;
    for (size_t i = 0; i != vert_count; ++i) {
      SkinBinding& binding = bindings[i];
      binding.Assign(
          index_it, index_stride, weight_it, weight_stride, node_count);
      influence_count = std::max(influence_count, binding.CountUsed());
      index_it += index_stride;
      weight_it += weight_stride;
    }
  }

  // Remap influences from node to joint indices, and determine if the skin is
  // effectively rigid (all verts bound to a single influence).
  uint16_t first_index = SkinInfluence::kUnused;
  bool is_rigid = true;
  for (SkinBinding& binding : bindings) {
    for (SkinInfluence& influence : binding.influences) {
      if (influence.index != SkinInfluence::kUnused) {
        UFG_ASSERT_LOGIC(influence.index < gjoint_count);
        UFG_ASSERT_LOGIC(gjoint_to_ujoint_map[influence.index] !=
                         SkinInfluence::kUnused);
        influence.index = gjoint_to_ujoint_map[influence.index];
        if (first_index == SkinInfluence::kUnused) {
          first_index = influence.index;
        } else if (influence.index != first_index) {
          is_rigid = false;
        }
      }
    }
  }

  out_skin_data->influence_count = static_cast<uint8_t>(influence_count);
  out_skin_data->is_rigid = is_rigid;
  out_skin_data->bindings.swap(bindings);
  return true;
}

void GetSkinJointMatricesForNormals(
    const SkinInfo& skin_info, size_t node_count, const Gltf::Id* node_parents,
    size_t ujoint_count, const GfQuatf* rots, const GfVec3f* scales,
    GfMatrix3f* out_norm_mats) {
  std::vector<uint16_t> node_to_ujoint_map(
      node_count, SkinInfluence::Unused());
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    const Gltf::Id node_id = skin_info.ujoint_to_node_map[ujoint_i];
    node_to_ujoint_map[Gltf::IdToIndex(node_id)] =
        static_cast<uint16_t>(ujoint_i);
  }

  std::vector<uint16_t> ujoint_parents(ujoint_count);
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    const Gltf::Id node_id = skin_info.ujoint_to_node_map[ujoint_i];
    const Gltf::Id parent_node_id = node_parents[Gltf::IdToIndex(node_id)];
    ujoint_parents[ujoint_i] = parent_node_id == Gltf::Id::kNull
        ? SkinInfluence::kUnused
        : node_to_ujoint_map[Gltf::IdToIndex(parent_node_id)];
  }

  // Get flattened joint matrices, omitting translation since it doesn't affect
  // normals.
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    const GfMatrix3f bind_mat(ToMatrix3f(skin_info.bind_mats[ujoint_i]));
    const GfMatrix3f inv_bind_mat = bind_mat.GetInverse();
    const GfQuatf rot = rots ? rots[ujoint_i] : GfQuatf::GetIdentity();
    const GfVec3f scale = scales ? scales[ujoint_i] : GfVec3f(1.0f);
    const GfMatrix3f anim_mat = GfMatrix3f(scale) * GfMatrix3f(rot);

    // Skinned vertices start in bind-pose space, so we need to convert them to
    // joint-space before animating them.
    GfMatrix3f mat = inv_bind_mat * anim_mat;

    // Apply parent transform. Since the joints are ordered hierarchically, this
    // has the effect of applying all ancestor transforms in the skeleton.
    const uint16_t parent_ujoint_i = ujoint_parents[ujoint_i];
    if (parent_ujoint_i != SkinInfluence::kUnused) {
      UFG_ASSERT_LOGIC(parent_ujoint_i < ujoint_i);
      const GfMatrix4d& parent_bind_mat4 = skin_info.bind_mats[parent_ujoint_i];
      const GfMatrix3f parent_bind_mat(ToMatrix3f(parent_bind_mat4));
      const GfMatrix3f& parent_mat = out_norm_mats[parent_ujoint_i];
      // mat is in joint-space, but parent_mat transforms from bind-space. So
      // multiply parent_bind_mat to compensate.
      mat *= parent_bind_mat * parent_mat;
    }

    out_norm_mats[ujoint_i] = mat;
  }

  // Apply inverse-transpose so matrices are suitable for transforming normals
  // (rather than positions).
  // See: https://paroj.github.io/gltut/Illumination/Tut09%20Normal%20Transformation.html
  for (size_t ujoint_i = 0; ujoint_i != ujoint_count; ++ujoint_i) {
    // Convert the position matrix (rotational component) to the normal matrix.
    const GfMatrix3f pos_mat = out_norm_mats[ujoint_i];
    const GfMatrix3f norm_mat = pos_mat.GetInverse().GetTranspose();
    out_norm_mats[ujoint_i] = norm_mat;
  }
}

void SkinNormals(
    const GfMatrix3f* norm_joint_mats, const GfVec3f* norms, size_t norm_count,
    const SkinBinding* skin_bindings,
    GfVec3f* out_norms) {
  for (size_t norm_index = 0; norm_index != norm_count; ++norm_index) {
    const GfVec3f& norm = norms[norm_index];
    const SkinBinding& skin_binding = skin_bindings[norm_index];
    GfVec3f out_norm(0.0f);
    for (const SkinInfluence& influence : skin_binding.influences) {
      if (influence.index == SkinInfluence::kUnused) {
        break;
      }
      const GfVec3f xform_norm = norm * norm_joint_mats[influence.index];
      out_norm += xform_norm.GetNormalized() * influence.weight;
    }
    out_norm.Normalize();
    out_norms[norm_index] = out_norm;
  }
}
}  // namespace ufg
