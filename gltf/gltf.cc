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

#include "gltf.h"  // NOLINT: Silence relative path warning.

#include "internal_util.h"  // NOLINT: Silence relative path warning.

const Gltf::Accessor::Value Gltf::Accessor::kValueZero = {};

const Gltf::Camera::Orthographic Gltf::Camera::Orthographic::kDefault = {};
const Gltf::Camera::Perspective Gltf::Camera::Perspective::kDefault = {};

const Gltf::Mesh::Attribute Gltf::Mesh::kAttributePosition(
    Gltf::Mesh::kSemanticPosition);
const Gltf::Mesh::Attribute Gltf::Mesh::kAttributeNormal(
    Gltf::Mesh::kSemanticNormal);
const Gltf::Mesh::Attribute Gltf::Mesh::kAttributeTangent(
    Gltf::Mesh::kSemanticTangent);
const Gltf::Mesh::Attribute Gltf::Mesh::kAttributeTexcoord0(
    Gltf::Mesh::kSemanticTexcoord);
const Gltf::Mesh::Attribute Gltf::Mesh::kAttributeColor0(
    Gltf::Mesh::kSemanticColor);
const Gltf::Mesh::Attribute Gltf::Mesh::kAttributeJoints0(
    Gltf::Mesh::kSemanticJoints);
const Gltf::Mesh::Attribute Gltf::Mesh::kAttributeWeights0(
    Gltf::Mesh::kSemanticWeights);

void Gltf::Asset::Clear() {
  copyright.clear();
  generator.clear();
  version.clear();
  minVersion.clear();
}

void Gltf::Asset::Swap(Asset* other) {
  other->copyright.swap(copyright);
  other->generator.swap(generator);
  other->version.swap(version);
  other->minVersion.swap(minVersion);
}

bool Gltf::Asset::IsSupportedVersion() const {
  // Support glTF 2 and minor revisions.
  return version == "2" ||
         StringBeginsWithCI(version.c_str(), version.length(), "2.");
}

static const Gltf::Material::Texture** AppendTexture(
    const Gltf::Material::Texture** it,
    const Gltf::Material::Texture* texture) {
  if (texture->index != Gltf::Id::kNull) {
    *it++ = texture;
  }
  return it;
}

size_t Gltf::Material::GetTextures(
    const Texture* (&out_textures)[kTextureMax]) const {
  const Texture** end = out_textures;
  end = AppendTexture(end, &pbr.baseColorTexture);
  end = AppendTexture(end, &pbr.metallicRoughnessTexture);
  if (pbr.specGloss) {
    end = AppendTexture(end, &pbr.specGloss->diffuseTexture);
    end = AppendTexture(end, &pbr.specGloss->specularGlossinessTexture);
  }
  end = AppendTexture(end, &normalTexture);
  end = AppendTexture(end, &occlusionTexture);
  end = AppendTexture(end, &emissiveTexture);
  return end - out_textures;
}

std::string Gltf::Mesh::Attribute::ToString() const {
  const char* prefix;
  bool has_numeric_suffix;
  if (static_cast<size_t>(semantic) < kSemanticCount) {
    const SemanticInfo& info = kSemanticInfos[semantic];
    prefix = info.prefix;
    has_numeric_suffix = info.has_numeric_suffix;
  } else {
    prefix = "(invalid)";
    has_numeric_suffix = false;
  }
  if (number != 0 || has_numeric_suffix) {
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%s%u", prefix, number);
    return buffer;
  } else {
    return prefix;
  }
}

void Gltf::Clear() {
  asset.Clear();
  scene = Id::kNull;
  extensionsUsed.clear();
  extensionsRequired.clear();
  accessors.clear();
  animations.clear();
  buffers.clear();
  bufferViews.clear();
  cameras.clear();
  images.clear();
  materials.clear();
  meshes.clear();
  nodes.clear();
  samplers.clear();
  scenes.clear();
  skins.clear();
  textures.clear();
}

