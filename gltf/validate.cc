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

#include "validate.h"  // NOLINT: Silence relative path warning.

#include "internal_util.h"  // NOLINT: Silence relative path warning.

namespace {
using Id = Gltf::Id;
using Uri = Gltf::Uri;
using Asset = Gltf::Asset;
using Accessor = Gltf::Accessor;
using Animation = Gltf::Animation;
using Buffer = Gltf::Buffer;
using BufferView = Gltf::BufferView;
using Camera = Gltf::Camera;
using Image = Gltf::Image;
using Material = Gltf::Material;
using Mesh = Gltf::Mesh;
using Node = Gltf::Node;
using Sampler = Gltf::Sampler;
using Scene = Gltf::Scene;
using Skin = Gltf::Skin;
using Texture = Gltf::Texture;

bool InRange(float value, float lower, float upper) {
  return value >= lower && value <= upper;
}

template <size_t kCount>
bool InRange(const float (&value)[kCount], float lower, float upper) {
  for (size_t i = 0; i != kCount; ++i) {
    if (value[i] < lower || value[i] > upper) {
      return false;
    }
  }
  return true;
}

bool IsValidPrimitiveMode(Mesh::Primitive::Mode mode, size_t index_count) {
  if (index_count == 0) {
    return false;
  }
  switch (mode) {
  case Mesh::Primitive::kModePoints:
    return true;
  case Mesh::Primitive::kModeLines:
    return index_count % 2 == 0;
  case Mesh::Primitive::kModeLineLoop:
  case Mesh::Primitive::kModeLineStrip:
    return index_count >= 2;
  case Mesh::Primitive::kModeTriangles:
    return index_count % 3 == 0;
  case Mesh::Primitive::kModeTriangleStrip:
  case Mesh::Primitive::kModeTriangleFan:
    return index_count >= 3;
  default:
    return false;
  }
}

std::string NumbersToCsv(const float* values, size_t value_count) {
  std::string csv;
  for (size_t i = 0; i != value_count; ++i) {
    if (!csv.empty()) {
      csv += ", ";
    }
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%f", values[i]);
    csv += buffer;
  }
  return csv;
}

std::string AttributeSetToCsv(const Mesh::AttributeSet& attrs) {
  std::string csv;
  for (const Mesh::Attribute& attr : attrs) {
    if (!csv.empty()) {
      csv += ", ";
    }
    csv += attr.ToString();
  }
  return csv;
}

bool HasExtension(
    const std::vector<Gltf::ExtensionId>& extensions, Gltf::ExtensionId key) {
  for (const Gltf::ExtensionId extension : extensions) {
    if (key == extension) {
      return true;
    }
  }
  return false;
}

class GltfValidator {
 public:
  void ValidateGltf(const Gltf& gltf, GltfLogger* logger) {
    gltf_ = &gltf;
    logger_ = logger;
    ValidateField("asset", gltf.asset);
    ValidateIdField("scene", gltf.scenes.size(), gltf.scene);
    ValidateExtensionsUsed();
    ValidateExtensionsRequired();
    ValidateFieldArray("accessors", gltf.accessors);
    ValidateFieldArray("animations", gltf.animations);
    ValidateFieldArray("buffers", gltf.buffers);
    ValidateFieldArray("bufferViews", gltf.bufferViews);
    ValidateFieldArray("cameras", gltf.cameras);
    ValidateFieldArray("images", gltf.images);
    ValidateFieldArray("materials", gltf.materials);
    ValidateFieldArray("meshes", gltf.meshes);
    ValidateFieldArray("nodes", gltf.nodes);
    ValidateFieldArray("samplers", gltf.samplers);
    ValidateFieldArray("scenes", gltf.scenes);
    ValidateFieldArray("skins", gltf.skins);
    ValidateFieldArray("textures", gltf.textures);

    CheckForNodeLoops();
  }

 private:
  const Gltf* gltf_;
  GltfLogger* logger_;
  GltfPathStack path_stack_;

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) {
    const std::string path = path_stack_.GetPath();
    GltfLog<kWhat>(logger_, path.c_str(), args...);
  }

  // Used as a placeholder for simple values that don't need validations.
  template <typename T>
  void IgnoreField(const char* name, const T& field) {}

  template <typename T, typename ...Args>
  void ValidateField(const char* name, const T& field, Args... args) {
    const GltfPathStack::Sentry path_sentry(&path_stack_, name);
    Validate(field, std::forward<Args>(args)...);
  }

  template <typename Element, typename ...Args>
  void ValidateArray(const Element* elements, size_t element_count,
                     Args... args) {
    for (uint32_t i = 0; i != element_count; ++i) {
      const GltfPathStack::Sentry path_sentry(&path_stack_, i);
      Validate(elements[i], std::forward<Args>(args)...);
    }
  }

  template <typename Element, typename... Args>
  void ValidateFieldArray(const char* name,
                          const std::vector<Element>& elements, Args... args) {
    const GltfPathStack::Sentry path_sentry(&path_stack_, name);
    ValidateArray(elements.data(), elements.size(),
                  std::forward<Args>(args)...);
  }

