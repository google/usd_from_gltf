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

#ifndef UFG_CONVERT_MATERIALIZER_H_
#define UFG_CONVERT_MATERIALIZER_H_

#include <map>
#include "common/common_util.h"
#include "common/config.h"
#include "common/logging.h"
#include "convert/convert_common.h"
#include "convert/convert_context.h"
#include "convert/convert_util.h"
#include "convert/texturator.h"
#include "gltf/cache.h"
#include "gltf/gltf.h"
#include "process/image.h"
#include "pxr/usd/sdf/path.h"
#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/shader.h"

namespace ufg {
using PXR_NS::SdfPath;
using PXR_NS::SdfValueTypeName;
using PXR_NS::TfToken;
using PXR_NS::UsdGeomScope;
using PXR_NS::UsdShadeMaterial;
using PXR_NS::UsdShadeShader;
using PXR_NS::UsdStageRefPtr;

// Creates and caches USD materials.
class Materializer {
 public:
  struct Uvset {
    UsdShadeShader shader;
    // TODO: It's possible for texture references in the material to
    // have different transforms but reference the same uvset. To handle this
    // properly, we'd need to duplicate the uvset and add a new plug for it in
    // the material. Since this isn't a common use-case, we can probably just
    // get away with forcing a uniform transform per uvset.
    Gltf::Material::Texture::Transform transform;
  };
  using UvsetMap = std::map<Gltf::Mesh::Attribute::Number, Uvset>;

  struct Value {
    SdfPath path;
    UsdShadeMaterial material;
    UvsetMap uvsets;
  };

  void Clear();
  void Begin(ConvertContext* cc);
  void End();
  const Value& FindOrCreate(Gltf::Id material_id);
  bool IsInvisible(Gltf::Id material_id);
  const std::vector<std::string>& GetWritten() const {
    return texturator_.GetWritten();
  }
  const std::vector<std::string>& GetCreatedDirectories() const {
    return texturator_.GetCreatedDirectories();
  }

 private:
  static const SdfPath kMaterialsPath;

  struct Key {
    Gltf::Material material;
    friend bool operator==(const Key& a, const Key& b) {
      return Gltf::Compare(a.material, b.material) == 0;
    }
    friend bool operator!=(const Key& a, const Key& b) { return !(a == b); }
    friend bool operator<(const Key& a, const Key& b) {
      return Gltf::Compare(a.material, b.material) < 0;
    }
  };

  using Map = std::map<Key, Value>;

  ConvertContext* cc_;
  UsdGeomScope scope_;
  Map materials_;
  Texturator texturator_;

  Texturator::Args GetDefaultTextureArgs() const;
  bool AddMaterialTextureUvset(
      Gltf::Id material_id, const SdfPath& material_path,
      const Gltf::Material::Texture& input, UvsetMap* uvsets);
  UsdShadeShader CreateTextureShader(uint32_t uvset_index,
                                     Gltf::Id sampler_id,
                                     const std::string& disk_path,
                                     const std::string& usd_path,
                                     const SdfPath& material_path,
                                     const UvsetMap& uvsets, const char* name);
  template <typename Vec>
  void AttachTextureInputTo(const std::string& usd_name, Gltf::Id sampler_id,
                            const Gltf::Material::Texture& input,
                            const SdfPath& material_path, const char* tex_name,
                            const TfToken& input_tok,
                            const SdfValueTypeName& input_type,
                            const Vec& scale, const Vec& fallback,
                            const TfToken& connect_tok,
                            const SdfValueTypeName& output_type,
                            const bool is_normal, UvsetMap* uvsets,
                            UsdShadeShader* pbr_shader);
  template <typename Vec>
  void AttachTextureInput(
      const Texturator::Args& tex_args, const Gltf::Material::Texture& input,
      Gltf::Id material_id, const SdfPath& material_path, const char* tex_name,
      const TfToken& input_tok, const SdfValueTypeName& input_type,
      const Vec& fallback, const TfToken& connect_tok,
      const SdfValueTypeName& output_type, const bool is_normal,
      UvsetMap* uvsets, UsdShadeShader* pbr_shader);
  void AttachBaseTextureInput(
      const std::string* usd_name, const Texturator::Args& tex_args,
      const Gltf::Material::Texture& input,
      Gltf::Id material_id, const SdfPath& material_path, const TfToken& tok,
      UvsetMap* uvsets, UsdShadeShader* pbr_shader);
  void AttachBaseTextureInput(
      const Texturator::Args& tex_args, const Gltf::Material::Texture& input,
      Gltf::Id material_id, const SdfPath& material_path, const TfToken& tok,
      UvsetMap* uvsets, UsdShadeShader* pbr_shader);
  void AttachUnlitTextureInputs(
      Gltf::Id material_id, const SdfPath& material_path,
      UvsetMap* uvsets, UsdShadeShader* pbr_shader);
  void AttachMetallicRoughnessTextureInput(
      Gltf::Id material_id, const SdfPath& material_path,
      UvsetMap* uvsets, UsdShadeShader* pbr_shader);
  void AttachSpecularGlossinessTextureInput(
      Gltf::Id material_id, const SdfPath& material_path,
      UvsetMap* uvsets, UsdShadeShader* pbr_shader);
};

}  // namespace ufg

#endif  // UFG_CONVERT_MATERIALIZER_H_
