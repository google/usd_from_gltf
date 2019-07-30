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

#include "process/process_util.h"

#include "common/logging.h"

namespace ufg {
namespace {
bool IsIncludedNode(const Gltf& gltf, Gltf::Id node_id,
                    const std::vector<std::string>& remove_node_prefixes) {
  if (remove_node_prefixes.empty()) {
    return true;
  }
  const Gltf::Node& node = *UFG_VERIFY(Gltf::GetById(gltf.nodes, node_id));
  return !Gltf::StringBeginsWithAnyCI(
      node.name.c_str(), node.name.length(), remove_node_prefixes);
}

void MarkUsedDescendants(
    const Gltf& gltf, const Gltf::Id root_id,
    const std::vector<std::string>& remove_node_prefixes,
    std::vector<bool>* nodes_used) {
  if (!IsIncludedNode(gltf, root_id, remove_node_prefixes)) {
    return;
  }
  const Gltf::Node& root_node = *UFG_VERIFY(Gltf::GetById(gltf.nodes, root_id));
  (*nodes_used)[Gltf::IdToIndex(root_id)] = true;
  for (const Gltf::Id child_id : root_node.children) {
    MarkUsedDescendants(gltf, child_id, remove_node_prefixes, nodes_used);
  }
}
}  // namespace

std::vector<Gltf::Id> GetNodeParents(const std::vector<Gltf::Node>& nodes) {
  const size_t node_count = nodes.size();
  std::vector<Gltf::Id> node_parents(node_count, Gltf::Id::kNull);
  for (size_t node_index = 0; node_index != node_count; ++node_index) {
    const Gltf::Node& node = nodes[node_index];
    for (const Gltf::Id child_id : node.children) {
      // TODO: Is it possible for a node to be reused in multiple
      // places in the hierarchy? Not sure if there's a way to express this in
      // USD.
      const size_t child_index = Gltf::IdToIndex(child_id);
      UFG_ASSERT_FORMAT(node_parents[child_index] == Gltf::Id::kNull);
      node_parents[child_index] = static_cast<Gltf::Id>(node_index);
    }
  }
  return node_parents;
}

Gltf::Id GetCommonAncestor(
    const Gltf::Id* node_parents, Gltf::Id node_id0, Gltf::Id node_id1) {
  if (node_id0 != node_id1) {
    const size_t depth0 = GetDepth(node_parents, node_id0);
    const size_t depth1 = GetDepth(node_parents, node_id1);
    const size_t depth_min = std::min(depth0, depth1);
    node_id0 = TraverseUp(node_parents, node_id0, depth0 - depth_min);
    node_id1 = TraverseUp(node_parents, node_id1, depth1 - depth_min);
    while (node_id0 != node_id1) {
      node_id0 = node_parents[Gltf::IdToIndex(node_id0)];
      node_id1 = node_parents[Gltf::IdToIndex(node_id1)];
    }
  }
  return node_id0;
}

std::vector<uint16_t> GetJointRoots(
    const Gltf::Id* node_parents, size_t node_count,
    const std::vector<Gltf::Id>& joint_to_node_map) {
  std::vector<bool> nodes_used(node_count, false);
  for (const Gltf::Id node_id : joint_to_node_map) {
    nodes_used[Gltf::IdToIndex(node_id)] = true;
  }

  std::vector<uint16_t> joint_roots;
  const size_t joint_count = joint_to_node_map.size();
  for (size_t joint_index = 0; joint_index != joint_count; ++joint_index) {
    const Gltf::Id node_id = joint_to_node_map[joint_index];
    Gltf::Id ancestor_node_id = node_parents[Gltf::IdToIndex(node_id)];
    while (ancestor_node_id != Gltf::Id::kNull) {
      if (nodes_used[Gltf::IdToIndex(ancestor_node_id)]) {
        break;
      }
      ancestor_node_id = node_parents[Gltf::IdToIndex(ancestor_node_id)];
    }
    if (ancestor_node_id == Gltf::Id::kNull) {
      joint_roots.push_back(static_cast<uint16_t>(joint_index));
    }
  }
  return joint_roots;
}

void MarkAffectedNodes(
    const std::vector<Gltf::Node>& nodes, Gltf::Id node_id,
    std::vector<bool>* affected_node_ids) {
  const size_t node_index = Gltf::IdToIndex(node_id);
  if ((*affected_node_ids)[node_index]) {
    return;
  }
  (*affected_node_ids)[node_index] = true;
  const Gltf::Node& node = nodes[node_index];
  for (const Gltf::Id child_node_id : node.children) {
    MarkAffectedNodes(nodes, child_node_id, affected_node_ids);
  }
}

std::vector<Gltf::Id> GetSceneRootNodes(
    const Gltf& gltf, Gltf::Id scene_id, const Gltf::Id* node_parents,
    const std::vector<std::string>& remove_node_prefixes) {
  // Flag used roots.
  const size_t node_count = gltf.nodes.size();
  std::vector<bool> root_nodes_used(node_count, false);
  if (scene_id == Gltf::Id::kNull) {
    // Get all root nodes.
    for (size_t node_index = 0; node_index != node_count; ++node_index) {
      if (node_parents[node_index] == Gltf::Id::kNull) {
        const Gltf::Id node_id = Gltf::IndexToId(node_index);
        if (IsIncludedNode(gltf, node_id, remove_node_prefixes)) {
          root_nodes_used[node_index] = true;
        }
      }
    }
  } else {
    // Get root nodes for a single scene.
    const Gltf::Scene& scene =
        *UFG_VERIFY(Gltf::GetById(gltf.scenes, scene_id));
    for (const Gltf::Id node_id : scene.nodes) {
      if (IsIncludedNode(gltf, node_id, remove_node_prefixes)) {
        root_nodes_used[Gltf::IdToIndex(node_id)] = true;
      }
    }
  }

  // Get the list of used roots.
  std::vector<Gltf::Id> root_nodes;
  for (size_t node_index = 0; node_index != node_count; ++node_index) {
    if (root_nodes_used[node_index]) {
      root_nodes.push_back(Gltf::IndexToId(node_index));
    }
  }
  return root_nodes;
}

std::vector<Gltf::Id> GetNodesUnderRoots(
    const Gltf& gltf, const std::vector<Gltf::Id>& root_nodes,
    const std::vector<std::string>& remove_node_prefixes) {
  const size_t node_count = gltf.nodes.size();
  std::vector<bool> nodes_used(node_count, false);
  for (const Gltf::Id root_id : root_nodes) {
    MarkUsedDescendants(gltf, root_id, remove_node_prefixes, &nodes_used);
  }

  size_t used_count = 0;
  for (const bool used : nodes_used) {
    if (used) {
      ++used_count;
    }
  }

  std::vector<Gltf::Id> used_nodes;
  used_nodes.reserve(used_count);
  for (size_t node_index = 0; node_index != node_count; ++node_index) {
    if (nodes_used[node_index]) {
      used_nodes.push_back(Gltf::IndexToId(node_index));
    }
  }
  return used_nodes;
}
}  // namespace ufg