  template <typename Element, size_t kCount, typename... Args>
  void ValidateFieldArray(const char* name, const Element (&elements)[kCount],
                          Args... args) {
    const GltfPathStack::Sentry path_sentry(&path_stack_, name);
    ValidateArray(elements, kCount, std::forward<Args>(args)...);
  }

  void ValidateId(size_t size, Gltf::Id id) {
    if (id != Gltf::Id::kNull && Gltf::IdToIndex(id) >= size) {
      Log<GLTF_ERROR_ID_OUT_OF_RANGE>(Gltf::IdToIndex(id), size - 1);
    }
  }

  void ValidateIdField(const char* name, size_t size, Gltf::Id id) {
    const GltfPathStack::Sentry path_sentry(&path_stack_, name);
    ValidateId(size, id);
  }

  void ValidateIdFieldArray(const char* name, size_t size,
                            const Gltf::Id* ids, size_t count) {
    for (uint32_t i = 0; i != count; ++i) {
      const GltfPathStack::Sentry path_sentry(&path_stack_, i);
      ValidateId(size, ids[i]);
    }
  }

  void ValidateIdFieldArray(const char* name, size_t size,
                            const std::vector<Gltf::Id>& ids) {
    ValidateIdFieldArray(name, size, ids.data(), ids.size());
  }

  void ValidateInsideBufferView(
      Gltf::Id view_id, Accessor::ComponentType component_type,
      Accessor::Type type, size_t count, size_t offset) {
    const BufferView* const view = Gltf::GetById(gltf_->bufferViews, view_id);
    if (!view) {
      return;
    }

    const Buffer* const buffer = Gltf::GetById(gltf_->buffers, view->buffer);
    if (!buffer) {
      // Bad buffer reference reported separately.
      return;
    }
    const ptrdiff_t buffer_space =
        buffer->byteLength - view->byteOffset - offset;

    const ptrdiff_t view_space = view->byteLength - offset;
    if (view_space < 0) {
      // Some non-conforming glTF files have incorrect view byteLength. This is
      // recoverable if the buffer itself is correctly sized, so in these cases
      // demote it to a warning.
      if (buffer_space < 0) {
        Log<GLTF_ERROR_OFFSET_OUTSIDE_BUFFER_VIEW>(offset, view->byteLength);
      } else {
        Log<GLTF_WARN_OFFSET_OUTSIDE_BUFFER_VIEW>(offset, view->byteLength);
      }
    }
    const size_t component_size = Gltf::GetComponentSize(component_type);
    const size_t component_count = Gltf::GetComponentCount(type);
    const size_t stride_min = component_size * component_count;
    if (stride_min != 0) {
      if (view->byteStride != 0 && view->byteStride < stride_min) {
        Log<GLTF_ERROR_COMPONENT_EXCEEDS_STRIDE>(
            Gltf::GetEnumName(type), Gltf::GetEnumName(component_type),
            stride_min, view->byteStride);
      } else if (view_space >= 0) {
        const size_t stride =
            view->byteStride ? view->byteStride : component_size;
        const size_t size = count == 0 ? 0 : (count - 1) * stride + stride_min;
        if (static_cast<ptrdiff_t>(size) > view_space) {
          if (static_cast<ptrdiff_t>(size) > buffer_space) {
            Log<GLTF_ERROR_SIZE_EXCEEDS_BUFFER_VIEW>(size, view_space);
          } else {
            Log<GLTF_WARN_SIZE_EXCEEDS_BUFFER_VIEW>(size, view_space);
          }
        }
      }
    }
  }

  void ValidateRanged(float value, float lower, float upper) {
    if (!InRange(value, lower, upper)) {
      Log<GLTF_WARN_SCALAR_OUT_OF_RANGE>(value, lower, upper);
    }
  }

  template <size_t kCount>
  void ValidateRanged(const float (&value)[kCount], float lower, float upper) {
    if (!InRange(value, lower, upper)) {
      const std::string value_text = NumbersToCsv(value, kCount);
      Log<GLTF_WARN_VECTOR_OUT_OF_RANGE>(value_text.c_str(), lower, upper);
    }
  }

  template <typename Value, typename Limit>
  void ValidateRangedField(
      const char* name, const Value& value, Limit lower, Limit upper) {
    const GltfPathStack::Sentry path_sentry(&path_stack_, name);
    ValidateRanged(value, lower, upper);
  }

  template <typename Value>
  void ValidateTextureScaleField(const char* name, const Value& value) {
    // The spec requires texture scale factors be in the [0, 1] range, but
    // values slightly outside this range shouldn't be an issue.
    constexpr float kValueMin = 0.0f;
    constexpr float kValueMax = 1.001f;
    ValidateRangedField(name, value, kValueMin, kValueMax);
  }

  void Validate(const Asset& v) {
    IgnoreField("copyright", v.copyright);
    IgnoreField("generator", v.generator);
    IgnoreField("version", v.version);
    IgnoreField("minVersion", v.minVersion);
  }

