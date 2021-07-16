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

#include "convert/materializer.h"

#include "common/config.h"
#include "convert/tokens.h"
#include "process/math.h"
#include "process/process_util.h"

namespace ufg {
namespace {

// Methods for converting generic vector values to GfVec4f for UsdUVTexture
// nodes.
GfVec4f ToVec4(const GfVec4f& value) { return value; } // NOLINT: Vector
// types valid for use by ToVec4 include Vec4s.

GfVec4f ToVec4(const GfVec3f& value) {
  return GfVec4f(value[0], value[1], value[2], 1.f);
}

GfVec4f ToVec4(const float value) {
  return GfVec4f(value, 0.f, 0.f, 1.f);
}

}  // namespace

using PXR_NS::SdfAssetPath;
using PXR_NS::UsdShadeInput;
using PXR_NS::VtValue;

const SdfPath Materializer::kMaterialsPath("Materials");

void Materializer::Clear() {
  cc_ = nullptr;
  scope_ = UsdGeomScope();
  materials_.clear();
  texturator_.Clear();
}

void Materializer::Begin(ConvertContext* cc) {
  cc_ = cc;
  scope_ =
      UsdGeomScope::Define(cc->stage, cc->root_path.AppendPath(kMaterialsPath));
  texturator_.Begin(cc);
}

void Materializer::End() {
  texturator_.End();
}

const Materializer::Value& Materializer::FindOrCreate(Gltf::Id material_id) {
  const size_t material_index = Gltf::IdToIndex(material_id);
  const Gltf::Material& material = cc_->gltf->materials[material_index];
  Key key = { material };
  if (cc_->settings.merge_identical_materials) {
    // Compare materials without respect to name to merge identical ones.
    key.material.name.clear();
  }
  const auto insert_result = materials_.insert(std::make_pair(key, Value()));
  Value& value = insert_result.first->second;
  if (!insert_result.second) {
    return value;
  }

  const std::string material_path_str = cc_->path_table.MakeUnique(
      scope_.GetPath(), "material", material.name, material_index);
  value.path = SdfPath(material_path_str);
  value.material = UsdShadeMaterial::Define(cc_->stage, value.path);

  const SdfPath pbr_shader_path = value.path.AppendElementString("pbr_shader");
  UsdShadeShader pbr_shader =
      UsdShadeShader::Define(cc_->stage, pbr_shader_path);
  pbr_shader.CreateIdAttr(VtValue(kTokPreviewSurface));
  value.material.CreateSurfaceOutput().ConnectToSource(pbr_shader.ConnectableAPI(), kTokSurface);

  if (material.unlit) {
    AttachUnlitTextureInputs(
        material_id, value.path, &value.uvsets, &pbr_shader);
  } else {
    if (material.pbr.specGloss) {
      AttachSpecularGlossinessTextureInput(
          material_id, value.path, &value.uvsets, &pbr_shader);
    } else {
      Texturator::Args base_args = GetDefaultTextureArgs();
      base_args.fallback = Texturator::kFallbackMagenta;
      base_args.alpha_mode = material.alphaMode;
      base_args.alpha_cutoff = material.alphaCutoff;
      base_args.scale.Assign(material.pbr.baseColorFactor);
      if (cc_->settings.bake_texture_color_scale_bias &&
          base_args.alpha_mode == Gltf::Material::kAlphaModeBlend) {
        // If we are baking the scale from the baseColorFactor, do not bake the
        // alpha. Instead, set the material opacity to that value.
        base_args.opacity = material.pbr.baseColorFactor[3];
        base_args.scale.Assign(material.pbr.baseColorFactor[0],
                               material.pbr.baseColorFactor[1],
                               material.pbr.baseColorFactor[2], 1.f);
      } else {
        base_args.scale.Assign(material.pbr.baseColorFactor);
      }
      AttachBaseTextureInput(
          base_args, material.pbr.baseColorTexture, material_id, value.path,
          kTokInputDiffuseColor, &value.uvsets, &pbr_shader);
      AttachMetallicRoughnessTextureInput(
          material_id, value.path, &value.uvsets, &pbr_shader);
    }

    Texturator::Args norm_args = GetDefaultTextureArgs();
    norm_args.usage = Texturator::kUsageNorm;
    norm_args.scale = material.normalTexture.scale;
    AttachTextureInput(
        norm_args, material.normalTexture, material_id, value.path,
        "tex_normal", kTokInputNormal, SdfValueTypeNames->Normal3f,
        kFallbackNormal, kTokRgb, SdfValueTypeNames->Float3, true,
        &value.uvsets, &pbr_shader);

    Texturator::Args emissive_args = GetDefaultTextureArgs();
    emissive_args.usage = cc_->settings.emissive_is_linear
                              ? Texturator::kUsageLinear
                              : Texturator::kUsageDefault;
    emissive_args.scale.Assign(material.emissiveFactor, 1.0f);
    AttachTextureInput(
        emissive_args, material.emissiveTexture, material_id, value.path,
        "tex_emissive", kTokInputEmissiveColor, SdfValueTypeNames->Color3f,
        kFallbackEmissive, kTokRgb, SdfValueTypeNames->Float3, false,
        &value.uvsets, &pbr_shader);

    Texturator::Args occl_args = GetDefaultTextureArgs();
    occl_args.usage = Texturator::kUsageOccl;
    occl_args.scale = material.occlusionTexture.strength;
    AttachTextureInput(
        occl_args, material.occlusionTexture, material_id, value.path,
        "tex_occlusion", kTokInputOcclusion, SdfValueTypeNames->Float,
        kFallbackOcclusion, kTokR, SdfValueTypeNames->Float, false,
        &value.uvsets, &pbr_shader);
  }

  if (cc_->settings.warn_ios_incompat && value.uvsets.size() > 1) {
    const std::string src_material_name =
        Gltf::GetName(cc_->gltf->materials, material_id, "material");
    LogOnce<UFG_WARN_MULTIPLE_UVSETS_UNSUPPORTED>(
        &cc_->once_logger, " Material(s): ", src_material_name.c_str());
  }

  return value;
}

bool Materializer::IsInvisible(Gltf::Id material_id) {
  const Gltf::Material* const material =
      Gltf::GetById(cc_->gltf->materials, material_id);
  if (!material) {
    return false;
  }

  // Transparency comes from base or diffuse textures.
  Gltf::Id texture_id;
  float alpha_scale;
  if (!material->unlit && material->pbr.specGloss) {
    texture_id = material->pbr.specGloss->diffuseTexture.index;
    alpha_scale = material->pbr.specGloss->diffuseFactor[kColorChannelA];
  } else {
    texture_id = material->pbr.baseColorTexture.index;
    alpha_scale = material->pbr.baseColorFactor[kColorChannelA];
  }

  const Gltf::Texture* const texture =
      Gltf::GetById(cc_->gltf->textures, texture_id);
  const Gltf::Id image_id = texture ? texture->source : Gltf::Id::kNull;
  return texturator_.IsAlphaFullyTransparent(image_id, alpha_scale, 0.0f);
}

Texturator::Args Materializer::GetDefaultTextureArgs() const {
  Texturator::Args args;
  args.resize = cc_->settings.image_resize;
  return args;
}

bool Materializer::AddMaterialTextureUvset(
    Gltf::Id material_id, const SdfPath& material_path,
    const Gltf::Material::Texture& input, UvsetMap* uvsets) {
  const uint8_t uvset_index = input.texCoord;
  if (cc_->settings.disable_multiple_uvsets) {
    if (!uvsets->empty() && uvsets->find(uvset_index) == uvsets->end()) {
      const std::string src_material_name =
          Gltf::GetName(cc_->gltf->materials, material_id, "material");
      LogOnce<UFG_WARN_SECONDARY_UVSET_DISABLED>(
          &cc_->once_logger, " Material(s): ", src_material_name.c_str());
      return false;
    }
  }

  const auto insert_result =
      uvsets->insert(std::make_pair(uvset_index, Uvset()));
  if (!insert_result.second) {
    // Uvset already referenced.
    return true;
  }

  // TODO: Verify uvset exists, and set the format accordingly.
  // * This is complicated by the fact that this info is only stored with the
  //   mesh primitive, and could technically vary between primitives. We might
  //   get away with assuming it doesn't though.

  const std::string uvset_name = AppendNumber("uvset", uvset_index);
  const std::string st_name = AppendNumber("st", uvset_index);

  Uvset& uvset = insert_result.first->second;
  const SdfPath uvset_path = material_path.AppendElementString(uvset_name);
  uvset.shader = UsdShadeShader::Define(cc_->stage, uvset_path);
  uvset.shader.CreateIdAttr(VtValue(kTokPrimvarFloat2));
  uvset.shader.CreateInput(kTokVarname, SdfValueTypeNames->Token)
      .Set(TfToken(st_name));
  uvset.transform = input.transform;
  return true;
}

UsdShadeShader Materializer::CreateTextureShader(
    uint32_t uvset_index, Gltf::Id sampler_id, const std::string& disk_path,
    const std::string& usd_path, const SdfPath& material_path,
    const UvsetMap& uvsets, const char* name) {
  const SdfPath tex_path = material_path.AppendElementString(name);
  UsdShadeShader tex = UsdShadeShader::Define(cc_->stage, tex_path);
  tex.CreateIdAttr(VtValue(kTokUvTexture));
  tex.CreateInput(kTokFile, SdfValueTypeNames->Asset)
      .Set(SdfAssetPath(usd_path));

  const auto uvset_found = uvsets.find(uvset_index);
  UFG_ASSERT_LOGIC(uvset_found != uvsets.end());
  tex.CreateInput(kTokSt, SdfValueTypeNames->Float2)
      .ConnectToSource(uvset_found->second.shader.ConnectableAPI(), kTokResult);

  // Set sampler states.
  Gltf::Sampler::WrapMode wrap_s =
      GltfEnumInfo<Gltf::Sampler::WrapMode>::kDefault;
  Gltf::Sampler::WrapMode wrap_t =
      GltfEnumInfo<Gltf::Sampler::WrapMode>::kDefault;
  const Gltf::Sampler* const sampler =
      Gltf::GetById(cc_->gltf->samplers, sampler_id);
  if (sampler) {
    // TODO: It would be possible to emulate wrap modes by
    // tessellating geometry at integer UV values (but it's complicated and
    // expensive).
    if (cc_->settings.warn_ios_incompat &&
        (!Gltf::IsUnsetOrDefault(sampler->wrapS) ||
         !Gltf::IsUnsetOrDefault(sampler->wrapT))) {
      const std::string src_sampler_name =
          Gltf::GetName(cc_->gltf->samplers, sampler_id, "sampler");
      LogOnce<UFG_WARN_TEXTURE_WRAP_UNSUPPORTED>(
          &cc_->once_logger, " Sampler(s): ", src_sampler_name.c_str(),
          Gltf::GetEnumNameOrDefault(sampler->wrapS),
          Gltf::GetEnumNameOrDefault(sampler->wrapT));
    }
    if (!Gltf::IsUnsetOrDefault(sampler->wrapS)) {
      wrap_s = sampler->wrapS;
    }
    if (!Gltf::IsUnsetOrDefault(sampler->wrapT)) {
      wrap_t = sampler->wrapT;
    }

    if (cc_->settings.warn_ios_incompat &&
        (!Gltf::IsUnsetOrDefault(sampler->minFilter) ||
         !Gltf::IsUnsetOrDefault(sampler->magFilter))) {
      const std::string src_sampler_name =
          Gltf::GetName(cc_->gltf->samplers, sampler_id, "sampler");
      LogOnce<UFG_WARN_TEXTURE_FILTER_UNSUPPORTED>(
          &cc_->once_logger, " Sampler(s): ", src_sampler_name.c_str(),
          Gltf::GetEnumNameOrDefault(sampler->minFilter),
          Gltf::GetEnumNameOrDefault(sampler->magFilter));
    }
    CreateEnumInput(kTokFilterMin, sampler->minFilter,
                    kTokFilterLinearMipmapLinear, &tex);
    CreateEnumInput(kTokFilterMag, sampler->magFilter, kTokFilterLinear, &tex);
  }

  // Wrap states must be set unconditionally because different renderers may
  // have different defaults (in particular, default wrap state is repeat on
  // iOS but clamp on OSX).
  CreateEnumInput(kTokWrapS, wrap_s, kTokEmpty, &tex);
  CreateEnumInput(kTokWrapT, wrap_t, kTokEmpty, &tex);

  return tex;
}

template <typename Vec>void Materializer::AttachTextureInputTo(
    const std::string& usd_name, Gltf::Id sampler_id,
    const Gltf::Material::Texture& input, const SdfPath& material_path,
    const char* tex_name, const TfToken& input_tok,
    const SdfValueTypeName& input_type, const Vec& scale, const Vec& fallback,
    const TfToken& connect_tok,
    const SdfValueTypeName& output_type, const bool is_normal,
    UvsetMap* uvsets, UsdShadeShader* pbr_shader) {
  UsdShadeInput in = pbr_shader->CreateInput(input_tok, input_type);

  const std::string disk_path = Gltf::JoinPath(cc_->dst_dir, usd_name);
  UsdShadeShader tex =
      CreateTextureShader(input.texCoord, sampler_id, disk_path, usd_name,
                          material_path, *uvsets, tex_name);
  tex.CreateInput(kTokFallback, SdfValueTypeNames->Float4)
      .Set(ToVec4(fallback));
  if (is_normal) {
    // As per USD documentation, normal maps should have scale of 2 and bias
    // of -1. We multiply by the scale provided by gltf, which is 1 by
    // default.
    if (!cc_->settings.bake_texture_color_scale_bias) {
      tex.CreateInput(kTokScale, SdfValueTypeNames->Float4)
          .Set(ToVec4(scale) * 2.0f);
    } else {
      // Any scaling specified in the gltf will have been baked into the normal
      // texture.
      tex.CreateInput(kTokScale, SdfValueTypeNames->Float4)
          .Set(GfVec4f(2.0f));
    }
    tex.CreateInput(kTokBias, SdfValueTypeNames->Float4)
        .Set(GfVec4f(-1.0f));
  } else if (!cc_->settings.bake_texture_color_scale_bias) {
      tex.CreateInput(kTokScale, SdfValueTypeNames->Float4)
          .Set(ToVec4(scale));
  }
  tex.CreateOutput(connect_tok, output_type);
  in.ConnectToSource(tex.ConnectableAPI(), connect_tok);
}


template <typename Vec>
void Materializer::AttachTextureInput(
    const Texturator::Args& tex_args, const Gltf::Material::Texture& input,
    Gltf::Id material_id, const SdfPath& material_path, const char* tex_name,
    const TfToken& input_tok, const SdfValueTypeName& input_type,
    const Vec& fallback, const TfToken& connect_tok,
    const SdfValueTypeName& output_type, const bool is_normal, UvsetMap* uvsets,
    UsdShadeShader* pbr_shader) {
  const Vec scale = ColorToVec<Vec>(tex_args.scale);
  UsdShadeInput in = pbr_shader->CreateInput(input_tok, input_type);
  const Gltf::Texture* const texture =
      Gltf::GetById(cc_->gltf->textures, input.index);
  const bool uvset_valid =
      !texture ||
      AddMaterialTextureUvset(material_id, material_path, input, uvsets);
  if (!texture || !uvset_valid) {
    in.Set(uvset_valid ? scale : fallback);
    return;
  }
  const std::string& usd_name = texturator_.Add(texture->source, tex_args);
  AttachTextureInputTo(usd_name, texture->sampler, input, material_path,
                       tex_name, input_tok, input_type, scale, fallback,
                       connect_tok, output_type, is_normal, uvsets, pbr_shader);
}

void Materializer::AttachBaseTextureInput(
    const std::string* usd_name, const Texturator::Args& tex_args,
    const Gltf::Material::Texture& input,
    Gltf::Id material_id, const SdfPath& material_path,
    const TfToken& tok, UvsetMap* uvsets, UsdShadeShader* pbr_shader) {
  // Alpha cutoff is not currently supported by USDPreviewSurface.
  // See: https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html
  if (!cc_->settings.bake_alpha_cutoff &&
      tex_args.alpha_mode == Gltf::Material::kAlphaModeMask) {
    const std::string src_material_name =
        Gltf::GetName(cc_->gltf->materials, material_id, "material");
    LogOnce<UFG_WARN_ALPHA_MASK_UNSUPPORTED>(
        &cc_->once_logger, " Material(s): ", src_material_name.c_str());
  }

  // TODO: Vertex colors.
  // * According to the glTF docs, the base color should be
  //   texture*baseColorFactor*vertexColor.
  // * This works in Usdview for untextured geometry.  Just need to connect in
  //   to the vertex colors in the mesh.
  // * This won't work if we need all 3 inputs (constant, textured, and vertex),
  //   because there's only a one texture and one scale input. To work around
  //   this, we'd need to bake the constant color into the texture or vertex
  //   colors.
  //   o In a similar vein, we could make constant+vertex color work in the
  //     viewer by baking the constant into the vertices.
  UsdShadeInput in = pbr_shader->CreateInput(tok, SdfValueTypeNames->Color3f);
  UsdShadeShader tex;
  const bool uvset_valid =
      !usd_name ||
      AddMaterialTextureUvset(material_id, material_path, input, uvsets);
  Gltf::Id image_id = Gltf::Id::kNull;
  if (usd_name && uvset_valid) {
    const std::string disk_path = Gltf::JoinPath(cc_->dst_dir, *usd_name);
    const Gltf::Texture* const texture =
        Gltf::GetById(cc_->gltf->textures, input.index);
    image_id = texture->source;
    tex = CreateTextureShader(input.texCoord, texture->sampler, disk_path,
                              *usd_name, material_path, *uvsets, "tex_base");

    tex.CreateInput(kTokFallback, SdfValueTypeNames->Float4).Set(kFallbackBase);
    if (!cc_->settings.bake_texture_color_scale_bias) {
      tex.CreateInput(kTokScale, SdfValueTypeNames->Float4)
          .Set(tex_args.scale.ToVec4());
    }
    tex.CreateOutput(kTokRgb, SdfValueTypeNames->Float3);
    tex.CreateOutput(kTokA, SdfValueTypeNames->Float);
    in.ConnectToSource(tex.ConnectableAPI(), kTokRgb);
  } else {
    in.Set(uvset_valid ? tex_args.scale.ToVec3() : ToVec3(kFallbackBase));
  }

  // Blending is enabled based on alpha_mode, but disabled if the effective
  // alpha is 1.0.
  const bool blend = tex_args.alpha_mode != Gltf::Material::kAlphaModeOpaque &&
                     (!texturator_.IsAlphaOpaque(image_id, tex_args.scale.a,
                                                 tex_args.bias.a) ||
                      tex_args.opacity < 1.0f - kColorTol);
  const bool use_tex_args_opacity =
      cc_->settings.bake_texture_color_scale_bias &&
      tex_args.alpha_mode == Gltf::Material::kAlphaModeBlend;
  if (blend) {
    UsdShadeInput in_opacity =
        pbr_shader->CreateInput(kTokInputOpacity, SdfValueTypeNames->Float);
    if (tex) {
      if (use_tex_args_opacity && tex_args.opacity < 1.0f - kColorTol) {
        // Scalar opacity is only used if we are baking the baseColorFactor
        // into the texture.
        in_opacity.Set(tex_args.opacity);
      } else {
        in_opacity.ConnectToSource(tex.ConnectableAPI(), kTokA);
      }
    } else {
      // The scale alpha value is 1 if we are baking the baseColorFactor into
      // the texture even if the texture does not exist. Instead we use the
      // opacity tex_args value in this case.
      in_opacity.Set(use_tex_args_opacity ? tex_args.opacity
                                          : tex_args.scale.a);
    }
  }
}

void Materializer::AttachBaseTextureInput(
    const Texturator::Args& tex_args, const Gltf::Material::Texture& input,
    Gltf::Id material_id, const SdfPath& material_path,
    const TfToken& tok, UvsetMap* uvsets, UsdShadeShader* pbr_shader) {
  const Gltf::Texture* const texture =
      Gltf::GetById(cc_->gltf->textures, input.index);
  const std::string* const usd_name =
      texture ? &texturator_.Add(texture->source, tex_args) : nullptr;
  AttachBaseTextureInput(
      usd_name, tex_args, input, material_id, material_path, tok,
      uvsets, pbr_shader);
}

void Materializer::AttachUnlitTextureInputs(
    Gltf::Id material_id, const SdfPath& material_path,
    UvsetMap* uvsets, UsdShadeShader* pbr_shader) {
  // USDPreviewSurface doesn't directly support unlit mode, but we can simulate
  // it by zeroing the lighting factors and using the base color as emissive.
  // * Note, this isn't quite correct because there is a rim-light effect that
  //   cannot be eliminated via material properties. This lightens silhouettes
  //   slightly.
  const Gltf::Material& material =
      *UFG_VERIFY(Gltf::GetById(cc_->gltf->materials, material_id));

  // Alpha cutoff is not currently supported by USDPreviewSurface.
  // See: https://graphics.pixar.com/usd/docs/UsdPreviewSurface-Proposal.html
  if (!cc_->settings.bake_alpha_cutoff &&
      material.alphaMode == Gltf::Material::kAlphaModeMask) {
    const std::string src_material_name =
        Gltf::GetName(cc_->gltf->materials, material_id, "material");
    LogOnce<UFG_WARN_ALPHA_MASK_UNSUPPORTED>(
        &cc_->once_logger, " Material(s): ", src_material_name.c_str());
  }

  pbr_shader->CreateInput(kTokInputMetallic, SdfValueTypeNames->Float)
      .Set(1.0f);
  pbr_shader->CreateInput(kTokInputRoughness, SdfValueTypeNames->Float)
      .Set(1.0f);

  const Gltf::Material::Texture& input = material.pbr.baseColorTexture;
  const Gltf::Texture* const texture =
      Gltf::GetById(cc_->gltf->textures, input.index);

  // Attach emissive RGB input.
  UsdShadeInput emissive_in = pbr_shader->CreateInput(
      kTokInputEmissiveColor, SdfValueTypeNames->Color3f);
  {
    Texturator::Args tex_args = GetDefaultTextureArgs();
    tex_args.usage = cc_->settings.emissive_is_linear
                         ? Texturator::kUsageLinear
                         : Texturator::kUsageDefault;
    tex_args.fallback = Texturator::kFallbackMagenta;
    tex_args.scale.Assign(material.pbr.baseColorFactor);
    const std::string* const usd_name =
        texture ? &texturator_.Add(texture->source, tex_args) : nullptr;
    const bool uvset_valid =
        !usd_name ||
        AddMaterialTextureUvset(material_id, material_path, input, uvsets);
    if (usd_name && uvset_valid) {
      const std::string disk_path = Gltf::JoinPath(cc_->dst_dir, *usd_name);
      const Gltf::Texture* const texture =
          Gltf::GetById(cc_->gltf->textures, input.index);
      UsdShadeShader tex = CreateTextureShader(
          input.texCoord, texture->sampler, disk_path, *usd_name, material_path,
          *uvsets, "tex_emissive");
      tex.CreateInput(kTokFallback, SdfValueTypeNames->Float4)
          .Set(kFallbackBase);
      if (!cc_->settings.bake_texture_color_scale_bias) {
        tex.CreateInput(kTokScale, SdfValueTypeNames->Float4)
            .Set(tex_args.scale.ToVec4());
      }
      tex.CreateOutput(kTokRgb, SdfValueTypeNames->Float3);
      tex.CreateOutput(kTokA, SdfValueTypeNames->Float);
      emissive_in.ConnectToSource(tex.ConnectableAPI(), kTokRgb);
    } else {
      emissive_in.Set(uvset_valid ? tex_args.scale.ToVec3()
                                  : ToVec3(kFallbackBase));
    }
  }

  UsdShadeInput diffuse_in = pbr_shader->CreateInput(
      kTokInputDiffuseColor, SdfValueTypeNames->Color3f);
  if (material.alphaMode == Gltf::Material::kAlphaModeOpaque) {
    diffuse_in.Set(kColorBlack);
  } else {
    UsdShadeInput opacity_in =
        pbr_shader->CreateInput(kTokInputOpacity, SdfValueTypeNames->Float);
    const int solid_alpha = texture ? texturator_.GetSolidAlpha(texture->source)
                                    : Image::kComponentMax;
    const float opacity_scale = material.pbr.baseColorFactor[3];
    if (solid_alpha < 0) {
      // Varying alpha. Create a solid black base texture with alpha from the
      // unlit source.
      Texturator::Args tex_args = GetDefaultTextureArgs();
      tex_args.usage = Texturator::kUsageUnlitA;
      tex_args.fallback = Texturator::kFallbackBlack;
      tex_args.alpha_mode = material.alphaMode;
      tex_args.alpha_cutoff = material.alphaCutoff;
      tex_args.scale.Assign(1.0f, 1.0f, 1.0f, opacity_scale);
      const std::string* const usd_name =
          texture ? &texturator_.Add(texture->source, tex_args) : nullptr;
      const bool uvset_valid =
          !usd_name ||
          AddMaterialTextureUvset(material_id, material_path, input, uvsets);
      if (usd_name && uvset_valid) {
        const std::string disk_path = Gltf::JoinPath(cc_->dst_dir, *usd_name);
        const Gltf::Texture* const texture =
            Gltf::GetById(cc_->gltf->textures, input.index);
        UsdShadeShader tex = CreateTextureShader(
            input.texCoord, texture->sampler, disk_path, *usd_name,
            material_path, *uvsets, "tex_opacity");
        tex.CreateInput(kTokFallback, SdfValueTypeNames->Float4)
            .Set(kFallbackBase);
        if (!cc_->settings.bake_texture_color_scale_bias) {
          tex.CreateInput(kTokScale, SdfValueTypeNames->Float4)
              .Set(tex_args.scale.ToVec4());
        }
        tex.CreateOutput(kTokRgb, SdfValueTypeNames->Float3);
        tex.CreateOutput(kTokA, SdfValueTypeNames->Float);
        diffuse_in.ConnectToSource(tex.ConnectableAPI(), kTokRgb);
        opacity_in.ConnectToSource(tex.ConnectableAPI(), kTokA);
      } else {
        diffuse_in.Set(kColorBlack);
        opacity_in.Set(opacity_scale);
      }
    } else {
      // Solid alpha. Just set opacity to a constant.
      diffuse_in.Set(kColorBlack);
      opacity_in.Set(Image::ComponentToFloat(solid_alpha) * opacity_scale);
    }
  }
}

void Materializer::AttachMetallicRoughnessTextureInput(
    Gltf::Id material_id, const SdfPath& material_path,
    UvsetMap* uvsets, UsdShadeShader* pbr_shader) {
  const Gltf::Material& material =
      *UFG_VERIFY(Gltf::GetById(cc_->gltf->materials, material_id));
  // Set constant value.
  const Gltf::Material::Texture& input = material.pbr.metallicRoughnessTexture;
  const Gltf::Texture* const texture =
      Gltf::GetById(cc_->gltf->textures, input.index);
  if (!texture) {
    pbr_shader->CreateInput(kTokInputMetallic, SdfValueTypeNames->Float)
        .Set(material.pbr.metallicFactor);
    pbr_shader->CreateInput(kTokInputRoughness, SdfValueTypeNames->Float)
        .Set(material.pbr.roughnessFactor);
    return;
  }

  Texturator::Args metal_args = GetDefaultTextureArgs();
  metal_args.usage = Texturator::kUsageMetal;
  metal_args.scale = material.pbr.metallicFactor;
  AttachTextureInput(
      metal_args, input, material_id, material_path,
      "tex_metallic", kTokInputMetallic, SdfValueTypeNames->Float,
      kFallbackMetallic, kTokR, SdfValueTypeNames->Float, false,
      uvsets, pbr_shader);

  Texturator::Args rough_args = GetDefaultTextureArgs();
  rough_args.usage = Texturator::kUsageRough;
  rough_args.scale = material.pbr.roughnessFactor;
  AttachTextureInput(
      rough_args, input, material_id, material_path,
      "tex_roughness", kTokInputRoughness, SdfValueTypeNames->Float,
      kFallbackRoughness, kTokR, SdfValueTypeNames->Float, false,
      uvsets, pbr_shader);
}

void Materializer::AttachSpecularGlossinessTextureInput(
    Gltf::Id material_id, const SdfPath& material_path,
    UvsetMap* uvsets, UsdShadeShader* pbr_shader) {
  const Gltf::Material& material =
      *UFG_VERIFY(Gltf::GetById(cc_->gltf->materials, material_id));
  const Gltf::Material::Pbr::SpecGloss& spec_gloss = *material.pbr.specGloss;
  Texturator::Args base_args = GetDefaultTextureArgs();
  base_args.fallback = Texturator::kFallbackMagenta;
  base_args.alpha_mode = material.alphaMode;
  base_args.alpha_cutoff = material.alphaCutoff;
  base_args.scale.Assign(spec_gloss.diffuseFactor);

  const Gltf::Material::Texture& input = spec_gloss.specularGlossinessTexture;
  const Gltf::Texture* const spec_gloss_texture =
      Gltf::GetById(cc_->gltf->textures, input.index);
  const Gltf::Texture* const diff_texture =
      Gltf::GetById(cc_->gltf->textures, spec_gloss.diffuseTexture.index);

  // The emulation requires mixing diffuse+specular to generate base+metallic
  // textures, so we need the UV mapping to be identical.
  const bool shared_uvs = !spec_gloss_texture || !diff_texture ||
                          input.texCoord == spec_gloss.diffuseTexture.texCoord;

  if (cc_->settings.emulate_specular_workflow && shared_uvs) {
    const bool have_any_texture = spec_gloss_texture || diff_texture;
    const bool diff_uvset_valid =
        !diff_texture ||
        AddMaterialTextureUvset(
            material_id, material_path, spec_gloss.diffuseTexture, uvsets);
    const bool spec_uvset_valid =
        !spec_gloss_texture ||
        AddMaterialTextureUvset(material_id, material_path, input, uvsets);
    const bool uvsets_valid = diff_uvset_valid && spec_uvset_valid;
    if (!have_any_texture || !uvsets_valid) {
      // Untextured, either due to untextured source inputs, or due to invalid
      // UV set. Convert single element.
      float metal;
      SpecDiffToMetalBase(spec_gloss.specularFactor, spec_gloss.diffuseFactor,
                          &metal, base_args.scale.c);
      AttachBaseTextureInput(
          base_args, spec_gloss.diffuseTexture, material_id, material_path,
          kTokInputDiffuseColor, uvsets, pbr_shader);
      pbr_shader->CreateInput(kTokInputMetallic, SdfValueTypeNames->Float)
          .Set(uvsets_valid ? metal : kFallbackMetallic);
      pbr_shader->CreateInput(kTokInputRoughness, SdfValueTypeNames->Float)
          .Set(uvsets_valid ? 1.0f - spec_gloss.glossinessFactor
                            : kFallbackRoughness);
      return;
    }

    Texturator::Args spec_args = GetDefaultTextureArgs();
    spec_args.usage = Texturator::kUsageSpecToMetal;
    spec_args.scale.Assign(spec_gloss.specularFactor, 1.0f);
    Texturator::Args diff_args = GetDefaultTextureArgs();
    diff_args.usage = Texturator::kUsageDiffToBase;
    diff_args.alpha_mode = material.alphaMode;
    diff_args.alpha_cutoff = material.alphaCutoff;
    diff_args.scale.Assign(spec_gloss.diffuseFactor);
    const std::string* metal_name = nullptr;
    const std::string* base_name = nullptr;
    const Gltf::Id spec_image_id =
        spec_gloss_texture ? spec_gloss_texture->source : Gltf::Id::kNull;
    const Gltf::Id diff_image_id =
        diff_texture ? diff_texture->source : Gltf::Id::kNull;
    texturator_.AddSpecToMetal(spec_image_id, spec_args, diff_image_id,
                               diff_args, &metal_name, &base_name);

    const Gltf::Material::Texture& base_input =
        diff_texture ? spec_gloss.diffuseTexture : input;
    AttachBaseTextureInput(
        base_name, base_args, base_input, material_id, material_path,
        kTokInputDiffuseColor, uvsets, pbr_shader);

    const Gltf::Id metal_sampler_id = spec_gloss_texture
                                          ? spec_gloss_texture->sampler
                                          : diff_texture->sampler;
    const Gltf::Material::Texture& metal_input =
        spec_gloss_texture ? input : spec_gloss.diffuseTexture;
    AttachTextureInputTo(*metal_name, metal_sampler_id, metal_input,
                         material_path, "tex_metallic", kTokInputMetallic,
                         SdfValueTypeNames->Float, material.pbr.metallicFactor,
                         kFallbackMetallic, kTokR,
                         SdfValueTypeNames->Float, false, uvsets, pbr_shader);

    // Set roughness to (1 - glossiness).
    if (spec_gloss_texture) {
      Texturator::Args rough_args = GetDefaultTextureArgs();
      rough_args.usage = Texturator::kUsageGlossToRough;
      rough_args.scale = spec_gloss.glossinessFactor;
      AttachTextureInput(
          rough_args, input, material_id, material_path,
          "tex_roughness", kTokInputRoughness, SdfValueTypeNames->Float,
          kFallbackRoughness, kTokR, SdfValueTypeNames->Float, false,
          uvsets, pbr_shader);
    } else {
      pbr_shader->CreateInput(kTokInputRoughness, SdfValueTypeNames->Float)
          .Set(1.0f - spec_gloss.glossinessFactor);
    }
  } else {
    if (cc_->settings.warn_ios_incompat) {
      const std::string src_material_name =
          Gltf::GetName(cc_->gltf->materials, material_id, "material");
      LogOnce<UFG_WARN_SPECULAR_WORKFLOW_UNSUPPORTED>(
          &cc_->once_logger, "Material(s): ", src_material_name.c_str());
    }
    pbr_shader->CreateInput(kTokInputUseSpecular, SdfValueTypeNames->Int)
        .Set(1);

    AttachBaseTextureInput(
        base_args, spec_gloss.diffuseTexture, material_id, material_path,
        kTokInputDiffuseColor, uvsets, pbr_shader);

    if (spec_gloss_texture) {
      Texturator::Args spec_args = GetDefaultTextureArgs();
      spec_args.usage = Texturator::kUsageSpec;
      spec_args.scale.Assign(spec_gloss.specularFactor, 1.0f);
      AttachTextureInput(
          spec_args, input, material_id, material_path,
          "tex_specular", kTokInputSpecularColor, SdfValueTypeNames->Float3,
          kFallbackSpecular, kTokRgb, SdfValueTypeNames->Float3, false,
          uvsets, pbr_shader);

      Texturator::Args gloss_args = GetDefaultTextureArgs();
      gloss_args.usage = Texturator::kUsageGloss;
      gloss_args.scale = spec_gloss.glossinessFactor;
      AttachTextureInput(
          gloss_args, input, material_id, material_path,
          "tex_glossiness", kTokInputGlossiness, SdfValueTypeNames->Float,
          kFallbackRoughness, kTokR, SdfValueTypeNames->Float, false,
          uvsets, pbr_shader);
    } else {
      pbr_shader->CreateInput(kTokInputSpecularColor, SdfValueTypeNames->Float3)
          .Set(GfVec3f(spec_gloss.specularFactor));
      pbr_shader->CreateInput(kTokInputGlossiness, SdfValueTypeNames->Float)
          .Set(spec_gloss.glossinessFactor);
    }
  }
}
}  // namespace ufg
