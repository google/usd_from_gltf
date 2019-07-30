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

#include "process/mesh.h"

#include "common/logging.h"
#include "draco/compression/decode.h"
#include "process/access.h"
#include "process/skin.h"

namespace ufg {
namespace {
void ConvertToTriangles(Gltf::Mesh::Primitive::Mode prim_mode,
                        std::vector<uint32_t>* indices) {
  UFG_ASSERT_LOGIC(Gltf::HasTriangles(prim_mode));
  const uint32_t src_count = static_cast<uint32_t>(indices->size());
  const uint32_t* const src_indices = indices->data();
  UFG_ASSERT_FORMAT(src_count >= 3);
  const uint32_t dst_count = 3 * (src_count - 2);
  std::vector<uint32_t> buffer(dst_count);
  uint32_t* const dst_indices = buffer.data();
  const uint32_t* const src_end = src_indices + src_count - 2;
  const uint32_t* src = src_indices;
  uint32_t* dst = dst_indices;
  switch (prim_mode) {
  case Gltf::Mesh::Primitive::kModeTriangleStrip: {
    for (;;) {
      // Forward winding.
      if (src == src_end) {
        break;
      }
      dst[0] = src[0];
      dst[1] = src[1];
      dst[2] = src[2];
      src += 1;
      dst += 3;

      // Reverse winding.
      if (src == src_end) {
        break;
      }
      dst[0] = src[0];
      dst[1] = src[2];
      dst[2] = src[1];
      src += 1;
      dst += 3;
    }
    break;
  }
  case Gltf::Mesh::Primitive::kModeTriangleFan: {
    const uint32_t i0 = src[0];
    for (; src != src_end; ++src) {
      dst[0] = i0;
      dst[1] = src[1];
      dst[2] = src[2];
      dst += 3;
    }
    break;
  }
  default:
    UFG_ASSERT_LOGIC(false);
    return;
  }
  indices->swap(buffer);
}

void GetMeshIndices(
    const Gltf& gltf, const Gltf::Mesh::Primitive& prim, size_t vert_count,
    GltfCache* gltf_cache, std::vector<uint32_t>* out_indices) {
  if (prim.indices != Gltf::Id::kNull) {
    // Indexed geometry. Read it from a buffer.
    size_t index_count, component_count;
    const uint32_t* const indices = gltf_cache->Access<uint32_t>(
        prim.indices, &index_count, &component_count);
    UFG_ASSERT_FORMAT(indices);
    UFG_ASSERT_FORMAT(component_count == 1);
    out_indices->resize(index_count);
    std::copy(indices, indices + index_count, out_indices->data());
  } else {
    // Non-indexed geometry. USD doesn't support this AFAICT, so just generate
    // indices.
    out_indices->resize(vert_count);
    uint32_t* const dst = out_indices->data();
    for (uint32_t i = 0; i != vert_count; ++i) {
      dst[i] = i;
    }
  }
  if (prim.mode != Gltf::Mesh::Primitive::kModeTriangles) {
    ConvertToTriangles(prim.mode, out_indices);
  }
}

size_t GetDracoAttributeComponentCount(const draco::Mesh& mesh,
                                       const Gltf::Mesh::AttributeSet& attrs,
                                       const Gltf::Mesh::Attribute& key) {
  const auto found = attrs.find(key);
  if (found == attrs.end()) {
    return 0;
  }
  const int32_t draco_attr_id =
      static_cast<int32_t>(Gltf::IdToIndex(found->accessor));
  const draco::PointAttribute* const attr =
      mesh.GetAttributeByUniqueId(draco_attr_id);
  return attr ? attr->num_components() : 0;
}

template <typename Scalar>
void CopyDracoScalars(
    const draco::PointAttribute& attr, const std::vector<bool>& used,
    size_t vec_count, size_t component_count,
    Scalar* out_scalars) {
  const size_t orig_vert_count = used.size();
  Scalar* dst = out_scalars;
  for (size_t orig_vi = 0; orig_vi != orig_vert_count; ++orig_vi) {
    if (!used[orig_vi]) {
      continue;
    }
    const uint32_t value_index = attr.mapped_index(
        draco::PointIndex(static_cast<uint32_t>(orig_vi))).value();
    UFG_VERIFY(
        (attr.ConvertValue<Scalar>(draco::AttributeValueIndex(value_index),
                                   static_cast<int8_t>(component_count), dst)));
    dst += component_count;
  }
  UFG_ASSERT_LOGIC((dst - out_scalars) == vec_count * component_count);
}

template <typename Scalar>
bool CopyDracoScalars(
    const draco::Mesh& mesh, size_t draco_attr_id,
    const std::vector<bool>& used, size_t vec_count, size_t component_count,
    Scalar* out_scalars) {
  const draco::PointAttribute* const attr =
      mesh.GetAttributeByUniqueId(static_cast<int32_t>(draco_attr_id));
  if (!attr || attr->num_components() != component_count) {
    return false;
  }
  CopyDracoScalars(*attr, used, vec_count, component_count, out_scalars);
  return true;
}

template <typename Scalar>
bool CopyDracoScalars(
    const draco::Mesh& mesh, const Gltf::Mesh::AttributeSet& attrs,
    const Gltf::Mesh::Attribute& key, const std::vector<bool>& used,
    size_t vec_count, size_t component_count,
    Scalar* out_scalars) {
  const auto found = attrs.find(key);
  return found != attrs.end() &&
         CopyDracoScalars(mesh, Gltf::IdToIndex(found->accessor), used,
                          vec_count, component_count, out_scalars);
}

template <typename Vec>
bool CopyDracoVectors(
    const draco::Mesh& mesh, size_t draco_attr_id,
    const std::vector<bool>& used, size_t vec_count,
    Vec* out_vecs) {
  constexpr size_t kComponentCount = Vec::dimension;
  using Scalar = typename Vec::ScalarType;
  static_assert(sizeof(Vec) / sizeof(Scalar) == kComponentCount, "");
  const draco::PointAttribute* const attr =
      mesh.GetAttributeByUniqueId(static_cast<int32_t>(draco_attr_id));
  if (!attr || attr->num_components() != kComponentCount) {
    return false;
  }
  CopyDracoScalars(*attr, used, vec_count, kComponentCount, out_vecs->data());
  return true;
}

template <typename Vec>
bool CopyDracoVectors(
    const draco::Mesh& mesh, const Gltf::Mesh::AttributeSet& attrs,
    const Gltf::Mesh::Attribute& key, const std::vector<bool>& used,
    size_t vec_count,
    Vec* out_vecs) {
  const auto found = attrs.find(key);
  return found != attrs.end() &&
         CopyDracoVectors(mesh, Gltf::IdToIndex(found->accessor), used,
                          vec_count, out_vecs);
}

size_t GetMeshInfoFromDraco(
    const void* draco_data, size_t draco_size,
    const Gltf::Mesh::AttributeSet& attrs, Gltf::Id mesh_id,
    const Gltf::Mesh& mesh, size_t prim_index,
    PrimInfo* out_info, std::vector<uint32_t>* out_indices,
    std::vector<bool>* out_orig_verts_used, Logger* logger) {
  draco::Decoder decoder;
  draco::DecoderBuffer decoder_buffer;
  decoder_buffer.Init(static_cast<const char*>(draco_data), draco_size);
  const auto geom_type_status =
      draco::Decoder::GetEncodedGeometryType(&decoder_buffer);
  if (!geom_type_status.ok()) {
    Log<UFG_ERROR_DRACO_UNKNOWN>(
      logger, "", Gltf::IdToIndex(mesh_id), prim_index, mesh.name.c_str());
    return 0;
  }
  if (geom_type_status.value() != draco::TRIANGULAR_MESH) {
    Log<UFG_ERROR_DRACO_NON_TRIANGLES>(
        logger, "", Gltf::IdToIndex(mesh_id), prim_index, mesh.name.c_str());
    return 0;
  }

  const auto draco_mesh_status = decoder.DecodeMeshFromBuffer(&decoder_buffer);
  if (!draco_mesh_status.ok() || !draco_mesh_status.value()) {
    Log<UFG_ERROR_DRACO_DECODE>(
        logger, "", Gltf::IdToIndex(mesh_id), prim_index, mesh.name.c_str());
    return 0;
  }
  const draco::Mesh& draco_mesh = *draco_mesh_status.value();

  // Convert triangle Face structures to indices.
  const uint32_t tri_count = draco_mesh.num_faces();
  const uint32_t index_count = 3 * tri_count;
  out_indices->resize(index_count);
  uint32_t* const indices = out_indices->data();
  for (uint32_t tri_index = 0; tri_index != tri_count; ++tri_index) {
    uint32_t* const tri = indices + 3 * tri_index;
    const draco::Mesh::Face& face =
        draco_mesh.face(draco::FaceIndex(tri_index));
    tri[0] = face[0].value();
    tri[1] = face[1].value();
    tri[2] = face[2].value();
  }

  // Get the set of used verts.
  const uint32_t orig_vert_count = draco_mesh.num_points();
  std::vector<bool> orig_verts_used(orig_vert_count, false);
  const size_t used_vert_count =
      GetUsedPoints(orig_vert_count, indices, index_count, &orig_verts_used);

  // Copy attributes.
  VtArray<GfVec3f> pos(used_vert_count);
  if (CopyDracoVectors(
          draco_mesh, attrs, Gltf::Mesh::kAttributePosition,
          orig_verts_used, used_vert_count, pos.data())) {
    UFG_ASSERT_LOGIC(pos.size() == used_vert_count);
    out_info->pos.swap(pos);
  }
  VtArray<GfVec3f> norm(used_vert_count);
  if (CopyDracoVectors(
          draco_mesh, attrs, Gltf::Mesh::kAttributeNormal,
          orig_verts_used, used_vert_count, norm.data())) {
    UFG_ASSERT_LOGIC(norm.size() == used_vert_count);
    out_info->norm.swap(norm);
  }

  // Copy all UV sets.
  for (const Gltf::Mesh::Attribute& attr : attrs) {
    if (attr.semantic != Gltf::Mesh::kSemanticTexcoord) {
      continue;
    }
    VtArray<GfVec2f> uv(used_vert_count);
    if (CopyDracoVectors(
            draco_mesh, attrs, attr,
            orig_verts_used, used_vert_count, uv.data())) {
      UFG_ASSERT_LOGIC(uv.size() == used_vert_count);
      FlipVs(uv.size(), uv.data());
      out_info->uvs[attr.number].swap(uv);
    }
  }

  // Copy 3- or 4-component colors.
  const size_t color_stride = GetDracoAttributeComponentCount(
      draco_mesh, attrs, Gltf::Mesh::kAttributeColor0);
  if (color_stride != 0) {
    UFG_ASSERT_FORMAT(color_stride == 3 || color_stride == 4);
    if (color_stride == 3) {
      out_info->color_stride = 3;
      VtArray<GfVec3f> color3(used_vert_count);
      if (CopyDracoVectors(
              draco_mesh, attrs, Gltf::Mesh::kAttributeColor0,
              orig_verts_used, used_vert_count, color3.data())) {
        UFG_ASSERT_LOGIC(color3.size() == used_vert_count);
        out_info->color3.swap(color3);
      }
    } else {
      out_info->color_stride = 4;
      VtArray<GfVec4f> color4(used_vert_count);
      if (CopyDracoVectors(
              draco_mesh, attrs, Gltf::Mesh::kAttributeColor0,
              orig_verts_used, used_vert_count, color4.data())) {
        UFG_ASSERT_LOGIC(color4.size() == used_vert_count);
        out_info->color4.swap(color4);
      }
    }
  }

  // Copy skin indices and weights.
  const size_t skin_index_stride = GetDracoAttributeComponentCount(
      draco_mesh, attrs, Gltf::Mesh::kAttributeJoints0);
  if (skin_index_stride > 0) {
    const size_t skin_weight_stride = GetDracoAttributeComponentCount(
        draco_mesh, attrs, Gltf::Mesh::kAttributeWeights0);
    UFG_ASSERT_FORMAT(skin_weight_stride > 0);
    UFG_ASSERT_FORMAT(skin_index_stride <= SkinBinding::kInfluenceMax);
    UFG_ASSERT_FORMAT(skin_weight_stride <= SkinBinding::kInfluenceMax);
    std::vector<int> skin_indices(used_vert_count * skin_index_stride);
    UFG_VERIFY(CopyDracoScalars(
        draco_mesh, attrs, Gltf::Mesh::kAttributeJoints0,
        orig_verts_used, used_vert_count, skin_index_stride,
        skin_indices.data()));
    std::vector<float> skin_weights(used_vert_count * skin_weight_stride);
    UFG_VERIFY(CopyDracoScalars(
        draco_mesh, attrs, Gltf::Mesh::kAttributeWeights0,
        orig_verts_used, used_vert_count, skin_weight_stride,
        skin_weights.data()));
    UFG_ASSERT_FORMAT(skin_indices.size() ==
                      skin_index_stride * used_vert_count);
    UFG_ASSERT_FORMAT(skin_weights.size() ==
                      skin_weight_stride * used_vert_count);
    out_info->skin_index_stride = static_cast<uint8_t>(skin_index_stride);
    out_info->skin_weight_stride = static_cast<uint8_t>(skin_weight_stride);
    out_info->skin_indices.swap(skin_indices);
    out_info->skin_weights.swap(skin_weights);
  }

  out_orig_verts_used->swap(orig_verts_used);
  return used_vert_count;
}

bool GetPrimInfo(
    const Gltf& gltf, Gltf::Id mesh_id, const Gltf::Mesh& mesh,
    size_t prim_index,
    GltfCache* gltf_cache, PrimInfo* out_info, Logger* logger) {
  const Gltf::Mesh::Primitive& prim = mesh.primitives[prim_index];
  if (!Gltf::HasTriangles(prim.mode)) {
    Log<UFG_WARN_NON_TRIANGLES>(
        logger, "", Gltf::GetEnumName(prim.mode), Gltf::IdToIndex(mesh_id),
        prim_index, mesh.name.c_str());
    return false;
  }

  const Gltf::Mesh::AttributeSet& attrs = prim.attributes;

  // The Draco mesh (if present) contains a subset of vertex attributes. So we
  // first decode attributes from the Draco mesh, then load any additional ones
  // from the glTF structures.
  std::vector<uint32_t> indices;
  std::vector<bool> orig_verts_used;
  size_t used_vert_count;
  if (prim.draco.bufferView != Gltf::Id::kNull) {
    // Decompress mesh indices and all vertex attributes.
    size_t draco_size;
    const void* const draco_data =
        gltf_cache->GetViewData(prim.draco.bufferView, &draco_size);
    if (!draco_data) {
      Log<UFG_ERROR_DRACO_LOAD>(
          logger, "", Gltf::IdToIndex(mesh_id), prim_index, mesh.name.c_str());
      return false;
    }
    used_vert_count = GetMeshInfoFromDraco(
        draco_data, draco_size, prim.draco.attributes, mesh_id, mesh,
        prim_index, out_info, &indices, &orig_verts_used, logger);
    if (used_vert_count == 0) {
      return false;
    }
  } else {
    // Get just the uncompressed mesh indices. Uncompressed vertex attributes
    // are loaded below.
    const auto pos_found = attrs.find(Gltf::Mesh::kAttributePosition);
    if (pos_found == attrs.end()) {
      // Error already reported during glTF validation.
      return false;
    }
    const Gltf::Mesh::Attribute& pos_attr = *pos_found;
    const Gltf::Accessor& pos_accessor =
        *UFG_VERIFY(Gltf::GetById(gltf.accessors, pos_attr.accessor));
    const uint32_t orig_vert_count = pos_accessor.count;
    GetMeshIndices(gltf, prim, orig_vert_count, gltf_cache, &indices);
    orig_verts_used.resize(orig_vert_count, false);
    used_vert_count = GetUsedPoints(
        orig_vert_count, indices.data(), indices.size(), &orig_verts_used);
  }

  // Create mapping from original to used verts.
  const size_t orig_vert_count = orig_verts_used.size();
  std::vector<uint32_t> orig_to_used_vert_map(orig_vert_count, kNoIndex);
  for (uint32_t orig_vi = 0, used_vi = 0; orig_vi != orig_vert_count;
       ++orig_vi) {
    if (orig_verts_used[orig_vi]) {
      orig_to_used_vert_map[orig_vi] = used_vi;
      ++used_vi;
    }
  }

  // Generate indexed polygon for each triangle.
  GetTriIndices(orig_to_used_vert_map, indices.data(), indices.size(),
                &out_info->tri_vert_counts, &out_info->tri_vert_indices);

  // Copy attributes (skipping any that were already copied from the Draco mesh,
  // which allows for hybrid storage).
  VtArray<GfVec3f> pos;
  if (out_info->pos.empty() &&
      CopyAccessorToVectors(gltf, attrs, Gltf::Mesh::kAttributePosition,
                            orig_verts_used, gltf_cache, &pos)) {
    UFG_ASSERT_LOGIC(pos.size() == used_vert_count);
    out_info->pos.swap(pos);
  }
  VtArray<GfVec3f> norm;
  if (out_info->norm.empty() &&
      CopyAccessorToVectors(gltf, attrs, Gltf::Mesh::kAttributeNormal,
                            orig_verts_used, gltf_cache, &norm)) {
    UFG_ASSERT_LOGIC(norm.size() == used_vert_count);
    out_info->norm.swap(norm);
  }

  // Copy all UV sets.
  for (const Gltf::Mesh::Attribute& attr : attrs) {
    if (attr.semantic != Gltf::Mesh::kSemanticTexcoord) {
      continue;
    }
    const auto uv_found = out_info->uvs.find(attr.number);
    if (uv_found != out_info->uvs.end()) {
      continue;
    }
    VtArray<GfVec2f> uv;
    if (CopyAccessorToVectors(gltf, attrs, attr, orig_verts_used, gltf_cache,
                              &uv)) {
      UFG_ASSERT_LOGIC(uv.size() == used_vert_count);
      FlipVs(uv.size(), uv.data());
      out_info->uvs[attr.number].swap(uv);
    }
  }

  // Copy 3- or 4-component colors.
  const size_t color_stride =
      GetAttributeComponentCount(gltf, attrs, Gltf::Mesh::kAttributeColor0);
  if (color_stride != 0 && out_info->color_stride == 0) {
    UFG_ASSERT_FORMAT(color_stride == 3 || color_stride == 4);
    if (color_stride == 3) {
      out_info->color_stride = 3;
      VtArray<GfVec3f> color3;
      if (CopyAccessorToVectors(gltf, attrs, Gltf::Mesh::kAttributeColor0,
                                orig_verts_used, gltf_cache, &color3)) {
        UFG_ASSERT_LOGIC(color3.size() == used_vert_count);
        out_info->color3.swap(color3);
      }
    } else {
      out_info->color_stride = 4;
      VtArray<GfVec4f> color4;
      if (CopyAccessorToVectors(gltf, attrs, Gltf::Mesh::kAttributeColor0,
                                orig_verts_used, gltf_cache, &color4)) {
        UFG_ASSERT_LOGIC(color4.size() == used_vert_count);
        out_info->color4.swap(color4);
      }
    }
  }

  // Copy skin indices and weights.
  if (out_info->skin_index_stride == 0) {
    std::vector<int> skin_indices;
    const size_t skin_index_stride =
        CopyAccessorToScalars(gltf, attrs, Gltf::Mesh::kAttributeJoints0,
                              orig_verts_used, gltf_cache, &skin_indices);
    std::vector<float> skin_weights;
    const size_t skin_weight_stride =
        CopyAccessorToScalars(gltf, attrs, Gltf::Mesh::kAttributeWeights0,
                              orig_verts_used, gltf_cache, &skin_weights);
    if (skin_index_stride != 0) {
      UFG_ASSERT_FORMAT(skin_weight_stride != 0);
      UFG_ASSERT_FORMAT(skin_index_stride <= SkinBinding::kInfluenceMax);
      UFG_ASSERT_FORMAT(skin_indices.size() ==
                        skin_index_stride * used_vert_count);
      UFG_ASSERT_FORMAT(skin_weight_stride <= SkinBinding::kInfluenceMax);
      UFG_ASSERT_FORMAT(skin_weights.size() ==
                        skin_weight_stride * used_vert_count);
      out_info->skin_index_stride = static_cast<uint8_t>(skin_index_stride);
      out_info->skin_weight_stride = static_cast<uint8_t>(skin_weight_stride);
      out_info->skin_indices.swap(skin_indices);
      out_info->skin_weights.swap(skin_weights);
    }
  }

  return true;
}
}  // namespace

size_t GetUsedPoints(
    size_t pos_count, const uint32_t* indices, size_t count,
    std::vector<bool>* out_used) {
  std::vector<bool> used(pos_count, false);
  size_t point_count = 0;
  const uint32_t* const end = indices + count;
  for (const uint32_t* it = indices; it != end; ++it) {
    const size_t index = *it;
    UFG_ASSERT_FORMAT(index < pos_count);
    if (!used[index]) {
      used[index] = true;
      ++point_count;
    }
  }
  out_used->swap(used);
  return point_count;
}

void GetTriIndices(
    const std::vector<uint32_t>& pos_to_point_map,
    const uint32_t* indices, size_t count,
    VtArray<int>* out_vert_counts, VtArray<int>* out_vert_indices) {
  constexpr uint32_t kTriIndices = 3;
  UFG_ASSERT_LOGIC(count % kTriIndices == 0);
  const size_t face_count = count / kTriIndices;
  VtArray<int> vert_counts(face_count, kTriIndices);
  VtArray<int> vert_indices(count);
  const size_t pos_count = pos_to_point_map.size();
  int* dst = vert_indices.data();
  const uint32_t* const src_end = indices + count;
  for (const uint32_t* src_it = indices; src_it != src_end; ++src_it) {
    const size_t pos_index = *src_it;
    UFG_ASSERT_FORMAT(pos_index < pos_count);
    const int point_index = pos_to_point_map[pos_index];
    UFG_ASSERT_LOGIC(point_index != kNoIndex);
    *dst++ = point_index;
  }
  out_vert_counts->swap(vert_counts);
  out_vert_indices->swap(vert_indices);
}

void PrimInfo::Swap(PrimInfo* other) {
  tri_vert_counts.swap(other->tri_vert_counts);
  tri_vert_indices.swap(other->tri_vert_indices);
  pos.swap(other->pos);
  norm.swap(other->norm);
  uvs.swap(other->uvs);
  std::swap(color_stride, other->color_stride);
  std::swap(skin_index_stride, other->skin_index_stride);
  std::swap(skin_weight_stride, other->skin_weight_stride);
  color3.swap(other->color3);
  color4.swap(other->color4);
  skin_indices.swap(other->skin_indices);
  skin_weights.swap(other->skin_weights);
}

void GetMeshInfo(
    const Gltf& gltf, Gltf::Id mesh_id,
    GltfCache* gltf_cache, MeshInfo* out_info, Logger* logger) {
  const Gltf::Mesh& mesh = *UFG_VERIFY(Gltf::GetById(gltf.meshes, mesh_id));
  const size_t prim_count = mesh.primitives.size();
  out_info->prims.resize(prim_count);
  for (size_t prim_index = 0; prim_index != prim_count; ++prim_index) {
    PrimInfo prim_info;
    if (GetPrimInfo(
            gltf, mesh_id, mesh, prim_index, gltf_cache, &prim_info, logger)) {
      out_info->prims[prim_index].Swap(&prim_info);
    }
  }
}
}  // namespace ufg