  void Validate(const Accessor::Sparse::Indices& v, uint32_t count) {
    ValidateIdField("bufferView", gltf_->bufferViews.size(), v.bufferView);
    IgnoreField("componentType", v.componentType);
    IgnoreField("byteOffset", v.byteOffset);
    ValidateInsideBufferView(v.bufferView, v.componentType,
                             Accessor::kTypeScalar, count, v.byteOffset);
  }

  void Validate(const Accessor::Sparse::Values& v, uint32_t count,
                Accessor::ComponentType component_type) {
    ValidateIdField("bufferView", gltf_->bufferViews.size(), v.bufferView);
    IgnoreField("byteOffset", v.byteOffset);
    ValidateInsideBufferView(v.bufferView, component_type,
                             Accessor::kTypeScalar, count, v.byteOffset);
  }

  void Validate(const Accessor::Sparse& v,
                Accessor::ComponentType component_type) {
    IgnoreField("count", v.count);
    ValidateField("indices", v.indices, v.count);
    ValidateField("values", v.values, v.count, component_type);
  }

  void Validate(const Accessor& v) {
    IgnoreField("name", v.name);
    ValidateIdField("bufferView", gltf_->bufferViews.size(), v.bufferView);
    IgnoreField("normalized", v.normalized);
    IgnoreField("byteOffset", v.byteOffset);
    IgnoreField("componentType", v.componentType);
    IgnoreField("type", v.type);
    IgnoreField("count", v.count);

    // TODO: We should verify min<=max (appropriate to componentType
    // and type), but that's a lot of code to validate fields we don't use.
    IgnoreField("min", v.min);
    IgnoreField("max", v.max);

    ValidateInsideBufferView(v.bufferView, v.componentType, v.type, v.count,
                             v.byteOffset);
    ValidateField("sparse", v.sparse, v.componentType);
  }

  size_t GetNodeMorphTargetCount(Id node_id) const {
    const Node* const node = Gltf::GetById(gltf_->nodes, node_id);
    if (node && !node->weights.empty()) {
      return node->weights.size();
    }
    const Mesh* const mesh = Gltf::GetById(gltf_->meshes, node->mesh);
    return mesh ? mesh->weights.size() : 0;
  }

  void Validate(const Animation::Channel::Target& v,
                const Animation::Sampler* sampler) {
    ValidateIdField("node", gltf_->nodes.size(), v.node);
    IgnoreField("path", v.path);

    const Accessor* const input_accessor =
        sampler ? Gltf::GetById(gltf_->accessors, sampler->input) : nullptr;
    const Accessor* const output_accessor =
        sampler ? Gltf::GetById(gltf_->accessors, sampler->output) : nullptr;
    const size_t weight_count = GetNodeMorphTargetCount(v.node);

    // Verify input and output sizes match.
    if (input_accessor && output_accessor) {
      const size_t input_count = input_accessor->count;
      const size_t output_scalar_count = output_accessor->count;
      const size_t key_count =
          sampler->interpolation ==
                  Animation::Sampler::kInterpolationCubicSpline
              ? 3 : 1;
      const size_t output_component_count =
          weight_count ? key_count * weight_count : key_count;
      if (output_scalar_count % output_component_count != 0) {
        Log<GLTF_ERROR_NON_MULTIPLE_SIZE>(
            output_scalar_count, output_component_count);
      }
      const size_t output_count = output_scalar_count / output_component_count;
      if (output_count != input_count) {
        Log<GLTF_ERROR_INPUT_OUTPUT_SIZE_MISMATCH>(input_count, output_count);
      }
    }

    // For kPathWeights, verify node and output are compatible with
    // morph-targets.
    if (v.path == Animation::Channel::Target::kPathWeights && sampler) {
      if (weight_count == 0) {
        Log<GLTF_ERROR_NO_MORPH_WEIGHTS>();
      }
      if (output_accessor) {
        if (output_accessor->type != Accessor::kTypeScalar ||
            output_accessor->componentType != Accessor::kComponentFloat) {
          Log<GLTF_ERROR_BAD_MORPH_WEIGHT_FORMAT>(
              Gltf::GetEnumName(output_accessor->type),
              Gltf::GetEnumName(output_accessor->componentType));
        }
      }
    }
  }

  void Validate(const Animation::Channel& v,
                const std::vector<Animation::Sampler>& samplers) {
    ValidateIdField("sampler", samplers.size(), v.sampler);
    ValidateField("target", v.target, Gltf::GetById(samplers, v.sampler));
  }

  void Validate(const Animation::Sampler& v) {
    ValidateIdField("input", gltf_->accessors.size(), v.input);
    IgnoreField("interpolation", v.interpolation);
    ValidateIdField("output", gltf_->accessors.size(), v.output);

    const Accessor* const input_accessor =
        Gltf::GetById(gltf_->accessors, v.input);
    if (input_accessor) {
      if (input_accessor->type != Accessor::kTypeScalar) {
        Log<GLTF_ERROR_BAD_TIME_INPUT_FORMAT>(
            Gltf::GetEnumName(input_accessor->type));
      }
      // According to the spec, componentType must be kComponentFloat. But we
      // support other types, so let it slide.
    }
  }