void Gltf::Swap(Gltf* other) {
  other->asset.Swap(&asset);
  std::swap(other->scene, scene);
  other->extensionsUsed.swap(extensionsUsed);
  other->extensionsRequired.swap(extensionsRequired);
  other->accessors.swap(accessors);
  other->animations.swap(animations);
  other->buffers.swap(buffers);
  other->bufferViews.swap(bufferViews);
  other->cameras.swap(cameras);
  other->images.swap(images);
  other->materials.swap(materials);
  other->meshes.swap(meshes);
  other->nodes.swap(nodes);
  other->samplers.swap(samplers);
  other->scenes.swap(scenes);
  other->skins.swap(skins);
  other->textures.swap(textures);
}

const char* const Gltf::kExtensionIdNames[kExtensionCount] = {
    nullptr,                                // kExtensionUnknown
    "KHR_materials_unlit",                  // kExtensionUnlit
    "KHR_materials_pbrSpecularGlossiness",  // kExtensionSpecGloss
    "KHR_texture_transform",                // kExtensionTextureTransform
    "KHR_draco_mesh_compression",           // kExtensionDraco
};

const uint16_t Gltf::kAccessorComponentTypeValues[Accessor::kComponentCount] = {
    0,     // kComponentUnset
    5120,  // kComponentByte
    5121,  // kComponentUnsignedByte
    5122,  // kComponentShort
    5123,  // kComponentUnsignedShort
    5125,  // kComponentUnsignedInt
    5126,  // kComponentFloat
};
const char* const Gltf::kAccessorComponentTypeNames[
  Accessor::kComponentCount] = {
    nullptr,           // kComponentUnset
    "BYTE",            // kComponentByte
    "UNSIGNED_BYTE",   // kComponentUnsignedByte
    "SHORT",           // kComponentShort
    "UNSIGNED_SHORT",  // kComponentUnsignedShort
    "UNSIGNED_INT",    // kComponentUnsignedInt
    "FLOAT",           // kComponentFloat
};
const Gltf::ComponentFormat Gltf::kAccessorComponentTypeFormats[
  Accessor::kComponentCount] = {
    Gltf::kComponentFormatCount,        // kComponentUnset
    Gltf::kComponentFormatSignedInt,    // kComponentByte
    Gltf::kComponentFormatUnsignedInt,  // kComponentUnsignedByte
    Gltf::kComponentFormatSignedInt,    // kComponentShort
    Gltf::kComponentFormatUnsignedInt,  // kComponentUnsignedShort
    Gltf::kComponentFormatUnsignedInt,  // kComponentUnsignedInt
    Gltf::kComponentFormatFloat,        // kComponentFloat
};
const uint8_t Gltf::kAccessorComponentTypeSizes[Accessor::kComponentCount] = {
    0,  // kComponentUnset
    1,  // kComponentByte
    1,  // kComponentUnsignedByte
    2,  // kComponentShort
    2,  // kComponentUnsignedShort
    4,  // kComponentUnsignedInt
    4,  // kComponentFloat
};

const char* const Gltf::kAccessorTypeNames[Accessor::kTypeCount] = {
    nullptr,   // kTypeUnset,
    "SCALAR",  // kTypeScalar
    "VEC2",    // kTypeVec2
    "VEC3",    // kTypeVec3
    "VEC4",    // kTypeVec4
    "MAT2",    // kTypeMat2
    "MAT3",    // kTypeMat3
    "MAT4",    // kTypeMat4
};
const uint8_t Gltf::kAccessorTypeComponentCounts[Gltf::Accessor::kTypeCount] = {
    0,      // kTypeUnset
    1,      // kTypeScalar
    2,      // kTypeVec2
    3,      // kTypeVec3
    4,      // kTypeVec4
    2 * 2,  // kTypeMat2
    3 * 3,  // kTypeMat3
    4 * 4,  // kTypeMat4
};

