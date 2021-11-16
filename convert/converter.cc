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

#include "convert/converter.h"

#include "common/common_util.h"
#include "convert/convert_util.h"
#include "convert/tokens.h"
#include "process/access.h"
#include "process/mesh.h"
#include "process/process_util.h"
#include "process/skin.h"
#include "pxr/base/vt/value.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/attribute.h"
#include "pxr/usd/usd/modelAPI.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/mesh.h"
#include "pxr/usd/usdGeom/metrics.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/tokens.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdShade/materialBindingAPI.h"
#include "pxr/usd/usdSkel/animation.h"
#include "pxr/usd/usdSkel/bindingAPI.h"
#include "pxr/usd/usdSkel/root.h"
#include "pxr/usd/usdSkel/skeleton.h"

namespace ufg {
using PXR_NS::SdfAssetPath;
using PXR_NS::SdfValueTypeNames;
using PXR_NS::TfMakeValidIdentifier;
using PXR_NS::UsdAttribute;
using PXR_NS::UsdGeomMesh;
using PXR_NS::UsdGeomPrimvar;
using PXR_NS::UsdGeomScope;
using PXR_NS::UsdGeomSetStageUpAxis;
using PXR_NS::UsdGeomTokens;
using PXR_NS::UsdGeomXform;
using PXR_NS::UsdGeomXformOp;
using PXR_NS::UsdModelAPI;
using PXR_NS::UsdPrim;
using PXR_NS::UsdShadeInput;
using PXR_NS::UsdShadeMaterialBindingAPI;
using PXR_NS::UsdSkelAnimation;
using PXR_NS::UsdSkelBindingAPI;
using PXR_NS::UsdSkelRoot;
using PXR_NS::UsdSkelSkeleton;
using PXR_NS::UsdStage;
using PXR_NS::VtValue;

namespace {
// We omit animation keys nearly equal to the default.
const GfVec3f kDefaultTranslation(0.0f, 0.0f, 0.0f);
constexpr float kDefaultTranslationTol = 0.00001f;
const GfVec3f kDefaultEuler(0.0f, 0.0f, 0.0f);
constexpr float kDefaultEulerTol = 0.00001f;
const GfVec3f kDefaultScale(1.0f, 1.0f, 1.0f);
constexpr float kDefaultScaleTol = 0.00001f;

const char* const kPassNames[] = {
    "Meshes",         // kPassRigid
    "SkinnedMeshes",  // kPassSkinned
};
static_assert(UFG_ARRAY_SIZE(kPassNames) == kPassCount, "");

const char* const kDefaultAbsolutePath = "/default";

// Converts the filename to a valid absolute SdfPath by removing any extension,
// converting to a valid identifier, then prepending with '/'.
// See usd/pxr/base/tf/stringUtils.h:TfIsValidIdentifier for more details.
SdfPath MakeAbsolutePath(const std::string& filename) {
  UFG_ASSERT_LOGIC(filename.find('/') == std::string::npos);
  std::string name = filename.substr(0, filename.find_first_of('.'));
  if (name.empty()) {
    return SdfPath(kDefaultAbsolutePath);
  }
  return SdfPath("/" + TfMakeValidIdentifier(name));
}

Gltf::Id GetSceneId(const Gltf& gltf, const ConvertSettings& settings) {
  if (!settings.all_nodes) {
    const Gltf::Id export_scene_id = Gltf::IndexToId(settings.scene_index);
    if (Gltf::IsValidId(gltf.scenes, export_scene_id)) {
      return Gltf::IndexToId(settings.scene_index);
    } else if (Gltf::IsValidId(gltf.scenes, gltf.scene)) {
      return gltf.scene;
    }
  }
  return Gltf::Id::kNull;
}

Gltf::Id GetAnimId(const Gltf& gltf, const ConvertSettings& settings) {
  const Gltf::Id export_anim_id = Gltf::IndexToId(settings.anim_index);
  return Gltf::IsValidId(gltf.animations, export_anim_id) ? export_anim_id
                                                          : Gltf::Id::kNull;
}

void SetTranslationKeys(const UsdGeomXform& xform, const GfVec3f& initial_point,
                        const std::vector<float>& times,
                        const std::vector<GfVec3f>& points) {
  const size_t src_count = times.size();
  if (src_count == 0) {
    if (!NearlyEqual(initial_point, kDefaultTranslation,
                     kDefaultTranslationTol)) {
      const UsdGeomXformOp op =
          xform.AddTranslateOp(UsdGeomXformOp::PrecisionFloat);
      op.Set(initial_point);
    }
    return;
  }
  UFG_ASSERT_LOGIC(points.size() == src_count);
  TranslationPrunerStream stream(times.data(), points.data());
  PruneAnimationKeys(src_count, &stream);
  const UsdGeomXformOp op =
      xform.AddTranslateOp(UsdGeomXformOp::PrecisionFloat);
  if (stream.IsPrunedConstant()) {
    op.Set(stream.points[0]);
  } else {
    const size_t pruned_count = stream.times.size();
    for (size_t i = 0; i != pruned_count; ++i) {
      op.Set(stream.points[i], GetTimeCode(stream.times[i]));
    }
  }
}

void SetRotationKeys(const UsdGeomXform& xform, const GfQuatf& initial_point,
                     const std::vector<float>& times,
                     const std::vector<GfQuatf>& points) {
  const size_t quat_count = times.size();
  if (quat_count == 0) {
    const GfVec3f initial_euler = QuatToEuler(initial_point);
    if (!NearlyEqual(initial_euler, kDefaultEuler, kDefaultEulerTol)) {
      const UsdGeomXformOp op =
          xform.AddRotateXYZOp(UsdGeomXformOp::PrecisionFloat);
      op.Set(RadToDeg(initial_euler));
    }
    return;
  }
  UFG_ASSERT_LOGIC(points.size() == quat_count);

  // TODO: Set basis.
  const UsdGeomXformOp op =
      xform.AddRotateXYZOp(UsdGeomXformOp::PrecisionFloat);

  // Prune quaternions, then convert to Euler.
  QuatPrunerStream stream(times.data(), points.data());
  PruneAnimationKeys(quat_count, &stream);
  std::vector<float> euler_times;
  std::vector<GfVec3f> eulers;
  ConvertRotationKeys(stream.times, stream.points, &euler_times, &eulers);
  if (stream.IsPrunedConstant()) {
    op.Set(GfVec3f(RadToDeg(eulers[0])));
  } else {
    const size_t euler_count = euler_times.size();
    for (size_t i = 0; i != euler_count; ++i) {
      op.Set(GfVec3f(RadToDeg(eulers[i])), GetTimeCode(euler_times[i]));
    }
  }
}

void SetScaleKeys(const UsdGeomXform& xform, const GfVec3f& initial_point,
                  const std::vector<float>& times,
                  const std::vector<GfVec3f>& points) {
  const size_t src_count = times.size();
  if (src_count == 0) {
    if (!NearlyEqual(initial_point, kDefaultScale, kDefaultScaleTol)) {
      const UsdGeomXformOp op =
          xform.AddScaleOp(UsdGeomXformOp::PrecisionFloat);
      op.Set(initial_point);
    }
    return;
  }
  UFG_ASSERT_LOGIC(points.size() == src_count);
  ScalePrunerStream stream(times.data(), points.data());
  PruneAnimationKeys(src_count, &stream);
  const UsdGeomXformOp op = xform.AddScaleOp(UsdGeomXformOp::PrecisionFloat);
  if (stream.IsPrunedConstant()) {
    op.Set(stream.points[0]);
  } else {
    const size_t pruned_count = stream.times.size();
    for (size_t i = 0; i != pruned_count; ++i) {
      op.Set(stream.points[i], GetTimeCode(stream.times[i]));
    }
  }
}

template <typename Vec>
void SetConstantSkinKey(const UsdAttribute& attr, const VtArray<Vec>& points,
                        float time_min, float time_max) {
  // We have to add two keys for compatibility with Apple's viewer (a single
  // constant key will cause the mesh to be rendered unskinned).
  attr.Set(points, GetTimeCode(time_min));
  attr.Set(points, GetTimeCode(time_max));
}

void SetTranslationSkinKeys(
    const UsdSkelAnimation& skel_anim,
    const NodeInfo* const* joint_infos, size_t ujoint_count,
    const VtArray<GfVec3f>& rest_points, float time_min, float time_max) {
  std::vector<TranslationKey> keys;
  GenerateSkinAnimKeys(ujoint_count, joint_infos, &keys);
  const size_t key_count = keys.size();
  const UsdAttribute attr = skel_anim.CreateTranslationsAttr();
  if (key_count > 0) {
    TranslationKeyPrunerStream stream(keys.data());
    PruneAnimationKeys(key_count, &stream);
    if (stream.IsPrunedConstant()) {
      VtArray<GfVec3f> points;
      ToVtArray(stream.keys[0].p, &points);
      SetConstantSkinKey(attr, points, time_min, time_max);
    } else {
      for (const TranslationKey& key : stream.keys) {
        VtArray<GfVec3f> points;
        ToVtArray(key.p, &points);
        attr.Set(points, GetTimeCode(key.t));
      }
    }
    return;
  } else {
    SetConstantSkinKey(attr, rest_points, time_min, time_max);
  }
}

void SetRotationSkinKeys(
    const UsdSkelAnimation& skel_anim,
    const NodeInfo* const* joint_infos, size_t ujoint_count,
    const VtArray<GfQuatf>& rest_points, float time_min, float time_max,
    std::vector<GfQuatf>* out_frame0_rots) {
  std::vector<RotationKey> keys;
  GenerateSkinAnimKeys(ujoint_count, joint_infos, &keys);
  const size_t key_count = keys.size();
  const UsdAttribute attr = skel_anim.CreateRotationsAttr();
  if (key_count > 0) {
    // TODO: Subdivide large rotations to compensate for Nlerp
    // innaccuracy in the iOS viewer.
    RotationKeyPrunerStream stream(keys.data());
    PruneAnimationKeys(key_count, &stream);
    if (stream.IsPrunedConstant()) {
      VtArray<GfQuatf> points;
      ToVtArray(stream.keys[0].p, &points);
      SetConstantSkinKey(attr, points, time_min, time_max);
    } else {
      for (const RotationKey& key : stream.keys) {
        VtArray<GfQuatf> points;
        ToVtArray(key.p, &points);
        attr.Set(points, GetTimeCode(key.t));
      }
    }
    out_frame0_rots->swap(keys[0].p);
  } else {
    SetConstantSkinKey(attr, rest_points, time_min, time_max);
    const GfQuatf* const points = rest_points.data();
    out_frame0_rots->assign(points, points + ujoint_count);
  }
}

float GetNormalizedScale(float scale) {
  return NearlyEqual(scale, 0.0f, kPruneScaleComponent) ? 1.0f : scale;
}

GfVec3f GetNormalizedScale(const GfVec3f& scale) {
  return GfVec3f(GetNormalizedScale(scale[0]),
                 GetNormalizedScale(scale[1]),
                 GetNormalizedScale(scale[2]));
}

GfVec3f SetScaleSkinKeys(
    const UsdSkelAnimation& skel_anim,
    const NodeInfo* const* joint_infos, size_t ujoint_count,
    const std::vector<GfVec3f>& rest_points, float time_min, float time_max,
    bool normalize, const std::vector<uint16_t>& ujoint_roots,
    std::vector<GfVec3f>* out_frame0_scales) {
  GfVec3f root_scale(1.0f);
  std::vector<ScaleKey> keys;
  GenerateSkinAnimKeys(ujoint_count, joint_infos, &keys);
  const size_t key_count = keys.size();
  const UsdAttribute attr = skel_anim.CreateScalesAttr();
  if (key_count > 0) {
    ScaleKeyPrunerStream stream(keys.data());
    PruneAnimationKeys(key_count, &stream);
    if (normalize) {
      // Normalize animation so joint0 has scale 1.0.
      const ScaleKey& key0 = stream.keys[0];
      root_scale = GetNormalizedScale(key0.p[0]);
      const GfVec3f recip_scale = Recip(root_scale);
      for (ScaleKey& key : stream.keys) {
        for (const size_t ujoint_index : ujoint_roots) {
          key.p[ujoint_index] = Multiply(key.p[ujoint_index], recip_scale);
        }
      }
    }
    if (stream.IsPrunedConstant()) {
      VtArray<GfVec3h> points;
      ToVtArray(stream.keys[0].p, &points);
      SetConstantSkinKey(attr, points, time_min, time_max);
    } else {
      for (const ScaleKey& key : stream.keys) {
        VtArray<GfVec3h> points;
        ToVtArray(key.p, &points);
        attr.Set(points, GetTimeCode(key.t));
      }
    }
    out_frame0_scales->swap(keys[0].p);
  } else {
    const GfVec3f* const points = rest_points.data();
    VtArray<GfVec3h> rest_points_h(ujoint_count);
    out_frame0_scales->assign(points, points + ujoint_count);
    for (size_t i = 0; i != ujoint_count; ++i) {
      rest_points_h[i] = GfVec3h(points[i]);
    }
    if (normalize) {
      // Normalize animation so joint0 has scale 1.0.
      root_scale = GetNormalizedScale(points[0]);
      const GfVec3f recip_scale = Recip(root_scale);
      for (const size_t ujoint_index : ujoint_roots) {
        const GfVec3f point = Multiply(points[ujoint_index], recip_scale);
        rest_points_h[ujoint_index] = GfVec3h(point);
        (*out_frame0_scales)[ujoint_index] = point;
      }
    }
    SetConstantSkinKey(attr, rest_points_h, time_min, time_max);
  }

  return root_scale;
}

template <typename Value, typename Attr>
void SetVertexValues(const Attr& attr, const VtArray<Value>& values,
                     bool emulate_double_sided) {
  if (emulate_double_sided) {
    const size_t count = values.size();
    VtArray<Value> doubled_values(2 * count);
    for (size_t i = 0; i != count; ++i) {
      doubled_values[i] = values[i];
    }
    for (size_t i = 0; i != count; ++i) {
      doubled_values[count + i] = values[i];
    }
    attr.Set(doubled_values);
  } else {
    attr.Set(values);
  }
}

void SetVertexNormals(const UsdAttribute& attr, const VtArray<GfVec3f>& values,
                      bool emulate_double_sided) {
  if (emulate_double_sided) {
    const size_t count = values.size();
    VtArray<GfVec3f> doubled_values(2 * count);
    for (size_t i = 0; i != count; ++i) {
      doubled_values[i] = values[i];
    }
    // Flip normals for back-facing geometry.
    for (size_t i = 0; i != count; ++i) {
      doubled_values[count + i] = -values[i];
    }
    attr.Set(doubled_values);
  } else {
    attr.Set(values);
  }
}

void SetVertexIndices(const UsdAttribute& attr, const VtArray<int>& values,
                      bool emulate_double_sided, size_t point_count) {
  if (emulate_double_sided) {
    const size_t count = values.size();
    VtArray<int> doubled_values(2 * count);
    for (size_t i = 0; i != count; ++i) {
      doubled_values[i] = values[i];
    }
    UFG_ASSERT_LOGIC(count % 3 == 0);
    // Flip the winding for back-facing geometry.
    for (size_t i = 0; i != count; i += 3) {
      int* const dst = &doubled_values[count + i];
      dst[0] = static_cast<int>(point_count + values[i + 2]);
      dst[1] = static_cast<int>(point_count + values[i + 1]);
      dst[2] = static_cast<int>(point_count + values[i + 0]);
    }
    attr.Set(doubled_values);
  } else {
    attr.Set(values);
  }
}
}  // namespace

void Converter::Reset(Logger* logger) {
  cc_.Reset(logger);
  curr_pass_ = kPassCount;
  materializer_.Clear();
  node_parents_.clear();
  node_infos_.clear();
  mesh_infos_.clear();
  used_skin_infos_.clear();
  gltf_skin_srcs_.clear();
  anim_info_.Clear();
  debug_bone_material_ = UsdShadeMaterial();
}

bool Converter::Convert(const ConvertSettings& settings, const Gltf& gltf,
                        GltfStream* gltf_stream, const std::string& src_dir,
                        const std::string& dst_dir,
                        const std::string& dst_filename,
                        const SdfLayerRefPtr& layer, Logger* logger) {
  try {
    const size_t old_error_count = logger->GetErrorCount();
    ConvertImpl(settings, gltf, gltf_stream, src_dir, dst_dir, dst_filename,
                layer, logger);
    cc_.once_logger.Flush();
    cc_.logger = nullptr;
    const size_t error_count = logger->GetErrorCount();
    return error_count == old_error_count;
  } catch (const AssertException& e) {
    Log<UFG_ERROR_ASSERT>(
        logger, "", e.GetFile(), e.GetLine(), e.GetExpression());
    return false;
  }
}

void Converter::CreateDebugBoneMesh(const SdfPath& parent_path,
                                    bool reverse_winding) {
  // TODO: Use a fancier mesh that indicates orientation.
  // TODO: Choose scale proportional to the source model bounds.
  static constexpr float kS = 0.05f;
  static const GfVec3f kColor(0.0f, 0.5f, 1.0f);
  static constexpr float kAlpha = 0.3f;

  // Cube mesh at origin, of extent ±kS.
  using V = GfVec3f;
  static const VtArray<GfVec3f> kPoints = {
    V(-kS, -kS, +kS), V(+kS, -kS, +kS), V(-kS, +kS, +kS), V(+kS, +kS, +kS),
    V(+kS, -kS, +kS), V(-kS, -kS, +kS), V(+kS, -kS, -kS), V(-kS, -kS, -kS),
    V(+kS, +kS, +kS), V(+kS, -kS, +kS), V(+kS, +kS, -kS), V(+kS, -kS, -kS),
    V(-kS, +kS, +kS), V(+kS, +kS, +kS), V(-kS, +kS, -kS), V(+kS, +kS, -kS),
    V(-kS, -kS, +kS), V(-kS, +kS, +kS), V(-kS, -kS, -kS), V(-kS, +kS, -kS),
    V(-kS, -kS, -kS), V(-kS, +kS, -kS), V(+kS, -kS, -kS), V(+kS, +kS, -kS),
  };
  static const VtArray<GfVec3f> kNorms = {
    V(+0, +0, +1), V(+0, +0, +1), V(+0, +0, +1), V(+0, +0, +1),
    V(+0, -1, +0), V(+0, -1, +0), V(+0, -1, +0), V(+0, -1, +0),
    V(+1, +0, +0), V(+1, +0, +0), V(+1, +0, +0), V(+1, +0, +0),
    V(+0, +1, +0), V(+0, +1, +0), V(+0, +1, +0), V(+0, +1, +0),
    V(-1, +0, +0), V(-1, +0, +0), V(-1, +0, +0), V(-1, +0, +0),
    V(+0, +0, -1), V(+0, +0, -1), V(+0, +0, -1), V(+0, +0, -1),
  };

  static const VtArray<int> kTriIndices = {
    0, 1, 2, 3, 2, 1,
    4, 5, 6, 7, 6, 5,
    8, 9, 10, 11, 10, 9,
    12, 13, 14, 15, 14, 13,
    16, 17, 18, 19, 18, 17,
    20, 21, 22, 23, 22, 21
  };

  static const VtArray<int> kTriCounts(kTriIndices.size() / 3, 3);

  // Create the material the first time it is referenced.
  if (!debug_bone_material_) {
    const UsdStageRefPtr& stage = cc_.stage;
    const SdfPath material_path("/Materials/debug_bone_material");
    const UsdShadeMaterial usd_material =
        UsdShadeMaterial::Define(stage, material_path);
    debug_bone_material_ = usd_material;
    const SdfPath pbr_shader_path =
        material_path.AppendElementString("pbr_shader");
    UsdShadeShader pbr_shader = UsdShadeShader::Define(stage, pbr_shader_path);
    pbr_shader.CreateIdAttr(VtValue(kTokPreviewSurface));
    usd_material.CreateSurfaceOutput().ConnectToSource(pbr_shader.ConnectableAPI(), kTokSurface);
    pbr_shader.CreateInput(kTokInputUseSpecular, SdfValueTypeNames->Int).Set(1);
    pbr_shader.CreateInput(kTokInputSpecularColor, SdfValueTypeNames->Color3f)
        .Set(kColorBlack);
    pbr_shader.CreateInput(kTokInputDiffuseColor, SdfValueTypeNames->Color3f)
        .Set(kColorBlack);
    pbr_shader.CreateInput(kTokInputEmissiveColor, SdfValueTypeNames->Color3f)
        .Set(kColor);
    pbr_shader.CreateInput(kTokInputOpacity, SdfValueTypeNames->Float)
        .Set(kAlpha);
  }

  VtArray<int> tri_indices = kTriIndices;
  if (reverse_winding) {
    ReverseTriWinding(tri_indices.data(), tri_indices.size());
  }

  // Create the mesh.
  const GfRange3f aabb = BoundPoints(kPoints.data(), kPoints.size());
  const VtArray<GfVec3f> extent({ aabb.GetMin(), aabb.GetMax() });
  const SdfPath path = parent_path.AppendElementString("debug_bone");
  const UsdGeomMesh usd_mesh = UsdGeomMesh::Define(cc_.stage, path);
  usd_mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
  usd_mesh.GetPointsAttr().Set(kPoints);
  usd_mesh.GetNormalsAttr().Set(kNorms);
  usd_mesh.GetFaceVertexIndicesAttr().Set(tri_indices);
  usd_mesh.GetFaceVertexCountsAttr().Set(kTriCounts);
  usd_mesh.GetExtentAttr().Set(extent);
  UsdShadeMaterialBindingAPI(usd_mesh.GetPrim()).Bind(debug_bone_material_);
}

void Converter::CreateSkeleton(const SdfPath& path, const SkinInfo& skin_info) {
  const UsdSkelSkeleton skeleton = UsdSkelSkeleton::Define(cc_.stage, path);
  skeleton.CreateJointsAttr().Set(skin_info.ujoint_names);
  skeleton.CreateBindTransformsAttr().Set(skin_info.bind_mats);
  skeleton.CreateRestTransformsAttr().Set(skin_info.rest_mats);
}

GfVec3f Converter::CreateSkelAnim(
    const SdfPath& path, const SkinInfo& skin_info, const AnimInfo& anim_info,
    std::vector<GfQuatf>* out_frame0_rots,
    std::vector<GfVec3f>* out_frame0_scales) {
  const Gltf::Animation* const anim =
      Gltf::GetById(cc_.gltf->animations, anim_info.id);
  const UsdSkelAnimation skel_anim = UsdSkelAnimation::Define(cc_.stage, path);
  skel_anim.CreateJointsAttr().Set(skin_info.ujoint_names);

  const std::vector<const NodeInfo*> joint_infos =
      GetJointNodeInfos(skin_info.ujoint_to_node_map, node_infos_);
  const size_t ujoint_count = skin_info.ujoint_to_node_map.size();

  // Get the rest-pose transforms for use in non-animated keys.
  std::vector<GfVec3f> rest_scales(ujoint_count);
  VtArray<GfQuatf> rest_rotations(ujoint_count);
  VtArray<GfVec3f> rest_translations(ujoint_count);
  for (size_t i = 0; i != ujoint_count; ++i) {
    const Gltf::Id node_id = skin_info.ujoint_to_node_map[i];
    const Gltf::Node& node =
        *UFG_VERIFY(Gltf::GetById(cc_.gltf->nodes, node_id));
    const Srt srt = GetNodeSrt(node);
    rest_scales[i] = srt.scale;
    rest_rotations[i] = srt.rotation;
    rest_translations[i] = srt.translation;
  }

  const float time_min = anim ? anim_info.time_min : 0.0f;
  const float time_max = anim ? anim_info.time_max : 1.0f;

  SetTranslationSkinKeys(skel_anim, joint_infos.data(), ujoint_count,
      rest_translations, time_min, time_max);
  SetRotationSkinKeys(skel_anim, joint_infos.data(), ujoint_count,
      rest_rotations, time_min, time_max, out_frame0_rots);

  const std::vector<uint16_t> ujoint_roots =
      GetJointRoots(node_parents_.data(), cc_.gltf->nodes.size(),
                    skin_info.ujoint_to_node_map);
  const GfVec3f root_scale = SetScaleSkinKeys(
      skel_anim, joint_infos.data(), ujoint_count, rest_scales, time_min,
      time_max, cc_.settings.normalize_skin_scale, ujoint_roots,
      out_frame0_scales);

  return root_scale;
}

void Converter::CreateMesh(
    size_t mesh_index, const SdfPath& parent_path, bool reverse_winding,
    const SkinnedMeshContext* skinned_mesh_context) {
  // TODO: Perform vertex welding, because some of our source meshes
  // appear to be non-indexed, thus way oversized.
  UFG_ASSERT_LOGIC(mesh_index < cc_.gltf->meshes.size());
  const Gltf::Mesh& mesh = cc_.gltf->meshes[mesh_index];
  const std::string mesh_path_str =
      cc_.path_table.MakeUnique(parent_path, "mesh", mesh.name, mesh_index);

  // The GLTF loader should prevent this.
  UFG_ASSERT_LOGIC(!mesh.primitives.empty());

  const MeshInfo& mesh_info = mesh_infos_[mesh_index];
  const size_t prim_count = mesh.primitives.size();
  UFG_ASSERT_LOGIC(prim_count == mesh_info.prims.size());

  // A glTF primitive is geometry with a specific format and material.
  // TODO: Can we handle multiple prims with UsdGeomSubsets?
  for (size_t prim_index = 0; prim_index != prim_count; ++prim_index) {
    const PrimInfo& prim_info = mesh_info.prims[prim_index];
    const size_t used_vert_count = prim_info.pos.size();
    if (used_vert_count == 0) {
      continue;
    }
    const Gltf::Mesh::Primitive& prim = mesh.primitives[prim_index];
    if (cc_.settings.remove_invisible &&
        materializer_.IsInvisible(prim.material)) {
      continue;
    }

    // Find or create the material.
    const Gltf::Material* const material =
        Gltf::GetById(cc_.gltf->materials, prim.material);
    const bool double_sided = material && material->doubleSided;
    const bool emulate_double_sided =
        double_sided && cc_.settings.emulate_double_sided;

    SkinData skin_data;
    const bool have_skin_data =
        skinned_mesh_context && prim_info.skin_index_stride != 0 &&
        GetSkinData(prim_info.skin_indices.data(), prim_info.skin_index_stride,
                    prim_info.skin_weights.data(), prim_info.skin_weight_stride,
                    cc_.gltf->nodes.size(), used_vert_count,
                    skinned_mesh_context->gjoint_to_ujoint_map,
                    skinned_mesh_context->gjoint_count, &skin_data);

    // TODO: Morph targets.
    // * This is supported in USD, but not on iOS.
    // * https://graphics.pixar.com/usd/docs/api/_usd_skel__schemas.html#UsdSkel_BlendShape
    // * It's also possible to emulate it via vertex animation similar to how
    //   XCode converts Alembic animations (you can set timeSamples for
    //   position, normal, etc). This works in Usdview, but is ignored on iOS.
    if (!mesh.weights.empty()) {
      const std::string src_mesh_name =
          Gltf::GetName(cc_.gltf->meshes, Gltf::IndexToId(mesh_index), "mesh");
      LogOnce<UFG_WARN_MORPH_TARGETS_UNSUPPORTED>(
          &cc_.once_logger, " Mesh(es): ", src_mesh_name.c_str());
    }

    // TODO: When we create a mesh we're providing a specific path to
    // it, which doesn't support instancing (a mesh being replicated at multiple
    // points in the hierarchy).
    std::string path_str = mesh_path_str;
    if (mesh.primitives.size() != 1) {
      path_str =
          AppendNumber(path_str + "_prim", &prim - mesh.primitives.data());
    }
    const SdfPath path(path_str);
    UsdGeomMesh usd_mesh = UsdGeomMesh::Define(cc_.stage, path);
    usd_mesh.CreateSubdivisionSchemeAttr().Set(UsdGeomTokens->none);

    // Set vertex attributes.
    UFG_ASSERT_FORMAT(!prim_info.pos.empty());
    SetVertexValues(usd_mesh.GetPointsAttr(), prim_info.pos,
                    emulate_double_sided);
    if (!prim_info.norm.empty()) {
      const VtArray<GfVec3f>* norms = &prim_info.norm;
      VtArray<GfVec3f> skin_norms;
      if (have_skin_data && skinned_mesh_context &&
          skinned_mesh_context->bake_norm_mats) {
        // Bake normals to the first frame of the animation.
        skin_norms.resize(prim_info.norm.size());
        SkinNormals(skinned_mesh_context->bake_norm_mats, prim_info.norm.data(),
                    prim_info.norm.size(), skin_data.bindings.data(),
                    skin_norms.data());
        norms = &skin_norms;
      }
      SetVertexNormals(usd_mesh.GetNormalsAttr(), *norms, emulate_double_sided);
      usd_mesh.SetNormalsInterpolation(UsdGeomTokens->vertex);
    }

    const Materializer::Value* const material_binding =
        material ? &materializer_.FindOrCreate(prim.material) : nullptr;

    // Set UVs.
    if (material) {
      for (const auto& uvset_kv : material_binding->uvsets) {
        const Gltf::Mesh::Attribute::Number number = uvset_kv.first;
        const auto uv_found = prim_info.uvs.find(number);
        if (uv_found == prim_info.uvs.end()) {
          // Missing UV set. This is incorrect but recoverable, so just skip
          // setting UVs. Don't bother warning about it here because the glTF
          // validator already does that.
          continue;
        }
        const PrimInfo::Uvset& src_uv = uv_found->second;
        const PrimInfo::Uvset* uv = &src_uv;
        PrimInfo::Uvset transformed_uv;
        const Gltf::Material::Texture::Transform& transform =
            uvset_kv.second.transform;
        if (!transform.IsIdentity()) {
          transformed_uv = src_uv;
          TransformUvs(transform, transformed_uv.size(), transformed_uv.data());
          uv = &transformed_uv;
        }
        const TfToken uvset_tok(AppendNumber("st", number));
        const UsdGeomPrimvar uvs_primvar = usd_mesh.CreatePrimvar(
            uvset_tok, SdfValueTypeNames->TexCoord2fArray,
            UsdGeomTokens->vertex);
        SetVertexValues(uvs_primvar, *uv, emulate_double_sided);
      }
    }

    // Set vertex colors.
    const size_t color_scalar_count = prim_info.color_stride == 3
                                          ? prim_info.color3.size()
                                          : prim_info.color4.size();
    if (color_scalar_count > 0) {
      const float* const color_scalars = prim_info.color_stride == 3
                                             ? prim_info.color3.data()->data()
                                             : prim_info.color4.data()->data();
      if (!AllNearlyEqual(color_scalars, color_scalar_count, 1.0f, kColorTol)) {
        // TODO: Add vertex colors. Not needed right now because we're
        // targeting the iOS viewer which doesn't support it. And while Usdview
        // supports it, it's not compatible with texturing because you must
        // replace the color input normally used for texturing (and the color
        // multiplier field does not work).
        const std::string src_mesh_name = Gltf::GetName(
            cc_.gltf->meshes, Gltf::IndexToId(mesh_index), "mesh");
        LogOnce<UFG_WARN_VERTEX_COLORS_UNSUPPORTED>(
            &cc_.once_logger, " Mesh(es): ", src_mesh_name.c_str());
      }
    }

    const VtArray<int>* tri_vert_indices = &prim_info.tri_vert_indices;
    VtArray<int> reversed_tri_vert_indices;
    if (reverse_winding) {
      reversed_tri_vert_indices = prim_info.tri_vert_indices;
      ReverseTriWinding(reversed_tri_vert_indices.data(),
                        reversed_tri_vert_indices.size());
      tri_vert_indices = &reversed_tri_vert_indices;
    }
    SetVertexIndices(usd_mesh.GetFaceVertexIndicesAttr(), *tri_vert_indices,
                     emulate_double_sided, used_vert_count);
    SetVertexValues(usd_mesh.GetFaceVertexCountsAttr(),
                    prim_info.tri_vert_counts, emulate_double_sided);

    // Set point extent from its AABB.
    // TODO: We may need to expand this to account for animation.
    const GfRange3f aabb =
        BoundPoints(prim_info.pos.data(), prim_info.pos.size());
    const VtArray<GfVec3f> extent({aabb.GetMin(), aabb.GetMax()});
    usd_mesh.GetExtentAttr().Set(extent);

    // Set material.
    if (material) {
      usd_mesh.GetDoubleSidedAttr().Set(double_sided && !emulate_double_sided);
      UsdShadeMaterialBindingAPI(usd_mesh.GetPrim())
          .Bind(material_binding->material);
    }

    // Bind skin data.
    if (have_skin_data) {
      UFG_ASSERT_LOGIC(skin_data.bindings.size() == used_vert_count);
      const size_t influence_count = skin_data.influence_count;
      const size_t influence_total = used_vert_count * influence_count;
      VtArray<int> joint_indices(influence_total);
      VtArray<float> joint_weights(influence_total);
      int* index_it = joint_indices.data();
      float* weight_it = joint_weights.data();
      for (const SkinBinding& binding : skin_data.bindings) {
        for (size_t i = 0; i != influence_count; ++i) {
          const SkinInfluence& influence = binding.influences[i];
          *index_it++ =
              influence.index == SkinInfluence::kUnused ? 0 : influence.index;
          *weight_it++ = influence.weight;
        }
      }
      UFG_ASSERT_LOGIC(index_it == joint_indices.data() + influence_total);
      UFG_ASSERT_LOGIC(weight_it == joint_weights.data() + influence_total);

      const UsdSkelBindingAPI binding_api(usd_mesh.GetPrim());
      binding_api.CreateSkeletonRel().AddTarget(
          skinned_mesh_context->skeleton_path);
      binding_api.CreateAnimationSourceRel().AddTarget(
          skinned_mesh_context->anim_path);
      const UsdGeomPrimvar joint_indices_primvar =
          binding_api.CreateJointIndicesPrimvar(
              skin_data.is_rigid, static_cast<int>(influence_count));
      const UsdGeomPrimvar joint_weights_primvar =
          binding_api.CreateJointWeightsPrimvar(
              skin_data.is_rigid, static_cast<int>(influence_count));
      SetVertexValues(joint_indices_primvar, joint_indices,
                      emulate_double_sided);
      SetVertexValues(joint_weights_primvar, joint_weights,
                      emulate_double_sided);
    }
  }
}

void Converter::CreateSkinnedMeshes(const SdfPath& parent_path,
                                    const std::vector<Gltf::Id>& node_ids,
                                    bool reverse_winding) {
  struct Skel {
    bool created = false;
    SdfPath skin_path;
    SdfPath skeleton_path;
    SdfPath anim_path;
    std::vector<GfMatrix3f> bake_norm_mats;
  };
  std::vector<Skel> skels(used_skin_infos_.size());

  for (const Gltf::Id node_id : node_ids) {
    const Gltf::Node& node =
        *UFG_VERIFY(Gltf::GetById(cc_.gltf->nodes, node_id));
    UFG_ASSERT_LOGIC(node.mesh != Gltf::Id::kNull);
    UFG_ASSERT_LOGIC(node.skin != Gltf::Id::kNull);
    const size_t mesh_index = Gltf::IdToIndex(node.mesh);

    // All skinning data (including the mesh), must be under a SkelRoot
    // primitive.
    const size_t gltf_skin_index = Gltf::IdToIndex(node.skin);
    const SkinSrc& skin_src = gltf_skin_srcs_[gltf_skin_index];
    const size_t used_skin_index = skin_src.used_skin_index;
    UFG_ASSERT_LOGIC(used_skin_index < used_skin_infos_.size());

    // Create SkelRoot, Skeleton, and SkelAnimation the first time this skin is
    // referenced.
    Skel& skel = skels[used_skin_index];
    if (!skel.created) {
      skel.created = true;

      const SkinInfo& skin_info = used_skin_infos_[used_skin_index];
      const std::string skin_path_str = cc_.path_table.MakeUnique(
          parent_path, "skin", skin_info.name, used_skin_index);
      skel.skin_path = SdfPath(skin_path_str);
      const UsdSkelRoot skel_root =
          UsdSkelRoot::Define(cc_.stage, skel.skin_path);

      skel.skeleton_path = skel.skin_path.AppendElementString("skeleton");
      CreateSkeleton(skel.skeleton_path, skin_info);

      const Gltf::Animation* const anim =
          Gltf::GetById(cc_.gltf->animations, anim_info_.id);
      const std::string anim_path_str =
          anim ? cc_.path_table.MakeUnique(skel.skin_path, "anim", anim->name,
                                           Gltf::IdToIndex(anim_info_.id))
               : cc_.path_table.MakeUnique(skel.skin_path, nullptr,
                                           "default_skin_anim", 0);
      skel.anim_path = SdfPath(anim_path_str);
      std::vector<GfQuatf> frame0_rots;
      std::vector<GfVec3f> frame0_scales;
      const GfVec3f root_scale = CreateSkelAnim(
          skel.anim_path, skin_info, anim_info_, &frame0_rots, &frame0_scales);

      // Apply animation scale to the skeleton root node.
      if (!NearlyEqual(root_scale, GfVec3f(1.0f), kPruneScaleComponent)) {
        skel_root.AddScaleOp().Set(root_scale);
      }

      if (cc_.settings.bake_skin_normals) {
        const size_t ujoint_count = skin_info.bind_mats.size();
        UFG_ASSERT_LOGIC(frame0_rots.empty() ||
                         frame0_rots.size() == ujoint_count);
        UFG_ASSERT_LOGIC(frame0_scales.empty() ||
                         frame0_scales.size() == ujoint_count);
        skel.bake_norm_mats.resize(ujoint_count);
        GetSkinJointMatricesForNormals(
            skin_info, cc_.gltf->nodes.size(), node_parents_.data(),
            ujoint_count, GetDataOrNull(frame0_rots),
            GetDataOrNull(frame0_scales), skel.bake_norm_mats.data());
      }
    }

    const std::vector<uint16_t>& joint_map = skin_src.gjoint_to_ujoint_map;
    const SkinnedMeshContext skinned_mesh_context = {
      skel.skeleton_path, skel.anim_path,
      joint_map.data(), joint_map.size(),
      GetDataOrNull(skel.bake_norm_mats)
    };
    CreateMesh(mesh_index, skel.skin_path, reverse_winding,
               &skinned_mesh_context);
  }
}

void Converter::CreateNodeHierarchy(Gltf::Id node_id,
                                    const SdfPath& parent_path,
                                    const GfMatrix4d& parent_world_mat) {
  const size_t node_index = Gltf::IdToIndex(node_id);
  const NodeInfo& info = node_infos_[node_index];
  if (!info.passes_used[curr_pass_]) {
    // Omit nodes without any content.
    return;
  }

  UFG_ASSERT_FORMAT(node_index < cc_.gltf->nodes.size());
  const Gltf::Node& node = cc_.gltf->nodes[node_index];
  const std::string path_str =
      cc_.path_table.MakeUnique(parent_path, "node", node.name, node_index);
  const SdfPath path(path_str);
  const UsdGeomXform xform = UsdGeomXform::Define(cc_.stage, path);

  // Apply transform.
  if (!info.is_animated) {
    // Either the node isn't animated, or it doesn't contain any meshes, so we
    // can treat it as static. Note for skinned meshes, animation data is stored
    // separately under SkelRoot.
    // TODO: Maybe set SRT separately with AddTranslateOp, AddScaleOp,
    // and AddRotate<XYZ>Op.
    const GfMatrix4d mat =
        node.is_matrix
            ? ToMatrix4d(node.matrix)
            : SrtToMatrix4d(node.scale, node.rotation, node.translation);
    xform.AddTransformOp().Set(mat);
  } else {
    // Animated node.
    const Srt srt = GetNodeSrt(node);
    SetTranslationKeys(xform, srt.translation, info.translation_times,
                       info.translation_points);
    SetRotationKeys(xform, srt.rotation, info.rotation_times,
                    info.rotation_points);
    SetScaleKeys(xform, srt.scale, info.scale_times, info.scale_points);
  }

  // TODO: Cameras.
  if (node.camera != Gltf::Id::kNull) {
    // TODO: This warning won't be emitted if this node is pruned due
    // to absence of meshes.
    const std::string src_node_name =
        Gltf::GetName(cc_.gltf->nodes, node_id, "node");
    LogOnce<UFG_WARN_CAMERAS_UNSUPPORTED>(
        &cc_.once_logger, " Node(s): ", src_node_name.c_str());
  }

  GfMatrix4d local_mat;
  bool resets_xform_stack;
  if (!xform.GetLocalTransformation(&local_mat, &resets_xform_stack)) {
    local_mat.SetIdentity();
  }
  UFG_ASSERT_LOGIC(!resets_xform_stack);

  // From my reading of usdSkel/utils.cpp, vector-matrix multiplication has the
  // vector on the left, so the convention appears to be that matrix
  // multiplication is ordered local*world. Either way, it's currently arbitrary
  // for our purposes because we're only using the world matrix to determine
  // when we need to reverse winding for inverse-scale.
  const GfMatrix4d world_mat = local_mat * parent_world_mat;
  const bool reverse_winding = cc_.settings.reverse_culling_for_inverse_scale &&
                               world_mat.GetDeterminant() < 0;

  // TODO: It's possible a mesh may be referenced at multiple places
  // in the hierarchy, in which case this duplicates mesh data. I'm not sure if
  // there's a way to instance meshes in USD, though.
  if (curr_pass_ == kPassRigid) {
    if (node.mesh != Gltf::Id::kNull && node.skin == Gltf::Id::kNull) {
      CreateMesh(Gltf::IdToIndex(node.mesh), path, reverse_winding, nullptr);
    } else if (cc_.settings.add_debug_bone_meshes) {
      CreateDebugBoneMesh(path, reverse_winding);
    }
  } else if (curr_pass_ == kPassSkinned) {
    CreateSkinnedMeshes(path, info.skinned_node_ids, reverse_winding);
  }

  // Add child transforms.
  for (const Gltf::Id child_id : node.children) {
    CreateNodeHierarchy(child_id, path, world_mat);
  }
}

void Converter::CreateNodes(const std::vector<Gltf::Id>& root_nodes) {
  // Create transform tree under each root.
  GfMatrix4d identity;
  identity.SetIdentity();
  for (size_t pass = 0; pass != kPassCount; ++pass) {
    bool used = root_node_info_.passes_used[pass];
    for (const Gltf::Id node_id : root_nodes) {
      const NodeInfo& node_info =
          *UFG_VERIFY(Gltf::GetById(node_infos_, node_id));
      if (node_info.passes_used[pass]) {
        used = true;
        break;
      }
    }
    if (!used) {
      continue;
    }

    const SdfPath pass_path =
        cc_.root_path.AppendElementString(kPassNames[pass]);
    const UsdGeomXform pass_xform = UsdGeomXform::Define(cc_.stage, pass_path);
    const UsdGeomXformOp pass_scale_op = pass_xform.AddScaleOp();

    // Skinned meshes may have been reanchored to the root (which doesn't have a
    // source node).
    if (pass == kPassSkinned && root_node_info_.passes_used[kPassSkinned]) {
      CreateSkinnedMeshes(pass_path, root_node_info_.skinned_node_ids, false);
    }

    curr_pass_ = static_cast<Pass>(pass);
    for (const Gltf::Id node_id : root_nodes) {
      CreateNodeHierarchy(node_id, pass_path, identity);
    }

    // Apply root scale, optionally scaling it to limit the bounding box size.
    // TODO(b/140108978): Remove once root_scale is no longer needed.
    float path_scale = cc_.settings.root_scale;
    if (cc_.settings.limit_bounds > 0.0f) {
      const GfBBox3d bound = pass_xform.ComputeLocalBound(
          UsdTimeCode::Default(), UsdGeomTokens->default_);
      const GfVec3d size = GetBoxSize(bound);
      const double max_dim = MaxComponent(size);
      if (max_dim > cc_.settings.limit_bounds) {
        const double limit_scale = cc_.settings.limit_bounds / max_dim;
        path_scale = static_cast<float>(path_scale * limit_scale);
      }
    }
    pass_scale_op.Set(GfVec3f(path_scale));
  }
}

void Converter::CreateAnimation(const AnimInfo& anim_info) {
  const Gltf::Animation* const anim =
      Gltf::GetById(cc_.gltf->animations, anim_info.id);
  UFG_ASSERT_LOGIC(anim);

  cc_.stage->SetStartTimeCode(anim_info.time_min * kFps);
  cc_.stage->SetEndTimeCode(anim_info.time_max * kFps);
  cc_.stage->SetTimeCodesPerSecond(kFps);

  // Generate a mapping from node to animation channels affecting that node.
  const size_t node_count = cc_.gltf->nodes.size();
  const size_t channel_count = anim->channels.size();
  for (size_t ci = 0; ci != channel_count; ++ci) {
    const Gltf::Animation::Channel& channel = anim->channels[ci];
    const Gltf::Id node_id = channel.target.node;
    const size_t node_index = Gltf::IdToIndex(node_id);
    if (node_index >= node_count) {
      continue;
    }
    NodeInfo& info = node_infos_[node_index];
    info.is_animated = true;

    const Gltf::Animation::Sampler& sampler =
        *Gltf::GetById(anim->samplers, channel.sampler);
    std::vector<float> times;
    CopyAccessorToVectors(*cc_.gltf, sampler.input, &cc_.gltf_cache, &times);
    if (times.empty()) {
      // If there is no animation, preserve the default transform associated
      // with the node.
      continue;
    }
    switch (channel.target.path) {
    case Gltf::Animation::Channel::Target::kPathTranslation: {
      info.translation_times.swap(times);
      CopyAccessorToVectors(*cc_.gltf, sampler.output, &cc_.gltf_cache,
                            &info.translation_points);
      ConvertAnimKeysToLinear<TranslationKeyConverter>(
          sampler.interpolation, &info.translation_times,
          &info.translation_points);
      break;
    }
    case Gltf::Animation::Channel::Target::kPathRotation: {
      info.rotation_times.swap(times);
      CopyAccessorToVectors(*cc_.gltf, sampler.output, &cc_.gltf_cache,
                            &info.rotation_points);
      SanitizeRotations(info.rotation_points.size(),
                        info.rotation_points.data());
      ConvertAnimKeysToLinear<QuatKeyConverter>(
          sampler.interpolation, &info.rotation_times, &info.rotation_points);
      break;
    }
    case Gltf::Animation::Channel::Target::kPathScale: {
      info.scale_times.swap(times);
      CopyAccessorToVectors(*cc_.gltf, sampler.output, &cc_.gltf_cache,
                            &info.scale_points);
      ConvertAnimKeysToLinear<ScaleKeyConverter>(
          sampler.interpolation, &info.scale_times, &info.scale_points);
      break;
    }
    case Gltf::Animation::Channel::Target::kPathWeights: {
      // TODO: Support morph-target animation.
      break;
    }
    default:
      UFG_ASSERT_FORMAT(false);
      break;
    }
  }
}

void Converter::CreateStage(const SdfLayerRefPtr& layer,
                            const std::string& dst_filename) {
  cc_.stage = UsdStage::Open(layer);

  // All nodes are placed under the following root node.
  cc_.root_path = MakeAbsolutePath(dst_filename);
  UsdPrim prim = cc_.stage->DefinePrim(cc_.root_path, TfToken("Xform"));
  prim.SetAssetInfoByKey(TfToken("name"),
                         VtValue(cc_.root_path.GetElementString()));
  UsdModelAPI(prim).SetKind(TfToken("component"));
  cc_.stage->SetDefaultPrim(prim);

  UsdGeomSetStageUpAxis(cc_.stage, pxr::UsdGeomTokens->y);
}

void Converter::ConvertImpl(const ConvertSettings& settings, const Gltf& gltf,
                            GltfStream* gltf_stream, const std::string& src_dir,
                            const std::string& dst_dir,
                            const std::string& dst_filename,
                            const SdfLayerRefPtr& layer, Logger* logger) {
  Reset(logger);

  CreateStage(layer, dst_filename);
  cc_.settings = settings;
  cc_.gltf = &gltf;
  cc_.src_dir = src_dir;
  cc_.dst_dir = dst_dir;
  cc_.gltf_cache.Reset(&gltf, gltf_stream);
  node_parents_ = GetNodeParents(gltf.nodes);

  const Gltf::Id scene_id = GetSceneId(gltf, cc_.settings);
  const std::vector<Gltf::Id> root_nodes =
      GetSceneRootNodes(*cc_.gltf, scene_id, node_parents_.data(),
          cc_.settings.remove_node_prefixes);
  const std::vector<Gltf::Id> scene_nodes =
      GetNodesUnderRoots(*cc_.gltf, root_nodes,
          cc_.settings.remove_node_prefixes);

  // Populate per-mesh info.
  const size_t mesh_count = cc_.gltf->meshes.size();
  mesh_infos_.resize(mesh_count);
  for (size_t mesh_index = 0; mesh_index != mesh_count; ++mesh_index) {
    GetMeshInfo(*cc_.gltf, Gltf::IndexToId(mesh_index), &cc_.gltf_cache,
                &mesh_infos_[mesh_index], cc_.logger);
  }

  // glTF can store multiple animations, but we only export a single one.
  const Gltf::Id anim_id = GetAnimId(*cc_.gltf, cc_.settings);
  if (anim_id != Gltf::Id::kNull) {
    anim_info_ = GetAnimInfo(*cc_.gltf, anim_id, &cc_.gltf_cache);
  }

  // Work-around for grossly inaccurate normals on the iOS viewer.
  // Specifically, if a model doesn't reference root joints in the skeleton
  // hierarchy, we will normally treat those as rigidly animated. But since the
  // iOS viewer doesn't skin normals, that will cause normals to only be
  // animated by a partial subset of transforms, which usually looks worse than
  // not animating them at all. This work-around forces all animated joints to
  // be skinned, even if they are not referenced by the mesh. This is likely to
  // make the animation larger, and won't fix all cases.
  // TODO: Technically we should also be considering static rotation
  // on these nodes.
  const std::vector<bool>* const force_nodes_used =
      cc_.settings.fix_skinned_normals && !anim_info_.nodes_animated.empty()
          ? &anim_info_.nodes_animated
          : nullptr;

  // Populate per-skin info.
  GetUsedSkinInfos(*cc_.gltf, mesh_infos_, node_parents_.data(), scene_nodes,
                   force_nodes_used, cc_.settings.merge_skeletons,
                   &cc_.gltf_cache, &used_skin_infos_, &gltf_skin_srcs_);

  // Populate per-node info.
  const size_t node_count = gltf.nodes.size();
  node_infos_.resize(node_count);
  for (const Gltf::Id node_id : scene_nodes) {
    const Gltf::Node& node =
        *UFG_VERIFY(Gltf::GetById(cc_.gltf->nodes, node_id));
    NodeInfo& node_info = *UFG_VERIFY(Gltf::GetById(node_infos_, node_id));
    const Srt srt = GetNodeSrt(node);
    node_info.SetStatic(srt);

    // Set passes_used visibility.
    const bool is_mesh = node.mesh != Gltf::Id::kNull;
    const bool is_skinned = node.skin != Gltf::Id::kNull && is_mesh;
    const bool is_rigid =
        !is_skinned && (is_mesh || cc_.settings.add_debug_bone_meshes);
    if (is_rigid) {
      // Rigid meshes appear in their original placement in the hierarchy.
      node_info.passes_used[kPassRigid] = true;
    } else if (is_skinned) {
      // Skinned meshes are reanchored under their skeleton, so it is not
      // affected by its original hierarchy transform. This mimics glTF's
      // behavior, and is particularly important for preventing double
      // application of the 1/100 cm scale.
      const size_t gltf_skin_index = Gltf::IdToIndex(node.skin);
      const SkinSrc& skin_src = gltf_skin_srcs_[gltf_skin_index];
      const size_t used_skin_index = skin_src.used_skin_index;
      UFG_ASSERT_LOGIC(used_skin_index < used_skin_infos_.size());
      const SkinInfo& skin_info = used_skin_infos_[used_skin_index];
      NodeInfo& root_node_info =
          skin_info.root_node_id == Gltf::Id::kNull
              ? root_node_info_
              : node_infos_[Gltf::IdToIndex(skin_info.root_node_id)];
      root_node_info.skinned_node_ids.push_back(node_id);
      root_node_info.passes_used[kPassSkinned] = true;
    }
  }

  // Propagate passes_used flags up the hierarchy so we know which nodes to
  // export for each pass.
  for (const Gltf::Id node_id : root_nodes) {
    PropagatePassesUsed(node_id, gltf.nodes.data(), node_infos_.data());
  }

  if (anim_id != Gltf::Id::kNull) {
    CreateAnimation(anim_info_);
  }

  materializer_.Begin(&cc_);
  CreateNodes(root_nodes);
  materializer_.End();
}
}  // namespace ufg