  void Validate(const Animation& v) {
    IgnoreField("name", v.name);
    ValidateFieldArray("channels", v.channels, v.samplers);
    ValidateFieldArray("samplers", v.samplers);

    // Validate there are no conflicting targets.
    const size_t channel_count = v.channels.size();
    for (size_t i0 = 0; i0 != channel_count; ++i0) {
      const Animation::Channel& channel0 = v.channels[i0];
      const Id node0_id = channel0.target.node;
      const Animation::Channel::Target::Path path0 = channel0.target.path;
      for (size_t i1 = i0 + 1; i1 != channel_count; ++i1) {
        const Animation::Channel& channel1 = v.channels[i1];
        if (channel1.target.node == node0_id && channel1.target.path == path0) {
          Log<GLTF_ERROR_ANIMATION_CHANNEL_CONFLICT>(
              i1, i0, Gltf::IdToIndex(node0_id), Gltf::GetEnumName(path0));
        }
      }
    }
  }

  void Validate(const Buffer& v) {
    IgnoreField("name", v.name);
    IgnoreField("uri", v.uri);
    IgnoreField("byteLength", v.byteLength);
  }

  void Validate(const BufferView& v) {
    IgnoreField("name", v.name);
    ValidateIdField("buffer", gltf_->buffers.size(), v.buffer);
    IgnoreField("byteOffset", v.byteOffset);
    IgnoreField("byteLength", v.byteLength);
    IgnoreField("byteStride", v.byteStride);
    IgnoreField("target", v.target);

    // Verify the view fits within the buffer.
    const Buffer* const buffer = Gltf::GetById(gltf_->buffers, v.buffer);
    if (buffer) {
      if (v.byteOffset > buffer->byteLength) {
        Log<GLTF_ERROR_VIEW_OFFSET_GT_BUFFER_SIZE>(
            v.byteOffset, buffer->byteLength);
      } else {
        const ptrdiff_t space =
            static_cast<ptrdiff_t>(buffer->byteLength) - v.byteOffset;
        if (static_cast<ptrdiff_t>(v.byteLength) > space) {
          Log<GLTF_ERROR_VIEW_SIZE_GT_BUFFER_SPACE>(v.byteLength, space);
        }
      }
    }
  }

  void Validate(const Camera::Orthographic& v) {
    if (v.xmag == 0.0f) {
      Log<GLTF_ERROR_CAMERA_XMAG_0>();
    }
    if (v.ymag == 0.0f) {
      Log<GLTF_ERROR_CAMERA_YMAG_0>();
    }
    if (v.znear >= v.zfar) {
      Log<GLTF_ERROR_CAMERA_ZNEAR_PAST_ZFAR>(v.znear, v.zfar);
    }
  }

  void Validate(const Camera::Perspective& v) {
    if (v.aspectRatio < 0.0f) {
      Log<GLTF_ERROR_CAMERA_ASPECT_RATIO_LT_0>(v.aspectRatio);
    }
    if (v.yfov <= 0.0f) {
      Log<GLTF_ERROR_CAMERA_YFOV_LE_0>(v.yfov);
    }
    if (v.zfar != 0.0f && v.znear >= v.zfar) {
      Log<GLTF_ERROR_CAMERA_ZNEAR_PAST_ZFAR>(v.znear, v.zfar);
    }
  }

  void Validate(const Camera& v) {
    IgnoreField("name", v.name);
    IgnoreField("type", v.type);
    switch (v.type) {
    case Camera::kTypeOrthographic:
      ValidateField("orthographic", v.orthographic);
      break;
    case Camera::kTypePerspective:
      ValidateField("perspective", v.perspective);
      break;
    case Camera::kTypeCount:
    default:
      // Cannot occur. Silence the compiler warning.
      break;
    }
  }

  void Validate(const Image& v) {
    IgnoreField("name", v.name);
    IgnoreField("uri", v.uri);
    IgnoreField("mimeType", &v.mimeType);
    if (!v.uri.IsSet()) {
      ValidateIdField("bufferView", gltf_->bufferViews.size(), v.bufferView);
    }
  }

  void Validate(const Gltf::Material::Texture::Transform& v) {
    IgnoreField("offset", v.offset);
    IgnoreField("rotation", v.rotation);
    IgnoreField("scale", v.scale);
  }

  void Validate(const Material::Texture& v) {
    ValidateIdField("index", gltf_->textures.size(), v.index);
    IgnoreField("texCoord", v.texCoord);
    ValidateField("transform", v.transform);
  }

  void Validate(const Material::NormalTexture& v) {
    Validate(static_cast<const Material::Texture&>(v));
    IgnoreField("scale", v.scale);
  }