const char* const Gltf::kAnimationChannelTargetPathNames[
  Animation::Channel::Target::kPathCount] = {
    nullptr,        // kPathUnset
    "translation",  // kPathTranslation
    "rotation",     // kPathRotation
    "scale",        // kPathScale
    "weights",      // kPathWeights
};

const char* const Gltf::kAnimationSamplerInterpolationNames[
  Animation::Sampler::kInterpolationCount] = {
    "LINEAR",       // kInterpolationLinear
    "STEP",         // kInterpolationStep
    "CUBICSPLINE",  // kInterpolationCubicSpline
};

const uint16_t Gltf::kBufferViewTargetValues[BufferView::kTargetCount] = {
    0,      // kTargetUnset
    34962,  // kTargetArrayBuffer
    34963,  // kTargetElementArrayBuffer
};
const char* const Gltf::kBufferViewTargetNames[BufferView::kTargetCount] = {
    nullptr,                 // kTargetUnset
    "ARRAY_BUFFER",          // kTargetArrayBuffer
    "ELEMENT_ARRAY_BUFFER",  // kTargetElementArrayBuffer
};

const char* const Gltf::kCameraTypeNames[Camera::kTypeCount] = {
    "perspective",   // kTypePerspective
    "orthographic",  // kTypeOrthographic
};

const char* const Gltf::kImageMimeTypeNames[Image::kMimeCount] = {
    nullptr,       // kMimeUnset
    "image/jpeg",  // kMimeJpeg
    "image/png",   // kMimePng
    "image/bmp",   // kMimeBmp
    "image/gif",   // kMimeGif
    nullptr,       // kMimeOther
};
const char* const Gltf::kImageMimeTypeExtensions[Image::kMimeCount] {
    nullptr,  // kMimeUnset
    ".jpg",   // kMimeJpeg
    ".png",   // kMimePng
    ".bmp",   // kMimeBmp
    ".gif",   // kMimeGif
    nullptr,  // kMimeOther
};

Gltf::Image::MimeType Gltf::FindImageMimeTypeByExtension(const char* ext) {
  for (size_t mime_type = 0; mime_type != Image::kMimeCount; ++mime_type) {
    const char* const mime_ext = kImageMimeTypeExtensions[mime_type];
    if (mime_ext && StringEqualCI(ext, mime_ext)) {
      return static_cast<Image::MimeType>(mime_type);
    }
  }
  if (StringEqualCI(ext, ".jpeg")) {
    return Image::kMimeJpeg;
  }
  return Image::kMimeUnset;
}

Gltf::Image::MimeType Gltf::FindImageMimeTypeByPath(const std::string& path) {
  const size_t ext_pos = path.rfind('.');
  const char* const ext =
      ext_pos == std::string::npos ? nullptr : &path[ext_pos];
  return ext ? FindImageMimeTypeByExtension(ext) : Image::kMimeUnset;
}

const char* const Gltf::kMaterialAlphaModeNames[Material::kAlphaModeCount] = {
    "OPAQUE",  // kAlphaModeOpaque
    "MASK",    // kAlphaModeMask
    "BLEND",   // kAlphaModeBlend
};

const Gltf::SemanticInfo Gltf::kSemanticInfos[Gltf::Mesh::kSemanticCount] = {
    // { prefix, prefix_len, suffix, component_min, component_max}
    {"POSITION" , CONST_STRLEN("POSITION" ), false, 3, 3},  // kSemanticPosition
    {"NORMAL"   , CONST_STRLEN("NORMAL"   ), false, 3, 3},  // kSemanticNormal
    {"TANGENT"  , CONST_STRLEN("TANGENT"  ), false, 3, 4},  // kSemanticTangent
    {"TEXCOORD_", CONST_STRLEN("TEXCOORD_"), true , 1, 4},  // kSemanticTexcoord
    {"COLOR_"   , CONST_STRLEN("COLOR_"   ), true , 3, 4},  // kSemanticColor
    {"JOINTS_"  , CONST_STRLEN("JOINTS_"  ), true , 1, 4},  // kSemanticJoints
    {"WEIGHTS_" , CONST_STRLEN("WEIGHTS_" ), true , 1, 4},  // kSemanticWeights
};

