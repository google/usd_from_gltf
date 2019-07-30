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

#ifndef UFG_PROCESS_PROCESS_UTIL_H_
#define UFG_PROCESS_PROCESS_UTIL_H_

#include <vector>
#include "common/common.h"
#include "common/logging.h"
#include "gltf/gltf.h"
#include "pxr/base/vt/array.h"

namespace ufg {
using PXR_NS::VtArray;

std::vector<Gltf::Id> GetNodeParents(const std::vector<Gltf::Node>& nodes);

// Recursively mark this node and its descendants as affected.
void MarkAffectedNodes(
    const std::vector<Gltf::Node>& nodes, Gltf::Id node_id,
    std::vector<bool>* affected_node_ids);

std::vector<Gltf::Id> GetSceneRootNodes(
    const Gltf& gltf, Gltf::Id scene_id, const Gltf::Id* node_parents,
    const std::vector<std::string>& remove_node_prefixes);
std::vector<Gltf::Id> GetNodesUnderRoots(
    const Gltf& gltf, const std::vector<Gltf::Id>& root_nodes,
  const std::vector<std::string>& remove_node_prefixes);

inline size_t GetDepth(const Gltf::Id* node_parents, Gltf::Id node_id) {
  size_t depth = 0;
  while (node_id != Gltf::Id::kNull) {
    ++depth;
    node_id = node_parents[Gltf::IdToIndex(node_id)];
  }
  return depth;
}

inline Gltf::Id TraverseUp(
    const Gltf::Id* node_parents, Gltf::Id node_id, size_t height) {
  for (; height; --height) {
    node_id = node_parents[Gltf::IdToIndex(node_id)];
  }
  return node_id;
}

inline Gltf::Id GetRoot(const Gltf::Id* node_parents, Gltf::Id node_id) {
  while (node_id != Gltf::Id::kNull) {
    node_id = node_parents[Gltf::IdToIndex(node_id)];
  }
  return node_id;
}

inline bool IsEqualOrUnder(
    const Gltf::Id* node_parents, Gltf::Id ancestor_id, Gltf::Id node_id) {
  while (node_id != Gltf::Id::kNull) {
    if (node_id == ancestor_id) {
      return true;
    }
    node_id = node_parents[Gltf::IdToIndex(node_id)];
  }
  return false;
}

Gltf::Id GetCommonAncestor(
    const Gltf::Id* node_parents, Gltf::Id node_id0, Gltf::Id node_id1);

// Get indices of root joints (i.e. joints that are not a child of another
// joint).
std::vector<uint16_t> GetJointRoots(
    const Gltf::Id* node_parents, size_t node_count,
    const std::vector<Gltf::Id>& joint_to_node_map);

// Get the full path of a node from the root.
// * The path is filled-in from end to begin. The caller provides the end, and
//   the function returns the begin.
inline Gltf::Id* GetNodePath(
    const Gltf::Id* node_parents, Gltf::Id node_id, Gltf::Id* out_end) {
  Gltf::Id* begin = out_end;
  while (node_id != Gltf::Id::kNull) {
    *--begin = node_id;
    node_id = node_parents[Gltf::IdToIndex(node_id)];
  }
  return begin;
}

struct NodeIdTreeLess {
  const Gltf::Id* node_parents;
  mutable std::vector<Gltf::Id> path0_buffer;
  mutable std::vector<Gltf::Id> path1_buffer;
  NodeIdTreeLess(const Gltf::Id* node_parents, size_t node_count)
      : node_parents(node_parents),
        path0_buffer(node_count),
        path1_buffer(node_count) {}

  inline bool operator()(Gltf::Id node_id0, Gltf::Id node_id1) const {
    Gltf::Id* const path0_end = path0_buffer.data() + path0_buffer.size();
    Gltf::Id* const path1_end = path1_buffer.data() + path1_buffer.size();
    const Gltf::Id* const path0 =
        GetNodePath(node_parents, node_id0, path0_end);
    const Gltf::Id* const path1 =
        GetNodePath(node_parents, node_id1, path1_end);
    const size_t path0_len = path0_end - path0;
    const size_t path1_len = path1_end - path1;
    const size_t min_len = std::min(path0_len, path1_len);
    for (size_t i = 0; i != min_len; ++i) {
      if (path0[i] != path1[i]) {
        return path0[i] < path1[i];
      }
    }
    return path0_len < path1_len;
  }
};

}  // namespace ufg

#endif  // UFG_PROCESS_PROCESS_UTIL_H_
