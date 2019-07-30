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

#ifndef UFG_CONVERT_CONVERTER_H_
#define UFG_CONVERT_CONVERTER_H_

#include <string>
#include <vector>
#include "common/common_util.h"
#include "common/config.h"
#include "convert/convert_common.h"
#include "convert/convert_context.h"
#include "convert/convert_util.h"
#include "convert/materializer.h"
#include "gltf/gltf.h"
#include "process/animation.h"
#include "process/image.h"
#include "process/mesh.h"
#include "process/skin.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/base/tf/token.h"
#include "pxr/base/vt/array.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/shader.h"

namespace ufg {
using PXR_NS::SdfLayerRefPtr;
using PXR_NS::SdfPath;
using PXR_NS::SdfValueTypeName;
using PXR_NS::TfToken;
using PXR_NS::UsdShadeMaterial;
using PXR_NS::UsdShadeShader;
using PXR_NS::UsdStageRefPtr;
using PXR_NS::VtArray;

class Converter {
 public:
  void Reset(Logger* logger);
  bool Convert(const ConvertSettings& settings, const Gltf& gltf,
               GltfStream* gltf_stream, const std::string& src_dir,
               const std::string& dst_dir, const SdfLayerRefPtr& layer,
               Logger* logger);
  const std::vector<std::string>& GetWritten() const {
    return materializer_.GetWritten();
  }
  const std::vector<std::string>& GetCreatedDirectories() const {
    return materializer_.GetCreatedDirectories();
  }

 private:
  struct SkinnedMeshContext {
    SdfPath skeleton_path;
    SdfPath anim_path;
    const uint16_t* gjoint_to_ujoint_map;
    size_t gjoint_count;
    const GfMatrix3f* bake_norm_mats;  // Null if not baking normals.
  };

  ConvertContext cc_;
  Pass curr_pass_;
  Materializer materializer_;

  // TODO: A node can exist in multiple places in the hierarchy, so
  // each node could have multiple parents. I haven't found any models that
  // actually do this, though.
  std::vector<Gltf::Id> node_parents_;

  NodeInfo root_node_info_;
  std::vector<NodeInfo> node_infos_;
  std::vector<MeshInfo> mesh_infos_;
  std::vector<SkinInfo> used_skin_infos_;
  std::vector<SkinSrc> gltf_skin_srcs_;
  AnimInfo anim_info_;
  UsdShadeMaterial debug_bone_material_;

  void CreateDebugBoneMesh(const SdfPath& parent_path, bool reverse_winding);
  void CreateSkeleton(const SdfPath& path, const SkinInfo& skin_info);
  GfVec3f CreateSkelAnim(const SdfPath& path, const SkinInfo& skin_info,
                         const AnimInfo& anim_info,
                         std::vector<GfQuatf>* out_frame0_rots,
                         std::vector<GfVec3f>* out_frame0_scales);
  void CreateMesh(size_t mesh_index, const SdfPath& parent_path,
                  bool reverse_winding,
                  const SkinnedMeshContext* skinned_mesh_context);
  void CreateSkinnedMeshes(const SdfPath& parent_path,
                           const std::vector<Gltf::Id>& node_ids,
                           bool reverse_winding);
  void CreateNodeHierarchy(Gltf::Id node_id, const SdfPath& parent_path,
                           const GfMatrix4d& parent_world_mat);
  void CreateNodes(const std::vector<Gltf::Id>& root_nodes);
  void CreateAnimation(const AnimInfo& anim_info);
  void ConvertImpl(const ConvertSettings& settings, const Gltf& gltf,
                   GltfStream* gltf_stream, const std::string& src_dir,
                   const std::string& dst_dir, const SdfLayerRefPtr& layer,
                   Logger* logger);
};

}  // namespace ufg

#endif  // UFG_CONVERT_CONVERTER_H_