const uint8_t Gltf::kMeshPrimitiveModeValues[Mesh::Primitive::kModeCount] = {
    0,  // kModePoints
    1,  // kModeLines
    2,  // kModeLineLoop
    3,  // kModeLineStrip
    4,  // kModeTriangles
    5,  // kModeTriangleStrip
    6,  // kModeTriangleFan
};
const char* const Gltf::kMeshPrimitiveModeNames[Mesh::Primitive::kModeCount] = {
    "POINTS",          // kModePoints
    "LINES",           // kModeLines
    "LINE_LOOP",       // kModeLineLoop
    "LINE_STRIP",      // kModeLineStrip
    "TRIANGLES",       // kModeTriangles
    "TRIANGLE_STRIP",  // kModeTriangleStrip
    "TRIANGLE_FAN",    // kModeTriangleFan
};

bool Gltf::HasTriangles(Mesh::Primitive::Mode primitive) {
  switch (primitive) {
    case Mesh::Primitive::kModeTriangles:
    case Mesh::Primitive::kModeTriangleStrip:
    case Mesh::Primitive::kModeTriangleFan:
      return true;
    default:
      return false;
  }
}

const uint16_t Gltf::kSamplerMagFilterValues[Sampler::kMagFilterCount] = {
    0,     // kMagFilterUnset
    9728,  // kMagFilterNearest
    9729,  // kMagFilterLinear
};
const char* const Gltf::kSamplerMagFilterNames[Sampler::kMagFilterCount] = {
    nullptr,    // kMagFilterUnset
    "NEAREST",  // kMagFilterNearest
    "LINEAR",   // kMagFilterLinear
};

const uint16_t Gltf::kSamplerMinFilterValues[Sampler::kMinFilterCount] = {
    0,     // kMinFilterUnset
    9728,  // kMinFilterNearest
    9729,  // kMinFilterLinear
    9984,  // kMinFilterNearestMipmapNearest
    9985,  // kMinFilterLinearMipmapNearest
    9986,  // kMinFilterNearestMipmapLinear
    9987,  // kMinFilterLinearMipmapLinear
};
const char* const Gltf::kSamplerMinFilterNames[Sampler::kMinFilterCount] = {
    nullptr,                   // kMinFilterUnset
    "NEAREST",                 // kMinFilterNearest
    "LINEAR",                  // kMinFilterLinear
    "NEAREST_MIPMAP_NEAREST",  // kMinFilterNearestMipmapNearest
    "LINEAR_MIPMAP_NEAREST",   // kMinFilterLinearMipmapNearest
    "NEAREST_MIPMAP_LINEAR",   // kMinFilterNearestMipmapLinear
    "LINEAR_MIPMAP_LINEAR",    // kMinFilterLinearMipmapLinear
};

const uint16_t Gltf::kSamplerWrapModeValues[Sampler::kWrapCount] = {
    0,      // kWrapUnset
    33071,  // kWrapClamp
    33648,  // kWrapMirror
    10497,  // kWrapRepeat
};
const char* const Gltf::kSamplerWrapModeNames[Sampler::kWrapCount] = {
    nullptr,            // kWrapUnset
    "CLAMP",            // kWrapClamp
    "MIRRORED_REPEAT",  // kWrapMirror
    "REPEAT",           // kWrapRepeat
};

Gltf::Image::MimeType Gltf::FindImageMimeTypeByUri(const Uri& uri) {
  return uri.data_type == Uri::kDataTypeNone
             ? FindImageMimeTypeByPath(uri.path)
             : GetUriDataImageMimeType(uri.data_type);
}