  void Validate(const Material::OcclusionTexture& v) {
    Validate(static_cast<const Material::Texture&>(v));
    IgnoreField("strength", v.strength);
  }

  void Validate(const Material::Pbr& v) {
    ValidateTextureScaleField("baseColorFactor", v.baseColorFactor);
    ValidateField("baseColorTexture", v.baseColorTexture);
    ValidateTextureScaleField("metallicFactor", v.metallicFactor);
    ValidateTextureScaleField("roughnessFactor", v.roughnessFactor);
    ValidateField("metallicRoughnessTexture", v.metallicRoughnessTexture);
  }

  void Validate(const Material::Pbr::SpecGloss& v) {
    ValidateTextureScaleField("diffuseFactor", v.diffuseFactor);
    ValidateField("diffuseTexture", v.diffuseTexture);
    ValidateTextureScaleField("specularFactor", v.specularFactor);
    ValidateTextureScaleField("glossinessFactor", v.glossinessFactor);
    ValidateField("specularGlossinessTexture", v.specularGlossinessTexture);
  }

  void Validate(const Material& v) {
    IgnoreField("name", v.name);
    ValidateField("pbrMetallicRoughness", v.pbr);
    if (v.pbr.specGloss) {
      ValidateField("specGloss", *v.pbr.specGloss);
    }
    ValidateField("normalTexture", v.normalTexture);
    ValidateField("occlusionTexture", v.occlusionTexture);
    ValidateField("emissiveTexture", v.emissiveTexture);
    ValidateRangedField("emissiveFactor", v.emissiveFactor, 0.0f, 1.0f);
    IgnoreField("alphaMode", v.alphaMode);
    IgnoreField("alphaCutoff", v.alphaCutoff);
    IgnoreField("doubleSided", v.doubleSided);
    IgnoreField("unlit", v.unlit);
  }

  void Validate(const Mesh::AttributeSet& v, size_t vert_count,
                const Mesh::AttributeSet* superset_attrs, bool is_draco) {
    for (const Mesh::Attribute& attr : v) {
      // Validate accessor size and format.
      // * Attributes in the Draco extension reference unique IDs in the
      //   compressed data rather than accessors, so we can't validate them
      //   here.
      if (!is_draco) {
        const Accessor* const accessor =
            Gltf::GetById(gltf_->accessors, attr.accessor);
        if (accessor) {
          if (vert_count != 0 && accessor->count != vert_count) {
            // Some non-conforming glTF files have accessors with 0 count. This
            // is recoverable because we use the count in the attribute rather
            // than the accessor for vertices, so demote it to a warning.
            const std::string attr_name = attr.ToString();
            if (accessor->count == 0) {
              Log<GLTF_WARN_ATTR_VERT_COUNT_MISMATCH>(
                  attr_name.c_str(), accessor->count, vert_count);
            } else {
              Log<GLTF_ERROR_ATTR_VERT_COUNT_MISMATCH>(
                  attr_name.c_str(), accessor->count, vert_count);
            }
          }
          const Gltf::SemanticInfo& semantic_info =
              Gltf::kSemanticInfos[attr.semantic];
          const size_t component_count =
              Gltf::GetComponentCount(accessor->type);
          if (component_count < semantic_info.component_min ||
              component_count > semantic_info.component_max) {
            Log<GLTF_ERROR_ATTR_BAD_COMPONENT_COUNT>(
                attr.ToString().c_str(), component_count,
                semantic_info.component_min, semantic_info.component_max);
          }
          if (attr.semantic == Mesh::kSemanticJoints &&
              accessor->componentType == Accessor::kComponentFloat) {
            Log<GLTF_ERROR_ATTR_JOINTS_ARE_FLOAT>(attr.ToString().c_str());
          }
        } else {
          Log<GLTF_ERROR_ATTR_INDEX_OUT_OF_RANGE>(
              attr.ToString().c_str(), Gltf::IdToIndex(attr.accessor),
              gltf_->accessors.size());
        }
      }

      // Verify this attribute belongs to the superset.
      if (superset_attrs) {
        if (superset_attrs->find(attr) == superset_attrs->end()) {
          Log<GLTF_ERROR_ATTR_NOT_IN_SUPERSET>(
              attr.ToString().c_str(),
              AttributeSetToCsv(*superset_attrs).c_str());
        }
      }
    }
  }

  void Validate(const Mesh::Primitive::Draco& v,
                const Mesh::AttributeSet& superset_attrs) {
    ValidateIdField("bufferView", gltf_->bufferViews.size(), v.bufferView);
    ValidateField("attributes", v.attributes, 0, &superset_attrs, true);
  }

  void CheckTexcoord(const char* input_name, const Material::Texture& input,
                     const Mesh::AttributeSet& attrs,
                     const Mesh::AttributeSet& draco_attrs) {
    if (input.index == Id::kNull) {
      return;
    }
    const Mesh::Attribute key(Mesh::kSemanticTexcoord, input.texCoord);
    if (attrs.find(key) == attrs.end() &&
        draco_attrs.find(key) == draco_attrs.end()) {
      Log<GLTF_WARN_MESH_MISSING_TEXCOORD>(input.texCoord, input_name);
    }
  }

