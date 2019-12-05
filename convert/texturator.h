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

#ifndef UFG_CONVERT_TEXTURATOR_H_
#define UFG_CONVERT_TEXTURATOR_H_

#include <map>
#include <memory>
#include <set>
#include <string>
#include "common/common_util.h"
#include "convert/convert_context.h"
#include "gltf/gltf.h"
#include "process/color.h"
#include "process/image.h"

namespace ufg {
// Performs texture reprocessing and caching.
class Texturator {
 public:
  enum Usage : uint8_t {
    kUsageDefault,       // Inputs: RGB:sRGB, A:linear
    kUsageLinear,        // Inputs: RGB:sRGB, A:linear
    kUsageDiffToBase,    // Inputs: RGB:sRGB, A:linear
    kUsageNorm,          // Inputs: RGB:linear
    kUsageOccl,          // Inputs: R:linear
    kUsageMetal,         // Inputs: B:linear
    kUsageRough,         // Inputs: G:linear
    kUsageSpec,          // Inputs: RGB:sRGB
    kUsageSpecToMetal,   // Inputs: RGB:sRGB
    kUsageGloss,         // Inputs: A:linear
    kUsageGlossToRough,  // Inputs: A:linear
    kUsageUnlitA,        // Inputs: RGB:sRGB, A:linear
    kUsageCount
  };

  enum Fallback : uint8_t {
    kFallbackBlack,
    kFallbackMagenta,
    kFallbackR0,
    kFallbackR1,
    kFallbackCount
  };

  struct Args {
    Usage usage = kUsageDefault;
    Fallback fallback = kFallbackBlack;
    Gltf::Material::AlphaMode alpha_mode = Gltf::Material::kAlphaModeOpaque;
    float alpha_cutoff = 0.5f;
    float opacity = 1.f;
    ColorF scale = ColorF::kOne;
    ColorF bias = ColorF::kZero;
    ImageResizeSettings resize;
  };

  void Clear();
  void Begin(ConvertContext* cc);
  void End();
  const std::string& Add(Gltf::Id image_id, const Args& args);
  void AddSpecToMetal(Gltf::Id spec_image_id, const Args& spec_args,
                      Gltf::Id diff_image_id, const Args& diff_args,
                      const std::string** out_metal_name,
                      const std::string** out_base_name);

  // Get the constant alpha for an image, or -1 if the image has varying alpha.
  // * The image should already be added with Add().
  // * Defaults to opaque alpha (Image::kComponentMax) if the source image
  //   cannot be loaded.
  int GetSolidAlpha(Gltf::Id image_id);

  // Return true if textured alpha is effectively opaque.
  bool IsAlphaOpaque(Gltf::Id image_id, float scale, float bias);
  // Return true if textured alpha is fully transparent (solid 0).
  bool IsAlphaFullyTransparent(Gltf::Id image_id, float scale, float bias);

  const std::vector<std::string>& GetWritten() const { return written_; }
  const std::vector<std::string>& GetCreatedDirectories() const {
    return created_dirs_;
  }

 private:
  enum State : uint8_t {
    kStateNew,
    kStateLoaded,
    kStateMissing,
  };

  enum NormalContent : uint8_t {
    kNormalUnknown,
    kNormalNormalized,
    kNormalNonNormalized,
  };

  struct Src {
    std::string name;
    State state = kStateNew;
    Image::Content rgba_contents[kColorChannelCount] = {Image::kContentCount};
    Image::Component solid_color[kColorChannelCount] =
        {0, 0, 0, Image::kComponentMax};
    NormalContent normal_content = kNormalUnknown;
    std::unique_ptr<Image> image;
  };

  using ColorId = int;
  static constexpr ColorId kColorIdIdentity = -1;

  using SrcMap = std::map<Gltf::Id, Src>;
  using ColorIdMap = std::map<ColorI, ColorId>;

  // Conversion operation.
  struct Op {
    Gltf::Id image_id;
    Args args;
    const Src* src = nullptr;
    bool is_constant = false;
    bool is_new = false;
    bool direct_copy = false;
    bool need_copy = false;
    uint32_t pass_mask = 0;
    uint32_t resize_width = 0;
    uint32_t resize_height = 0;
    std::string dst_path;

    Op() {}
    Op(Gltf::Id image_id, const Args& args) : image_id(image_id), args(args) {}
  };

  enum JobType : uint8_t {
    kJobAdd,
    kJobAddSpecToMetal,
  };

  struct Job {
    JobType type;
    // Up to 2 ops for AddSpecToMetal.
    Op ops[2];
  };

  ConvertContext* cc_;
  SrcMap srcs_;
  std::set<std::string> dsts_;
  ColorIdMap scale_ids_;
  ColorIdMap bias_ids_;
  std::vector<Job> jobs_;
  std::vector<std::string> written_;
  std::vector<std::string> created_dirs_;

  // Convert a color to a unique identifier from its quantized value, used to
  // uniquely name a transformed texture (without having to encode 4x floats
  // into the name).
  ColorId GetColorId(const ColorF& color, const ColorI& identity,
                     ColorIdMap* color_id_map);

  std::string GetDstSuffix(const Texturator::Args& args, Gltf::Id image_id,
                           Src* src, Op* op);
  const std::string& AddFallback(Fallback fallback);
  void LoadSrc(Gltf::Id image_id, Src* src) const;
  Src* FindOrAddSrc(Gltf::Id image_id);
  const std::string* AddDst(Gltf::Id image_id, const Args& args, Op* out_op);
  void EnsureComponentContent(Gltf::Id image_id, Src* src) const;
  Image::Content GetComponentContent(ColorChannel channel, Gltf::Id image_id,
                                     Src* src) const;
  bool NeedNormalization(Gltf::Id image_id, Src* src) const;
  uint32_t GetSrcWidth(Gltf::Id image_id, Src* src) const;
  uint32_t GetSrcHeight(Gltf::Id image_id, Src* src) const;
  bool GetResizeSize(Gltf::Id image_id, const ImageResizeSettings& resize,
                     Src* src, uint32_t* out_width, uint32_t* out_height) const;
  size_t EstimateDecompressedJobSize(const Job& job, float global_scale) const;
  float ChooseGlobalScale() const;
  bool PrepareWrite(const std::string& dst_path);
  void ProcessAdd(const Op& op);
  void ProcessAddSpecToMetal(const Op& spec_op, const Op& diff_op);
  void ProcessJob(const Job& job);

  template <What kWhat, typename ...Ts>
  inline void Log(Ts... args) const {
    ufg::Log<kWhat>(cc_->logger, "", args...);
  }
};
}  // namespace ufg

#endif  // UFG_CONVERT_TEXTURATOR_H_