const Gltf::Image::MimeType Gltf::kUriDataImageMimeTypes[
  Gltf::Uri::kDataTypeCount] {
    Gltf::Image::kMimeUnset,  // kDataTypeNone
    Gltf::Image::kMimeUnset,  // kDataTypeBin
    Gltf::Image::kMimeJpeg ,  // kDataTypeImageJpeg
    Gltf::Image::kMimePng  ,  // kDataTypeImagePng
    Gltf::Image::kMimeBmp  ,  // kDataTypeImageBmp
    Gltf::Image::kMimeGif  ,  // kDataTypeImageGif
    Gltf::Image::kMimeOther,  // kDataTypeImageOther
    Gltf::Image::kMimeUnset,  // kDataTypeUnknown
};

const char* const kUriDataTypeNames[]{
    nullptr,                     // kDataTypeNone
    "application/octet-stream",  // kDataTypeBin
    "image/jpeg",                // kDataTypeImageJpeg
    "image/png",                 // kDataTypeImagePng
    "image/bmp",                 // kDataTypeImageBmp
    "image/gif",                 // kDataTypeImageGif
    nullptr,                     // kDataTypeImageOther
    nullptr,                     // kDataTypeUnknown
};
static_assert(arraysize(kUriDataTypeNames) == Gltf::Uri::kDataTypeCount, "");

Gltf::Uri::DataType Gltf::FindUriDataType(const char* name, size_t name_len) {
  for (size_t type = 0; type != arraysize(kUriDataTypeNames); ++type) {
    const char* const entry = kUriDataTypeNames[type];
    if (entry && StringEqualCI(name, name_len, entry, strlen(entry))) {
      return static_cast<Gltf::Uri::DataType>(type);
    }
  }
  if (StringBeginsWith(name, name_len, "image/")) {
    return Gltf::Uri::kDataTypeImageOther;
  }
  return Gltf::Uri::kDataTypeUnknown;
}

#define GLTF_COMPARE(a, b)          \
  do {                              \
    const int diff = Compare(a, b); \
    if (diff) {                     \
      return diff;                  \
    }                               \
  } while (0)

int Gltf::Compare(const Material::Texture::Transform& a,
                  const Material::Texture::Transform& b) {
  GLTF_COMPARE(a.offset, b.offset);
  GLTF_COMPARE(a.rotation, b.rotation);
  GLTF_COMPARE(a.scale, b.scale);
  return 0;
}

int Gltf::Compare(const Material::Texture& a, const Material::Texture& b) {
  GLTF_COMPARE(a.index, b.index);
  GLTF_COMPARE(a.texCoord, b.texCoord);
  GLTF_COMPARE(a.transform, b.transform);
  return 0;
}

int Gltf::Compare(const Material::Pbr::SpecGloss& a,
                  const Material::Pbr::SpecGloss& b) {
  GLTF_COMPARE(a.diffuseFactor, b.diffuseFactor);
  GLTF_COMPARE(a.diffuseTexture, b.diffuseTexture);
  GLTF_COMPARE(a.specularFactor, b.specularFactor);
  GLTF_COMPARE(a.glossinessFactor, b.glossinessFactor);
  GLTF_COMPARE(a.specularGlossinessTexture, b.specularGlossinessTexture);
  return 0;
}

int Gltf::Compare(const Material::Pbr& a, const Material::Pbr& b) {
  GLTF_COMPARE(a.baseColorFactor, b.baseColorFactor);
  GLTF_COMPARE(a.baseColorTexture, b.baseColorTexture);
  GLTF_COMPARE(a.metallicFactor, b.metallicFactor);
  GLTF_COMPARE(a.roughnessFactor, b.roughnessFactor);
  GLTF_COMPARE(a.metallicRoughnessTexture, b.metallicRoughnessTexture);
  GLTF_COMPARE(a.specGloss, b.specGloss);
  return 0;
}

int Gltf::Compare(const Material::NormalTexture& a,
                  const Material::NormalTexture& b) {
  GLTF_COMPARE(static_cast<const Material::Texture&>(a),
               static_cast<const Material::Texture&>(b));
  GLTF_COMPARE(a.scale, b.scale);
  return 0;
}