  // Verify that for each JOINTS_# there is a corresponding WEIGHTS_#, and
  // vice-versa.
  // * This checks against both attrs and other_attrs, to support separate
  //   attributes sets for the Draco extension.
  void CheckPairedJointsAndWeights(const Mesh::AttributeSet& attrs,
                                   const Mesh::AttributeSet& other_attrs) {
    for (const Mesh::Attribute& attr : attrs) {
      const bool is_joints = attr.semantic == Mesh::kSemanticJoints;
      const bool is_weights = attr.semantic == Mesh::kSemanticWeights;
      if (is_joints || is_weights) {
        const Mesh::Attribute key(
            is_joints ? Mesh::kSemanticWeights : Mesh::kSemanticJoints,
            attr.number);
        if (attrs.find(key) == attrs.end() &&
            other_attrs.find(key) == other_attrs.end()) {
          Log<GLTF_ERROR_SKIN_JOINT_WEIGHT_MISMATCH>(
              attr.ToString().c_str(), key.ToString().c_str());
        }
      }
    }
  }

  void Validate(const Mesh::Primitive& v, size_t morph_target_count) {
    const Mesh::AttributeSet& attrs = v.attributes;
    const Mesh::AttributeSet& draco_attrs = v.draco.attributes;
    const auto pos_found = attrs.find(Mesh::kAttributePosition);
    const Accessor* const position_accessor =
        pos_found == attrs.end()
            ? nullptr
            : Gltf::GetById(gltf_->accessors, pos_found->accessor);
    const size_t vert_count = position_accessor ? position_accessor->count : 0;

    ValidateField("attributes", attrs, vert_count, nullptr, false);
    ValidateIdField("indices", gltf_->accessors.size(), v.indices);
    ValidateIdField("material", gltf_->materials.size(), v.material);
    IgnoreField("mode", v.mode);
    ValidateFieldArray("targets", v.targets, vert_count, &attrs, false);
    ValidateField("draco", v.draco, attrs);

    // We must have a POSITION attribute as a minimum.
    if (pos_found == attrs.end() &&
        draco_attrs.find(Mesh::kAttributePosition) == draco_attrs.end()) {
      Log<GLTF_ERROR_MISSING_POSITION>();
    }

    // Verify indices are unsigned scalar.
    const Accessor* const indices_accessor =
        Gltf::GetById(gltf_->accessors, v.indices);
    if (indices_accessor) {
      if (indices_accessor->type != Accessor::kTypeScalar ||
          !Gltf::IsComponentUnsigned(indices_accessor->componentType)) {
        Log<GLTF_ERROR_BAD_INDICES_FORMAT>(
            Gltf::GetEnumName(indices_accessor->type),
            Gltf::GetEnumName(indices_accessor->componentType));
      }
    }

    // Verify the number of indices is compatible with the primitive mode.
    const size_t index_count =
        indices_accessor ? indices_accessor->count : vert_count;
    if (index_count != 0 && !IsValidPrimitiveMode(v.mode, index_count)) {
      Log<GLTF_ERROR_BAD_PRIMITIVE_INDEX_COUNT>(
          Gltf::GetEnumName(v.mode), index_count);
    }

    // If we have skin indices, verify we have weights (and vice-versa).
    CheckPairedJointsAndWeights(attrs, draco_attrs);
    CheckPairedJointsAndWeights(draco_attrs, attrs);

    // Verify number of morph targets in the primitive matches the number
    // indicated in the mesh.
    if (v.targets.size() != morph_target_count) {
      Log<GLTF_ERROR_MORPH_TARGET_MISMATCH>(
          v.targets.size(), morph_target_count);
    }

    // Verify mesh contains any texcoord attributes referenced by the material.
    const Material* const material =
        Gltf::GetById(gltf_->materials, v.material);
    if (material) {
      CheckTexcoord("baseColorTexture",
          material->pbr.baseColorTexture, attrs, draco_attrs);
      CheckTexcoord("metallicRoughnessTexture",
          material->pbr.metallicRoughnessTexture, attrs, draco_attrs);
      if (material->pbr.specGloss) {
        CheckTexcoord("diffuseTexture",
            material->pbr.specGloss->diffuseTexture, attrs, draco_attrs);
        CheckTexcoord("specularGlossinessTexture",
            material->pbr.specGloss->specularGlossinessTexture,
            attrs, draco_attrs);
      }
      CheckTexcoord("normalTexture",
          material->normalTexture, attrs, draco_attrs);
      CheckTexcoord("occlusionTexture",
          material->occlusionTexture, attrs, draco_attrs);
      CheckTexcoord("emissiveTexture",
          material->emissiveTexture, attrs, draco_attrs);
    }
  }

  void Validate(const Mesh& v) {
    IgnoreField("name", v.name);
    ValidateFieldArray("primitives", v.primitives, v.weights.size());
    IgnoreField("weights", v.weights);
  }

