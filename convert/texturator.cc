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

#include "convert/texturator.h"

#include "gltf/disk_util.h"
#include "process/float_image.h"
#include "process/math.h"

namespace ufg {
namespace {
// Use premultiplied alpha when resizing so colors are weighted by opacity.
constexpr bool kResizePremulAlpha = true;

constexpr ColorChannel kChannelOcclusion = kColorChannelR;
constexpr ColorChannel kChannelMetallic = kColorChannelB;
constexpr ColorChannel kChannelRoughness = kColorChannelG;
constexpr ColorChannel kChannelGlossiness = kColorChannelA;

constexpr uint32_t kQuantizeBits = 10;
constexpr uint32_t kQuantizeUnits = 1 << kQuantizeBits;

const ColorI kQuantizeScaleIdentity =
    {kQuantizeUnits, kQuantizeUnits, kQuantizeUnits, kQuantizeUnits};
const ColorI kQuantizeBiasIdentity = {0, 0, 0, 0};

enum PassId {
  kPassIdRemoveAlpha,
  kPassIdAddAlpha,
  kPassIdColorSpace,
  kPassIdNormalizeNormals,
  kPassIdAlphaCutoff,
  kPassIdScaleBias,
  kPassIdSpecToMetal,
  kPassIdResize,
};

constexpr uint32_t kPassFlagRemoveAlpha = 1 << kPassIdRemoveAlpha;
constexpr uint32_t kPassFlagAddAlpha = 1 << kPassIdAddAlpha;
constexpr uint32_t kPassFlagColorSpace = 1 << kPassIdColorSpace;
constexpr uint32_t kPassFlagNormalizeNormals = 1 << kPassIdNormalizeNormals;
constexpr uint32_t kPassFlagAlphaCutoff = 1 << kPassIdAlphaCutoff;
constexpr uint32_t kPassFlagScaleBias = 1 << kPassIdScaleBias;
constexpr uint32_t kPassFlagSpecToMetal = 1 << kPassIdSpecToMetal;
constexpr uint32_t kPassFlagResize = 1 << kPassIdResize;

constexpr uint32_t kPassMaskFloat = kPassFlagColorSpace | kPassFlagScaleBias |
                                    kPassFlagSpecToMetal | kPassFlagResize;

using RelevanceMask = uint8_t;
constexpr RelevanceMask kRelevanceR = 1 << kColorChannelR;
constexpr RelevanceMask kRelevanceG = 1 << kColorChannelG;
constexpr RelevanceMask kRelevanceB = 1 << kColorChannelB;
constexpr RelevanceMask kRelevanceA = 1 << kColorChannelA;
constexpr RelevanceMask kRelevanceRGB = kRelevanceR | kRelevanceG | kRelevanceB;
constexpr RelevanceMask kRelevanceRGBA = kRelevanceRGB | kRelevanceA;

struct UsageInfo {
  // Destination image name suffix.
  const char* dst_suffix;
  // Max number of color components in the destination image.
  uint8_t dst_component_max;
  // Indicates which channels in the source image are relevant to the output.
  RelevanceMask relevance_mask;
  // Color space for source and destination RGB components. A is always linear.
  ColorSpace src_rgb_color_space;
  ColorSpace dst_rgb_color_space;
};

#define TEXUSG(suffix, comps, rel, src, dst) \
  {suffix, comps, kRelevance##rel, kColorSpace##src, kColorSpace##dst}
const UsageInfo kUsageInfos[] = {
    TEXUSG(""        , 4, RGBA, Srgb  , Srgb  ),  // kUsageDefault
    TEXUSG("_lin"    , 4, RGBA, Srgb  , Linear),  // kUsageLinear
    TEXUSG("_base"   , 4, RGBA, Srgb  , Srgb  ),  // kUsageDiffToBase
    TEXUSG(""        , 3, RGB , Linear, Linear),  // kUsageNorm
    TEXUSG("_occl"   , 1, R   , Linear, Linear),  // kUsageOccl
    TEXUSG("_metal"  , 1, B   , Linear, Linear),  // kUsageMetal
    TEXUSG("_rough"  , 1, G   , Linear, Linear),  // kUsageRough
    TEXUSG("_spec"   , 3, RGB , Srgb  , Srgb  ),  // kUsageSpec
    TEXUSG("_metal"  , 3, RGB , Srgb  , Linear),  // kUsageSpecToMetal
    TEXUSG("_gloss"  , 1, A   , Linear, Linear),  // kUsageGloss
    TEXUSG("_rough"  , 1, A   , Linear, Linear),  // kUsageGlossToRough
    TEXUSG("_unlit_a", 4, A   , Srgb  , Srgb  ),  // kUsageUnlitA
};
#undef TEXUSG
static_assert(UFG_ARRAY_SIZE(kUsageInfos) == Texturator::kUsageCount, "");

struct FallbackInfo {
  const char* name;
  bool r_only;
  Image::Component color[3];
};

const FallbackInfo kFallbackInfos[] = {
    {"fallback_black.png"  , false, {  0,   0,   0}},  // kFallbackBlack
    {"fallback_magenta.png", false, {255,   0, 255}},  // kFallbackMagenta
    {"fallback_r0.png"     , true , {  0,   0,   0}},  // kFallbackR0
    {"fallback_r1.png"     , true , {255, 255, 255}},  // kFallbackR1
};
static_assert(UFG_ARRAY_SIZE(kFallbackInfos) == Texturator::kFallbackCount, "");

void SetImageExtension(Gltf::Image::MimeType mime_type, std::string* path) {
  if (mime_type == Gltf::Image::kMimeUnset) {
    return;
  }
  const char* const ext = Gltf::GetMimeTypeExtension(mime_type);
  const Gltf::Image::MimeType old_mime_type =
      Gltf::FindImageMimeTypeByPath(*path);
  if (old_mime_type != Gltf::Image::kMimeUnset) {
    // Remove the existing image type extension. This has two effects:
    // 1) It allows us to change the destination image type.
    // 2) It canonicalizes the type extension. This is important for some apps
    //    (such as Usdview) that recognize ".jpg" but not ".jpeg".
    const size_t old_ext_pos = path->rfind('.');
    UFG_ASSERT_LOGIC(old_ext_pos != std::string::npos);
    path->resize(old_ext_pos);
  }
  *path += ext;
}

const int Quantize(float value) {
  return static_cast<int>(value * kQuantizeUnits + 0.5f);
}

ColorI Quantize(const ColorF& f) {
  ColorI q;
  for (size_t i = 0; i != UFG_ARRAY_SIZE(f.c); ++i) {
    q.c[i] = Quantize(f.c[i]);
  }
  return q;
}

std::unique_ptr<Image> CopyImageRgb(const Image& src) {
  std::unique_ptr<Image> dst(new Image());
  dst->CreateFromRgb(src);
  return dst;
}

std::unique_ptr<Image> CopyImageRgba(const Image& src) {
  std::unique_ptr<Image> dst(new Image());
  dst->CreateFromRgba(src, Image::kComponentMax);
  return dst;
}

std::unique_ptr<Image> CopyImageChannel(const Image& src, ColorChannel channel,
                                        const Image::Transform& transform) {
  std::unique_ptr<Image> dst(new Image());
  dst->CreateFromChannel(src, channel, transform);
  return dst;
}

std::unique_ptr<Image> CopyImageMasked(
    const Image& src, const Image::Component (&keep_mask)[kColorChannelCount],
    const Image::Component (&replace_value)[kColorChannelCount]) {
  std::unique_ptr<Image> dst(new Image());
  dst->CreateFromMasked(src, keep_mask, replace_value);
  return dst;
}

std::unique_ptr<Image> CopyImageByUsage(
    const Image& src, Texturator::Usage usage, uint32_t pass_mask) {
  switch (usage) {
    case Texturator::kUsageDiffToBase:
      return CopyImageRgb(src);
    case Texturator::kUsageOccl:
      return CopyImageChannel(src, kChannelOcclusion, Image::Transform::kNone);
    case Texturator::kUsageMetal:
      return CopyImageChannel(src, kChannelMetallic, Image::Transform::kNone);
    case Texturator::kUsageRough:
      return CopyImageChannel(src, kChannelRoughness, Image::Transform::kNone);
    case Texturator::kUsageSpec:
      return CopyImageRgb(src);
    case Texturator::kUsageSpecToMetal:
      return CopyImageRgb(src);
    case Texturator::kUsageGloss:
      return CopyImageChannel(src, kChannelGlossiness, Image::Transform::kNone);
    case Texturator::kUsageGlossToRough:
      return CopyImageChannel(src, kChannelGlossiness, Image::Transform::kNone);
    case Texturator::kUsageUnlitA:
      return CopyImageMasked(src, {0, 0, 0, 0xff}, {0, 0, 0, 0});
    case Texturator::kUsageDefault:
    case Texturator::kUsageNorm:
    case Texturator::kUsageLinear:
    default:
      if (pass_mask & kPassFlagRemoveAlpha) {
        return CopyImageRgb(src);
      } else if (pass_mask & kPassFlagAddAlpha) {
        return CopyImageRgba(src);
      } else {
        return std::unique_ptr<Image>(new Image(src));
      }
      break;
  }
}

std::unique_ptr<Image> WhiteImageByUsage(const Image& src,
                                         Texturator::Usage usage) {
  const UsageInfo& usage_info = kUsageInfos[usage];
  static const Image::Component kWhite[] = {
      Image::kComponentMax, Image::kComponentMax,
      Image::kComponentMax, Image::kComponentMax};
  std::unique_ptr<Image> image(new Image());
  const size_t channel_count =
      std::min(src.GetChannelCount(),
               static_cast<uint32_t>(usage_info.dst_component_max));
  image->CreateWxH(src.GetWidth(), src.GetHeight(), kWhite, channel_count);
  return image;
}

std::string GetSrcName(const Gltf& gltf, Gltf::Id image_id,
                       Gltf::Image::MimeType* out_mime_type) {
  const Gltf::Image* const gltf_image = Gltf::GetById(gltf.images, image_id);
  if (gltf_image) {
    const bool is_buffer = gltf_image->bufferView != Gltf::Id::kNull;
    const bool is_embedded =
        gltf_image->uri.data_type != Gltf::Uri::kDataTypeNone;
    if (is_buffer || is_embedded) {
      const Gltf::Image::MimeType mime_type =
          is_embedded ? Gltf::GetUriDataImageMimeType(gltf_image->uri.data_type)
                      : gltf_image->mimeType;
      const char* const ext = mime_type == Gltf::Image::kMimeOther
                                  ? ""
                                  : Gltf::GetMimeTypeExtension(mime_type);
      if (ext) {
        *out_mime_type = mime_type;
        return AppendNumber("bin/image", Gltf::IdToIndex(image_id)) + ext;
      }
    } else {
      *out_mime_type = Gltf::FindImageMimeTypeByPath(gltf_image->uri.path);
      return Gltf::GetSanitizedPath(gltf_image->uri.path.c_str());
    }
  }
  *out_mime_type = Gltf::Image::kMimeUnset;
  return std::string();
}

void ApplyFloatPasses(const Texturator::Args& args, uint32_t pass_mask,
                      size_t width, size_t height, FloatImage* image) {
  if (pass_mask & kPassFlagScaleBias) {
    if (args.usage == Texturator::kUsageNorm) {
      image->ScaleBiasNormals(args.scale, args.bias);
    } else {
      image->ScaleBias(args.scale, args.bias);
    }
  }
  if (pass_mask & kPassFlagResize) {
    image->Resize(width, height, kResizePremulAlpha);
  }
}

void GetDstSize(uint32_t src_width, uint32_t src_height,
                const ImageResizeSettings& resize, float global_scale,
                uint32_t* out_width, uint32_t* out_height) {
  UFG_ASSERT_LOGIC(resize.size_min <= resize.size_max);
  UFG_ASSERT_LOGIC(resize.size_min > 0);
  const float scale = resize.scale * global_scale;
  uint32_t width = std::max(1, static_cast<int>(src_width * scale + 0.5f));
  uint32_t height = std::max(1, static_cast<int>(src_height * scale + 0.5f));

  // TODO: These operations can change the aspect ratio, which we may
  // want to prevent.
  if (resize.force_power_of_2) {
    width = Power2Floor(width);
    height = Power2Floor(height);
  }
  width = Clamp(width, resize.size_min, resize.size_max);
  height = Clamp(height, resize.size_min, resize.size_max);

  *out_width = width;
  *out_height = height;
}

size_t EstimateDecompressedImageSize(const Image& image,
                                     const Texturator::Args& args,
                                     float global_scale) {
  uint32_t width, height;
  GetDstSize(image.GetWidth(), image.GetHeight(), args.resize, global_scale,
             &width, &height);

  const UsageInfo& usage_info = kUsageInfos[args.usage];
  const size_t channel_count =
      std::min(image.GetChannelCount(),
               static_cast<uint32_t>(usage_info.dst_component_max));

  // Determined experimentally, the iOS viewer uses surface formats of R8 or
  // RGBA8.
  const size_t aligned_channel_count = channel_count == 1 ? 1 : 4;

  // Determined experimentally, there is some texture alignment less than this
  // amount.
  const size_t kAlignSize = 64;
  const size_t aligned_width = AlignUp(width, kAlignSize);
  const size_t aligned_height = AlignUp(height, kAlignSize);

  // Add the base image plus 1/3 for mip maps (approximate sum of progressively
  // halved mips is 1/3).
  const size_t base_pixels = aligned_width * aligned_height;
  const size_t mip_pixels = base_pixels / 3;
  return (base_pixels + mip_pixels) * aligned_channel_count;
}
}  // namespace

void Texturator::Clear() {
  cc_ = nullptr;
  srcs_.clear();
  dsts_.clear();
  scale_ids_.clear();
  bias_ids_.clear();
  jobs_.clear();
  written_.clear();
}

void Texturator::Begin(ConvertContext* cc) {
  cc_ = cc;
}

void Texturator::End() {
  // Apply global resize scale.
  const float global_scale = ChooseGlobalScale();
  if (global_scale != 1.0f) {
    for (Job& job : jobs_) {
      const size_t op_count = job.type == kJobAddSpecToMetal ? 2 : 1;
      for (size_t op_index = 0; op_index != op_count; ++op_index) {
        Op& op = job.ops[op_index];
        if (op.src->image) {
          const Image& image = *op.src->image;
          const uint32_t src_width = image.GetWidth();
          const uint32_t src_height = image.GetHeight();
          GetDstSize(src_width, src_height, op.args.resize, global_scale,
                     &op.resize_width, &op.resize_height);
          if (op.resize_width != src_width || op.resize_height != src_height) {
            op.pass_mask |= kPassFlagResize;
            op.direct_copy = false;
          }
        }
      }
    }
  }

  // Create directories for images up-front.
  bool prep_failed = false;
  for (const Job& job : jobs_) {
    const size_t op_count = job.type == kJobAddSpecToMetal ? 2 : 1;
    for (size_t op_index = 0; op_index != op_count; ++op_index) {
      const Op& op = job.ops[op_index];
      if (!op.direct_copy || op.need_copy) {
        if (!PrepareWrite(op.dst_path)) {
          prep_failed = true;
        }
      }
    }
  }
  if (prep_failed) {
    return;
  }

  // Process jobs sequentially.
  // Note, this could be multithreaded but there are several issues that make it
  // impractical currently:
  // - The logging and IO caching are not thread-safe.
  // - There are a limited number of textures, so we don't get high utilization
  //   by processing them in parallel. To get better utilization, we need to
  //   break up the images themselves into smaller sections.
  // - The ufgtest.py batch conversion script already performs multithreading on
  //   a process basis, so multithreading within each process helps very little
  //   (and in some cases may be detrimental).
  for (const Job& job : jobs_) {
    ProcessJob(job);
  }
}

const std::string& Texturator::Add(Gltf::Id image_id, const Args& args) {
  Op op(image_id, args);
  const std::string* const dst_name = AddDst(image_id, args, &op);
  if (!dst_name) {
    return AddFallback(args.fallback);
  }
  if (!op.is_new) {
    // Destination image already written. Just return the name.
    return *dst_name;
  }
  jobs_.push_back(Job());
  Job& job = jobs_.back();
  job.type = kJobAdd;
  job.ops[0] = op;
  return *dst_name;
}

void Texturator::AddSpecToMetal(Gltf::Id spec_image_id, const Args& spec_args,
                                Gltf::Id diff_image_id, const Args& diff_args,
                                const std::string** out_metal_name,
                                const std::string** out_base_name) {
  UFG_ASSERT_LOGIC(spec_args.usage == kUsageSpecToMetal);
  UFG_ASSERT_LOGIC(diff_args.usage == kUsageDiffToBase);

  Op spec_op(spec_image_id, spec_args);
  Op diff_op(diff_image_id, diff_args);
  const std::string* spec_dst_name = AddDst(spec_image_id, spec_args, &spec_op);
  const std::string* diff_dst_name = AddDst(diff_image_id, diff_args, &diff_op);
  if (!spec_dst_name && !diff_dst_name) {
    *out_metal_name = &AddFallback(spec_args.fallback);
    *out_base_name = &AddFallback(diff_args.fallback);
    return;
  }

  // If either source is absent, use the name of the other.
  spec_op.is_constant = !spec_dst_name;
  if (spec_op.is_constant) {
    spec_dst_name = AddDst(diff_image_id, spec_args, &spec_op);
  }
  diff_op.is_constant = !diff_dst_name;
  if (diff_op.is_constant) {
    diff_dst_name = AddDst(spec_image_id, diff_args, &diff_op);
  }

  if (!spec_op.is_new && !diff_op.is_new) {
    // Both destination images already written. Just return the names.
    *out_metal_name = spec_dst_name;
    *out_base_name = diff_dst_name;
    return;
  }

  jobs_.push_back(Job());
  Job& job = jobs_.back();
  job.type = kJobAddSpecToMetal;
  job.ops[0] = spec_op;
  job.ops[1] = diff_op;

  *out_metal_name = spec_dst_name;
  *out_base_name = diff_dst_name;
}

int Texturator::GetSolidAlpha(Gltf::Id image_id) {
  Src* const src = FindOrAddSrc(image_id);
  if (!src) {
    return Image::kComponentMax;
  }
  EnsureComponentContent(image_id, src);
  if (!Image::IsSolid(src->rgba_contents[kColorChannelA])) {
    return -1;
  }
  return src->solid_color[kColorChannelA];
}

bool Texturator::IsAlphaOpaque(Gltf::Id image_id, float scale, float bias) {
  const int ia = GetSolidAlpha(image_id);
  if (ia < 0) {
    return false;
  }
  const float a = (ia * Image::kComponentToFloatScale) * scale + bias;
  return a >= 1.0f - kColorTol;
}

bool Texturator::IsAlphaFullyTransparent(
    Gltf::Id image_id, float scale, float bias) {
  const int ia = GetSolidAlpha(image_id);
  if (ia < 0) {
    return false;
  }
  const float a = (ia * Image::kComponentToFloatScale) * scale + bias;
  return a <= kColorTol;
}

Texturator::ColorId Texturator::GetColorId(
    const ColorF& color, const ColorI& identity, ColorIdMap* color_id_map) {
  const ColorI q = Quantize(color);
  if (q == identity) {
    return kColorIdIdentity;
  }
  const ColorId new_id = static_cast<ColorId>(color_id_map->size());
  const auto insert_result = color_id_map->insert(std::make_pair(q, new_id));
  return insert_result.first->second;
}

std::string Texturator::GetDstSuffix(
    const Texturator::Args& args, Gltf::Id image_id, Src* src, Op* op) {
  uint32_t pass_mask = 0;
  std::string suffix;

  const UsageInfo& usage_info = kUsageInfos[args.usage];

  if (args.usage == Texturator::kUsageDefault) {
    const bool remove_alpha =
        args.alpha_mode == Gltf::Material::kAlphaModeOpaque &&
        GetComponentContent(kColorChannelA, image_id, src) !=
            Image::kContentSolid1;
    if (remove_alpha) {
      suffix += "_rgb";
      pass_mask |= kPassFlagRemoveAlpha;
    }
  } else {
    suffix += usage_info.dst_suffix;
  }

  if (usage_info.src_rgb_color_space != usage_info.dst_rgb_color_space) {
    pass_mask |= kPassFlagColorSpace;
  }

  if (args.usage == kUsageSpecToMetal) {
    pass_mask |= kPassFlagSpecToMetal;
  }

  if (cc_->settings.bake_texture_color_scale_bias) {
    const ColorId scale_id =
        GetColorId(args.scale, kQuantizeScaleIdentity, &scale_ids_);
    if (scale_id != kColorIdIdentity) {
      suffix += AppendNumber("_scale", scale_id);
      pass_mask |= kPassFlagScaleBias;
    }

    const ColorId bias_id =
        GetColorId(args.bias, kQuantizeBiasIdentity, &bias_ids_);
    if (bias_id != kColorIdIdentity) {
      suffix += AppendNumber("_bias", bias_id);
      pass_mask |= kPassFlagScaleBias;
    }
  }

  // Add alpha channel if it's missing but needed for alpha scale/bias.
  if (args.usage == Texturator::kUsageDefault &&
      args.alpha_mode != Gltf::Material::kAlphaModeOpaque &&
      (pass_mask & kPassFlagScaleBias) &&
      (args.scale.a != 1.0f || args.bias.a != 0.0f)) {
    LoadSrc(image_id, src);
    if (src->image && src->image->GetChannelCount() < kColorChannelCount) {
      suffix += "_rgba";
      pass_mask |= kPassFlagAddAlpha;
    }
  }

  if (GetResizeSize(image_id, args.resize, src,
                    &op->resize_width, &op->resize_height)) {
    suffix += AppendNumber("_", op->resize_width);
    suffix += AppendNumber("x", op->resize_height);
    pass_mask |= kPassFlagResize;
  }

  if (cc_->settings.normalize_normals && args.usage == kUsageNorm &&
      ((pass_mask & kPassFlagScaleBias) || NeedNormalization(image_id, src))) {
    suffix += "_norm";
    pass_mask |= kPassFlagNormalizeNormals;
  }

  if (cc_->settings.bake_alpha_cutoff &&
      args.alpha_mode == Gltf::Material::kAlphaModeMask) {
    const Image::Content content =
        GetComponentContent(kColorChannelA, image_id, src);
    if (!Image::IsBinary(content)) {
      const Image::Component cutoff =
          Image::FloatToComponent(args.alpha_cutoff);
      suffix += AppendNumber("_cutoff", cutoff);
      pass_mask |= kPassFlagAlphaCutoff;
    }
  }

  op->pass_mask = pass_mask;
  return suffix;
}

const std::string& Texturator::AddFallback(Fallback fallback) {
  // Create the fallback on-demand, the first time it's referenced.
  const FallbackInfo& info = kFallbackInfos[fallback];
  const auto dst_insert_result = dsts_.insert(std::string(info.name));
  const std::string& dst_name = *dst_insert_result.first;
  if (!dst_insert_result.second) {
    return dst_name;
  }

  Image image;
  if (info.r_only) {
    image.CreateR1x1(info.color[0]);
  } else {
    image.Create1x1(info.color);
  }

  const std::string dst_path = Gltf::JoinPath(cc_->dst_dir, dst_name);
  if (PrepareWrite(dst_path)) {
    if (!image.Write(dst_path.c_str(), cc_->settings, cc_->logger)) {
      Log<UFG_ERROR_IO_WRITE_IMAGE>(dst_path.c_str());
    }
  }
  return dst_name;
}

void Texturator::LoadSrc(Gltf::Id image_id, Src* src) const {
  // Load source image on-demand, the first time it is referenced.
  if (src->state != kStateNew) {
    return;
  }
  Gltf::Image::MimeType named_mime_type;
  src->name = GetSrcName(*cc_->gltf, image_id, &named_mime_type);
  UFG_ASSERT_LOGIC(!src->name.empty());
  size_t size;
  Gltf::Image::MimeType mime_type;
  const uint8_t* const data =
      cc_->gltf_cache.GetImageData(image_id, &size, &mime_type);
  if (!data) {
    src->state = kStateMissing;
    return;
  }
  std::unique_ptr<Image> image(new Image());
  {
    Logger::NameSentry name_sentry(cc_->logger, src->name);
    if (!image->Read(data, size, mime_type, cc_->logger)) {
      src->state = kStateMissing;
      return;
    }
  }
  src->image = std::move(image);
  src->state = kStateLoaded;
}

Texturator::Src* Texturator::FindOrAddSrc(Gltf::Id image_id) {
  const Gltf::Image* const gltf_image =
      Gltf::GetById(cc_->gltf->images, image_id);
  if (!gltf_image) {
    return nullptr;
  }
  const auto src_insert_result = srcs_.insert(std::make_pair(image_id, Src()));
  if (src_insert_result.second && !cc_->gltf_cache.ImageExists(image_id)) {
    Gltf::Image::MimeType mime_type;
    const std::string src_name = GetSrcName(*cc_->gltf, image_id, &mime_type);
    GltfLog<GLTF_ERROR_MISSING_IMAGE>(cc_->logger, "", src_name.c_str());
  }
  return &src_insert_result.first->second;
}

const std::string* Texturator::AddDst(Gltf::Id image_id, const Args& args,
                                      Op* out_op) {
  out_op->is_new = false;

  Gltf::Image::MimeType mime_type;
  const std::string src_name = GetSrcName(*cc_->gltf, image_id, &mime_type);
  if (src_name.empty()) {
    return nullptr;
  }

  // Find or add the source image.
  const auto src_insert_result = srcs_.insert(std::make_pair(image_id, Src()));
  Src& src = src_insert_result.first->second;
  if (src_insert_result.second) {
    // Verify the source file exists.
    if (!cc_->gltf_cache.ImageExists(image_id)) {
      GltfLog<GLTF_ERROR_MISSING_IMAGE>(cc_->logger, "", src_name.c_str());
      return nullptr;
    }
  }
  out_op->src = &src;

  // Generate a unique destination name based on conversion args.
  const std::string dst_suffix = GetDstSuffix(args, image_id, &src, out_op);
  std::string new_name = AddFileNameSuffix(src_name, dst_suffix.c_str());

  // Choose image type based on the source type and presence of alpha.
  Gltf::Image::MimeType dst_mime_type = mime_type;
  const bool is_supported_output_type =
      (dst_mime_type == Gltf::Image::kMimeJpeg ||
       dst_mime_type == Gltf::Image::kMimePng);
  const bool override_jpg =
      cc_->settings.prefer_jpeg && dst_mime_type != Gltf::Image::kMimeJpeg;
  if (!is_supported_output_type || override_jpg) {
    if (args.alpha_mode == Gltf::Material::kAlphaModeOpaque ||
        GetComponentContent(kColorChannelA, image_id, &src) ==
            Image::kContentSolid1) {
      dst_mime_type = Gltf::Image::kMimeJpeg;
    } else {
      dst_mime_type = Gltf::Image::kMimePng;
    }
  }
  if (out_op->pass_mask & kPassFlagAddAlpha) {
    dst_mime_type = Gltf::Image::kMimePng;
  }
  SetImageExtension(dst_mime_type, &new_name);

  // Find or add a destination entry keyed by its unique name.
  const auto dst_insert_result = dsts_.insert(new_name);
  const std::string& dst_name = *dst_insert_result.first;
  if (!dst_insert_result.second) {
    return &dst_name;
  }
  out_op->is_new = true;

  out_op->dst_path = Gltf::JoinPath(cc_->dst_dir, dst_name);
  if (dst_suffix.empty() && dst_mime_type == mime_type) {
    if (cc_->settings.limit_total_image_decompressed_size != 0) {
      // We need to load the source image to determine size info.
      // TODO: Ideally, load just the size rather than the whole file.
      LoadSrc(image_id, &src);
    }
    out_op->direct_copy = true;
    out_op->need_copy = !cc_->gltf_cache.IsImageAtPath(
        image_id, cc_->dst_dir.c_str(), dst_name.c_str());
    return &dst_name;
  }

  LoadSrc(image_id, &src);
  if (src.state == kStateMissing) {
    return nullptr;
  }

  return &dst_name;
}

void Texturator::EnsureComponentContent(Gltf::Id image_id, Src* src) const {
  LoadSrc(image_id, src);
  if (src->image &&
      src->rgba_contents[kColorChannelR] == Image::kContentCount) {
    src->image->GetContents(src->rgba_contents,
                            cc_->settings.fix_accidental_alpha,
                            src->solid_color);
  }
}

Image::Content Texturator::GetComponentContent(
    ColorChannel channel, Gltf::Id image_id, Src* src) const {
  EnsureComponentContent(image_id, src);
  return src->rgba_contents[channel];
}

bool Texturator::NeedNormalization(Gltf::Id image_id, Src* src) const {
  LoadSrc(image_id, src);
  if (src->normal_content != kNormalUnknown) {
    return src->normal_content == kNormalNonNormalized;
  }
  if (!src->image) {
    src->normal_content = kNormalNormalized;
    return false;
  }
  const Image& image = *src->image;
  const size_t width = image.GetWidth();
  const size_t height = image.GetHeight();
  const size_t channel_count = image.GetChannelCount();
  UFG_ASSERT_FORMAT(channel_count >= 3);

  // Given u8 component size, fixed-point values fit in an int. For u16, this
  // needs to be changed to int64_t.
  static_assert(sizeof(Image::Component) == sizeof(uint8_t), "");
  using FpInt = int;

  static constexpr FpInt kOne = Image::kComponentMax;

  // The maximum error allowed, in squared fixed-point units.
  // * The effective linear tolerance is sqrt(kErrSqTol/kOne)/kOne.
  static constexpr FpInt kErrSqTol = 4 * kOne;  // sqrt(4)/255 = ~0.008

  const Image::Component* pixel = image.GetData();
  const Image::Component* const pixel_end =
      pixel + width * height * channel_count;
  for (; pixel != pixel_end; pixel += channel_count) {
    const FpInt x = 2 * static_cast<FpInt>(pixel[0]) - kOne;
    const FpInt y = 2 * static_cast<FpInt>(pixel[1]) - kOne;
    const FpInt z = 2 * static_cast<FpInt>(pixel[2]) - kOne;
    const int m_sq = x * x + y * y + z * z;
    const int err_sq = m_sq - kOne * kOne;
    if (std::abs(err_sq) > kErrSqTol) {
      src->normal_content = kNormalNonNormalized;
      return true;
    }
  }

  src->normal_content = kNormalNormalized;
  return false;
}

uint32_t Texturator::GetSrcWidth(Gltf::Id image_id, Src* src) const {
  LoadSrc(image_id, src);
  return src->image ? src->image->GetWidth() : 0;
}

uint32_t Texturator::GetSrcHeight(Gltf::Id image_id, Src* src) const {
  LoadSrc(image_id, src);
  return src->image ? src->image->GetHeight() : 0;
}

bool Texturator::GetResizeSize(
    Gltf::Id image_id, const ImageResizeSettings& resize,
    Src* src, uint32_t* out_width, uint32_t* out_height) const {
  UFG_ASSERT_LOGIC(resize.size_min <= resize.size_max);
  UFG_ASSERT_LOGIC(resize.size_min > 0);
  if (resize.IsDefault()) {
    return false;
  }
  const uint32_t src_width = GetSrcWidth(image_id, src);
  if (src_width == 0) {
    return false;
  }
  const uint32_t src_height = GetSrcHeight(image_id, src);
  uint32_t dst_width, dst_height;
  GetDstSize(src_width, src_height, resize, 1.0f, &dst_width, &dst_height);
  if (dst_width == src_width && dst_height == src_height) {
    return false;
  }
  *out_width = dst_width;
  *out_height = dst_height;
  return true;
}

size_t Texturator::EstimateDecompressedJobSize(const Job& job,
                                               float global_scale) const {
  size_t size = 0;
  switch (job.type) {
  case kJobAdd: {
    const Op& op = job.ops[0];
    const Image* const image = op.src->image.get();
    if (image) {
      size += EstimateDecompressedImageSize(*image, op.args, global_scale);
    }
    break;
  }
  case kJobAddSpecToMetal: {
    const Op& spec_op = job.ops[0];
    const Op& diff_op = job.ops[1];
    const Image* const spec_image = spec_op.is_constant
                                        ? diff_op.src->image.get()
                                        : spec_op.src->image.get();
    const Image* const diff_image = diff_op.is_constant
                                        ? spec_op.src->image.get()
                                        : diff_op.src->image.get();
    if (spec_image) {
      size += EstimateDecompressedImageSize(*spec_image, spec_op.args,
                                            global_scale);
    }
    if (diff_image) {
      size += EstimateDecompressedImageSize(*diff_image, diff_op.args,
                                            global_scale);
    }
    break;
  }
  default:
    UFG_ASSERT_LOGIC(false);
    break;
  }
  return size;
}

float Texturator::ChooseGlobalScale() const {
  const size_t decompressed_limit =
      cc_->settings.limit_total_image_decompressed_size;
  if (decompressed_limit == 0) {
    return 1.0f;
  }

  // Find the first scale for which the decompressed size is not exceeded.
  const size_t job_count = jobs_.size();
  std::vector<size_t> decompressed_sizes(job_count, 0);
  const float scale_step = cc_->settings.limit_total_image_scale_step;
  float scale_power = 1.0f;
  float scale_increment = 1.0f;
  for (;;) {
    const float global_scale = scale_power * scale_increment;
    size_t decompressed_total = 0;
    bool any_changed = false;
    for (size_t job_index = 0; job_index != job_count; ++job_index) {
      const size_t decompressed_size =
          EstimateDecompressedJobSize(jobs_[job_index], global_scale);
      decompressed_total += decompressed_size;
      if (decompressed_sizes[job_index] != decompressed_size) {
        decompressed_sizes[job_index] = decompressed_size;
        any_changed = true;
      }
    }
    if (decompressed_total <= decompressed_limit) {
      break;
    }
    if (!any_changed) {
      Log<UFG_WARN_TEXTURE_LIMIT>(
          job_count, decompressed_limit, decompressed_total);
      break;
    }

    // Scale linearly down to the minimum, then push that scale and start over.
    // E.g. with step=0.25:
    // 1.0, 0.75, 0.50, 0.25, 0.25*0.75, 0.25*0.50, 0.25*0.25, ...
    if (scale_increment < 1.5f * scale_step) {
      // Reached the end of the linear sequence. Push the scale and start over.
      scale_power = global_scale;
      scale_increment = 1.0f;
    }
    scale_increment -= scale_step;
  }

  return scale_power * scale_increment;
}

bool Texturator::PrepareWrite(const std::string& dst_path) {
  UFG_ASSERT_LOGIC(!dst_path.empty());
  if (cc_->gltf_cache.IsSourcePath(dst_path.c_str())) {
    Log<UFG_ERROR_STOMP>(dst_path.c_str());
    return false;
  }
  const std::vector<std::string> created_dirs =
      GltfDiskCreateDirectoryForFile(dst_path);
  written_.push_back(dst_path);
  created_dirs_.insert(created_dirs_.end(),
                       created_dirs.begin(), created_dirs.end());
  return true;
}

void Texturator::ProcessAdd(const Op& op) {
  // Copy the original file to the destination if it doesn't require any
  // processing.
  if (op.direct_copy) {
    UFG_ASSERT(op.pass_mask == 0);
    if (op.need_copy) {
      cc_->gltf_cache.CopyImage(op.image_id, op.dst_path);
    }
    return;
  }

  const uint32_t pass_mask = op.pass_mask;
  const Args& args = op.args;

  // Generating new image.  Apply conversions.
  std::unique_ptr<Image> image =
      CopyImageByUsage(*op.src->image, args.usage, pass_mask);
  if (pass_mask & kPassMaskFloat) {
    const UsageInfo& usage_info = kUsageInfos[args.usage];
    FloatImage float_image(*image, usage_info.src_rgb_color_space);
    ApplyFloatPasses(args, pass_mask, op.resize_width, op.resize_height,
                     &float_image);
    float_image.CopyTo(usage_info.dst_rgb_color_space, &*image);
  }
  if (pass_mask & kPassFlagNormalizeNormals) {
    image->NormalizeNormals();
  }
  if (args.usage == kUsageGlossToRough) {
    image->Invert();
  }
  if (pass_mask & kPassFlagAlphaCutoff) {
    image->ApplyAlphaCutoff(Image::FloatToComponent(args.alpha_cutoff));
  }

  Image::Component dst_solid_color[kColorChannelCount];
  if (image->AreChannelsSolid(cc_->settings.fix_accidental_alpha,
                              dst_solid_color)) {
    // Replace solid black occlusion with solid white.
    if (cc_->settings.black_occlusion_is_white && args.usage == kUsageOccl &&
        dst_solid_color[kColorChannelR] == 0) {
      dst_solid_color[kColorChannelR] = Image::kComponentMax;
    }

    // Shrink solid textures to 1x1 to save space.
    image->Create1x1(dst_solid_color, image->GetChannelCount());
  }

  const bool is_norm = args.usage == kUsageNorm;
  if (!image->Write(op.dst_path.c_str(), cc_->settings, cc_->logger, is_norm)) {
    Log<UFG_ERROR_IO_WRITE_IMAGE>(op.dst_path.c_str());
  }
}

void Texturator::ProcessAddSpecToMetal(const Op& spec_op, const Op& diff_op) {
  const Args& spec_args = spec_op.args;
  const bool spec_is_constant = spec_op.is_constant;

  const Args& diff_args = diff_op.args;
  const bool diff_is_constant = diff_op.is_constant;

  // Get specular color in transformed linear space.
  const uint32_t spec_pass_mask = spec_op.pass_mask;
  std::unique_ptr<Image> spec_image =
      spec_is_constant ? WhiteImageByUsage(*diff_op.src->image, spec_args.usage)
                       : CopyImageByUsage(*spec_op.src->image, spec_args.usage,
                                          spec_pass_mask);
  UFG_ASSERT_LOGIC(spec_image->GetChannelCount() == 3);
  FloatImage spec_float_image(*spec_image, kColorSpaceSrgb);
  ApplyFloatPasses(spec_args, spec_pass_mask, spec_op.resize_width,
                   spec_op.resize_height, &spec_float_image);

  // Get diffuse color in transformed linear space.
  const uint32_t diff_pass_mask = diff_op.pass_mask;
  std::unique_ptr<Image> diff_image =
      diff_is_constant ? WhiteImageByUsage(*spec_op.src->image, diff_args.usage)
                       : CopyImageByUsage(*diff_op.src->image, diff_args.usage,
                                          diff_pass_mask);
  FloatImage diff_float_image(*diff_image, kColorSpaceSrgb);
  ApplyFloatPasses(diff_args, diff_pass_mask, diff_op.resize_width,
                   diff_op.resize_height, &diff_float_image);

  // Convert specular+diffuse --> metallic+base.
  FloatImage metal_float_image;
  FloatImage::ConvertSpecDiffToMetalBase(
      spec_float_image, &diff_float_image, &metal_float_image);

  // Write metallic texture.
  if (spec_op.is_new) {
    UFG_ASSERT_LOGIC(!spec_op.direct_copy);
    Image metal_image;
    // According to the spec, the metallic channel is stored as linear. However,
    // the SpecGlossVsMetalRough reference sample has metallic values in sRGB,
    // so the two models do not look quite alike. I'm assuming this is just an
    // error in the sample, so I'm sticking to the spec.
    metal_float_image.CopyTo(kColorSpaceLinear, &metal_image);
    Image::Component metal_dst_solid_color[kColorChannelCount];
    if (metal_image.AreChannelsSolid(cc_->settings.fix_accidental_alpha,
                                     metal_dst_solid_color)) {
      metal_image.Create1x1(metal_dst_solid_color, 1);
    }
    if (!metal_image.Write(
            spec_op.dst_path.c_str(), cc_->settings, cc_->logger)) {
      Log<UFG_ERROR_IO_WRITE_IMAGE>(spec_op.dst_path.c_str());
      return;
    }
  }

  // Write base texture.
  if (diff_op.is_new) {
    UFG_ASSERT_LOGIC(!diff_op.direct_copy);
    Image base_image;
    diff_float_image.CopyTo(kColorSpaceSrgb, &base_image);
    if (diff_pass_mask & kPassFlagAlphaCutoff) {
      base_image.ApplyAlphaCutoff(
          Image::FloatToComponent(diff_args.alpha_cutoff));
    }
    Image::Component base_dst_solid_color[kColorChannelCount];
    if (base_image.AreChannelsSolid(cc_->settings.fix_accidental_alpha,
                                    base_dst_solid_color)) {
      base_image.Create1x1(base_dst_solid_color, diff_image->GetChannelCount());
    }
    if (!base_image.Write(
            diff_op.dst_path.c_str(), cc_->settings, cc_->logger)) {
      Log<UFG_ERROR_IO_WRITE_IMAGE>(diff_op.dst_path.c_str());
      return;
    }
  }
}

void Texturator::ProcessJob(const Job& job) {
  switch (job.type) {
  case kJobAdd:
    ProcessAdd(job.ops[0]);
    break;
  case kJobAddSpecToMetal:
    ProcessAddSpecToMetal(job.ops[0], job.ops[1]);
    break;
  default:
    UFG_ASSERT_LOGIC(false);
    break;
  }
}
}  // namespace ufg