int Gltf::Compare(const Material::OcclusionTexture& a,
                  const Material::OcclusionTexture& b) {
  GLTF_COMPARE(static_cast<const Material::Texture&>(a),
               static_cast<const Material::Texture&>(b));
  GLTF_COMPARE(a.strength, b.strength);
  return 0;
}

int Gltf::Compare(const Material& a, const Material& b) {
  GLTF_COMPARE(a.name, b.name);
  GLTF_COMPARE(a.pbr, b.pbr);
  GLTF_COMPARE(a.normalTexture, b.normalTexture);
  GLTF_COMPARE(a.occlusionTexture, b.occlusionTexture);
  GLTF_COMPARE(a.emissiveTexture, b.emissiveTexture);
  GLTF_COMPARE(a.emissiveFactor, b.emissiveFactor);
  GLTF_COMPARE(a.alphaMode, b.alphaMode);
  GLTF_COMPARE(a.alphaCutoff, b.alphaCutoff);
  GLTF_COMPARE(a.doubleSided, b.doubleSided);
  GLTF_COMPARE(a.unlit, b.unlit);
  return 0;
}

bool Gltf::SanitizePath(char* path) {
  static constexpr char kReservedChars[] = "<>:\"|?*";
  bool changed = false;
  for (char* s = path; *s; ++s) {
    if (strchr(kReservedChars, *s)) {
      *s = '_';
      changed = true;
    }
  }
  return changed;
}

std::string Gltf::GetSanitizedPath(const char* path) {
  std::string sane_path = path;
  SanitizePath(&sane_path[0]);
  return sane_path;
}

std::vector<const char*> Gltf::GetReferencedUriPaths() const {
  // Gather paths.
  const size_t path_max = buffers.size() + images.size();
  std::vector<const char*> paths(path_max);
  const char** const path_begin = paths.data();
  const char** path_end = path_begin;
  for (const Buffer& buffer : buffers) {
    if (!buffer.uri.path.empty()) {
      *path_end++ = buffer.uri.path.c_str();
    }
  }
  for (const Image& image : images) {
    if (!image.uri.path.empty()) {
      *path_end++ = image.uri.path.c_str();
    }
  }

  // Sort then remove duplicates.
  std::sort(path_begin, path_end,
            [](const char* a, const char* b) { return strcmp(a, b) < 0; });
  path_end = std::unique(
      path_begin, path_end,
      [](const char* a, const char* b) { return strcmp(a, b) == 0; });
  paths.resize(path_end - path_begin);
  return paths;
}

const std::vector<Gltf::ExtensionId> Gltf::GetReferencedExtensions() const {
  // Flag referenced extensions.
  bool referenced[kExtensionCount] = {};
  for (const Material& material : materials) {
    if (material.unlit) {
      referenced[kExtensionUnlit] = true;
    }
    if (material.pbr.specGloss) {
      referenced[kExtensionSpecGloss] = true;
    }

    const Material::Texture* textures[Material::kTextureMax];
    const size_t texture_count = material.GetTextures(textures);
    for (size_t texture_i = 0; texture_i != texture_count; ++texture_i) {
      const Material::Texture& texture = *textures[texture_i];
      if (!texture.transform.IsIdentity()) {
        referenced[kExtensionTextureTransform] = true;
      }
    }
  }
  for (const Mesh& mesh : meshes) {
    for (const Mesh::Primitive& prim : mesh.primitives) {
      if (prim.draco.bufferView != Id::kNull) {
        referenced[kExtensionDraco] = true;
      }
    }
  }

  // Return list of referenced extensions.
  std::vector<Gltf::ExtensionId> ref_extensions;
  for (size_t extension = 0; extension != arraysize(referenced); ++extension) {
    if (referenced[extension]) {
      ref_extensions.push_back(static_cast<Gltf::ExtensionId>(extension));
    }
  }
  return ref_extensions;
}