  void Validate(const Node& v) {
    IgnoreField("name", v.name);
    ValidateIdField("camera", gltf_->cameras.size(), v.camera);
    ValidateIdField("mesh", gltf_->meshes.size(), v.mesh);
    ValidateIdField("skin", gltf_->skins.size(), v.skin);
    if (v.is_matrix) {
      IgnoreField("matrix", v.matrix);
    } else {
      IgnoreField("scale", v.scale);
      IgnoreField("rotation", v.rotation);
      IgnoreField("translation", v.translation);
    }
    ValidateIdFieldArray("children", gltf_->nodes.size(), v.children);
    IgnoreField("weights", v.weights);

    // Validate number of morph target weights matches that of the mesh.
    const Mesh* const mesh = Gltf::GetById(gltf_->meshes, v.mesh);
    const size_t weight_count = v.weights.size();
    if (mesh) {
      const size_t mesh_weight_count = mesh->weights.size();
      if (weight_count != 0 && weight_count != mesh_weight_count) {
        Log<GLTF_ERROR_MORPH_WEIGHT_MISMATCH>(weight_count, mesh_weight_count);
      }
    } else if (weight_count != 0) {
      Log<GLTF_ERROR_MORPH_NO_MESH>(weight_count);
    }
  }

  void Validate(const Sampler& v) {
    IgnoreField("name", v.name);
    IgnoreField("magFilter", v.magFilter);
    IgnoreField("minFilter", v.minFilter);
    IgnoreField("wrapS", v.wrapS);
    IgnoreField("wrapT", v.wrapT);
  }

  void Validate(const Scene& v) {
    IgnoreField("name", v.name);
    ValidateIdFieldArray("nodes", gltf_->nodes.size(), v.nodes);
  }

  void Validate(const Skin& v) {
    IgnoreField("name", v.name);
    ValidateIdField("inverseBindMatrices", gltf_->accessors.size(),
                    v.inverseBindMatrices);
    ValidateIdField("skeleton", gltf_->nodes.size(), v.skeleton);
    ValidateIdFieldArray("joints", gltf_->nodes.size(), v.joints);

    // Verify inverseBindMatrices type and length.
    const Accessor* const ibm_accessor =
        Gltf::GetById(gltf_->accessors, v.inverseBindMatrices);
    if (ibm_accessor) {
      if (ibm_accessor->type != Accessor::kTypeMat4 ||
          ibm_accessor->componentType != Accessor::kComponentFloat) {
        Log<GLTF_ERROR_BAD_INV_BIND_FORMAT>(
            Gltf::GetEnumName(ibm_accessor->type),
            Gltf::GetEnumName(ibm_accessor->componentType));
      }
      if (v.joints.size() != ibm_accessor->count) {
        Log<GLTF_ERROR_SKIN_JOINT_BIND_MISMATCH>(
            v.joints.size(), ibm_accessor->count);
      }
    }
  }

  void Validate(const Texture& v) {
    IgnoreField("name", v.name);
    ValidateIdField("sampler", gltf_->samplers.size(), v.sampler);
    ValidateIdField("source", gltf_->images.size(), v.source);
  }

  void CheckDuplicateExtensions(
      const std::vector<Gltf::ExtensionId>& extensions) {
    const size_t extension_count = extensions.size();
    for (size_t i0 = 0; i0 != extension_count; ++i0) {
      const Gltf::ExtensionId extension0 = extensions[i0];
      for (size_t i1 = i0 + 1; i1 != extension_count; ++i1) {
        const Gltf::ExtensionId extension1 = extensions[i1];
        if (extension0 == extension1) {
          Log<GLTF_WARN_EXTENSION_DUPLICATE>(Gltf::GetEnumName(extension0));
        }
      }
    }
  }

  void ValidateExtensionsUsed() {
    const GltfPathStack::Sentry path_sentry(&path_stack_, "extensionsUsed");

    // Check that extensionsUsed contains all referenced extensions.
    const std::vector<Gltf::ExtensionId> ref_extensions =
        gltf_->GetReferencedExtensions();
    for (const Gltf::ExtensionId extension : ref_extensions) {
      if (!HasExtension(gltf_->extensionsUsed, extension)) {
        Log<GLTF_WARN_EXTENSION_MISSING_USED>(Gltf::GetEnumName(extension));
      }
    }

    // Warn if an extension is marked as used but not actually referenced.
    for (const Gltf::ExtensionId extension : gltf_->extensionsUsed) {
      if (!HasExtension(ref_extensions, extension)) {
        Log<GLTF_WARN_EXTENSION_UNREFERENCED>(Gltf::GetEnumName(extension));
      }
    }

    CheckDuplicateExtensions(gltf_->extensionsUsed);
  }

  void ValidateExtensionsRequired() {
    const GltfPathStack::Sentry path_sentry(&path_stack_, "extensionsUsed");

    // Check that all required extensions exist in extensionsUsed.
    IgnoreField("extensionsRequired", gltf_->extensionsRequired);
    for (const Gltf::ExtensionId extension : gltf_->extensionsRequired) {
      if (!HasExtension(gltf_->extensionsUsed, extension)) {
        Log<GLTF_ERROR_EXTENSION_REQUIRED_UNUSED>(Gltf::GetEnumName(extension));
      }
    }

    CheckDuplicateExtensions(gltf_->extensionsRequired);
  }

  void CheckForNodeLoopsAt(Id root_id, Id* node_path, Id* node_path_tail,
                           Id* visited_root_ids) {
    const Id node_id = *node_path_tail;
    if (visited_root_ids[Gltf::IdToIndex(node_id)] == root_id) {
      // If we've already visited this node for this root, there must be a loop.
      std::string node_path_text;
      for (const Id* it = node_path; it <= node_path_tail; ++it) {
        char id_text[32];
        snprintf(id_text, sizeof(id_text), "%zu", Gltf::IdToIndex(*it));
        if (!node_path_text.empty()) {
          node_path_text += "->";
        }
        node_path_text += id_text;
      }
      Log<GLTF_ERROR_NODE_LOOP>(node_path_text.c_str());
      return;
    }

    // Visit children.
    visited_root_ids[Gltf::IdToIndex(node_id)] = root_id;
    const Node& node = *Gltf::GetById(gltf_->nodes, node_id);
    for (const Id child_id : node.children) {
      const Node* const child = Gltf::GetById(gltf_->nodes, child_id);
      if (child) {
        node_path_tail[1] = child_id;
        CheckForNodeLoopsAt(root_id, node_path, node_path_tail + 1,
                            visited_root_ids);
      }
    }
  }

  void CheckForNodeLoops() {
    const size_t node_count = gltf_->nodes.size();
    std::vector<Id> visited_root_ids(node_count, Id::kNull);
    std::vector<Id> node_path(node_count + 1);
    for (size_t root_index = 0; root_index != node_count; ++root_index) {
      if (visited_root_ids[root_index] != Id::kNull) {
        continue;
      }
      const Id root_id = Gltf::IndexToId(root_index);
      node_path[0] = root_id;
      CheckForNodeLoopsAt(root_id, node_path.data(), node_path.data(),
                          visited_root_ids.data());
    }
  }
};

template <typename Element>
const char* GetBufferOrImageRef(
    const std::vector<Element>& elements, size_t index) {
  const Element& element = elements[index];
  if (!element.uri.path.empty()) {
    return element.uri.path.c_str();
  } else if (!element.name.empty()) {
    return element.name.c_str();
  } else {
    return "<unnamed>";
  }
}

bool VerifyResourcesExist(
    GltfStream* gltf_stream, const Gltf& gltf, GltfLogger* logger) {
  const size_t buffer_count = gltf.buffers.size();
  size_t missing_count = 0;
  for (size_t buffer_index = 0; buffer_index != buffer_count; ++buffer_index) {
    if (!gltf_stream->BufferExists(gltf, Gltf::IndexToId(buffer_index))) {
      const std::string path = "buffers[" + std::to_string(buffer_index) + "]";
      const char* const ref = GetBufferOrImageRef(gltf.buffers, buffer_index);
      GltfLog<GLTF_ERROR_MISSING_BUFFER>(logger, path.c_str(), ref);
      ++missing_count;
    }
  }
  const size_t image_count = gltf.images.size();
  for (size_t image_index = 0; image_index != image_count; ++image_index) {
    if (!gltf_stream->ImageExists(gltf, Gltf::IndexToId(image_index))) {
      const std::string path = "images[" + std::to_string(image_index) + "]";
      const char* const ref = GetBufferOrImageRef(gltf.images, image_index);
      GltfLog<GLTF_ERROR_MISSING_IMAGE>(logger, path.c_str(), ref);
      ++missing_count;
    }
  }
  return missing_count == 0;
}
}  // namespace

bool GltfValidate(const Gltf& gltf, GltfLogger* logger) {
  const size_t old_error_count = logger->GetErrorCount();
  GltfValidator validator;
  validator.ValidateGltf(gltf, logger);
  const size_t new_error_count = logger->GetErrorCount();
  return new_error_count == old_error_count;
}

bool GltfLoadAndValidate(
    GltfStream* gltf_stream, const char* name, const GltfLoadSettings& settings,
    Gltf* out_gltf, GltfLogger* logger) {
  std::unique_ptr<std::istream> is = gltf_stream->GetGltfIStream();
  if (!is) {
    GltfLog<GLTF_ERROR_IO_OPEN_READ>(logger, "", name);
    return false;
  }
  Gltf gltf;
  if (!GltfLoad(*is, settings, &gltf, logger)) {
    return false;
  }
  if (!GltfValidate(gltf, logger)) {
    return false;
  }
  if (!VerifyResourcesExist(gltf_stream, gltf, logger)) {
    return false;
  }
  gltf.Swap(out_gltf);
  return true;
}
