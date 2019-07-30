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

#ifndef UFG_PROCESS_IMAGE_H_
#define UFG_PROCESS_IMAGE_H_

#include <algorithm>
#include <vector>
#include "common/common.h"
#include "common/config.h"
#include "common/logging.h"
#include "gltf/gltf.h"
#include "process/color.h"

namespace ufg {
class Image {
 public:
  enum FileType : uint8_t {
    kFileTypeJpg,
    kFileTypePng,
  };

  // A single component (R, G, B, or A).
  using Component = uint8_t;

  struct Transform {
    enum Type {
      kTypeNone,    // Copy channel as-is.
      kTypeInvert,  // 1.0 - channel
    };
    Type type;
    static const Transform kNone;
    static const Transform kInvert;
  };

  static constexpr Image::Component kComponentMax =
      std::numeric_limits<Image::Component>::max();
  static constexpr float kComponentToFloatScale = 1.0f / kComponentMax;

  inline static constexpr float ComponentToFloat(Image::Component c) {
    return c * kComponentToFloatScale;
  }

  inline static constexpr Image::Component FloatToComponent(float f) {
    return f <= 0.0f ?
        0 : (f >= 1.0f ? kComponentMax :
             static_cast<Image::Component>(f * kComponentMax + 0.5f));
  }

  template <size_t kCount>
  inline static void ComponentToFloat(const Image::Component (&c)[kCount],
                                      float (&out_f)[kCount]) {
    for (size_t i = 0; i != kCount; ++i) {
      out_f[i] = ComponentToFloat(c[i]);
    }
  }

  template <size_t kCount>
  inline static void FloatToComponent(const float (&f)[kCount],
                                      Image::Component (&out_c)[kCount]) {
    for (size_t i = 0; i != kCount; ++i) {
      out_c[i] = FloatToComponent(f[i]);
    }
  }

  Image();
  ~Image();

  bool IsValid() const { return width_ != 0; }
  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  uint32_t GetChannelCount() const { return channel_count_; }
  const Component* GetData() const { return buffer_.data(); }
  Component* ModifyData() { return buffer_.data(); }

  void Clear();
  bool Read(
      const void* buffer, size_t size, Gltf::Image::MimeType mime_type,
      Logger* logger);
  bool Write(
      const char* path, const ConvertSettings& settings,
      Logger* logger, bool is_norm = false) const;

  void CreateFromChannel(
      const Image& src, ColorChannel channel, const Transform& transform);
  void CreateFromRgb(const Image& src);
  void CreateFromRgba(const Image& src, Component default_alpha);
  void CreateFromMasked(const Image& src,
                        const Component (&keep_mask)[kColorChannelCount],
                        const Component (&replace_value)[kColorChannelCount]);

  bool ChannelEquals(ColorChannel channel, Component value) const;

  std::vector<float> ToFloat(bool srgb_to_linear) const;
  void CreateFromFloat(const float* data, size_t width, size_t height,
                       size_t channel_count, bool linear_to_srgb);

  void Create1x1(const Component *color, size_t channel_count);
  void CreateWxH(size_t width, size_t height,
                 const Component* color, size_t channel_count);

  template <size_t kCount>
  void Create1x1(const Component (&color)[kCount]) {
    Create1x1(color, kCount);
  }

  void CreateR1x1(Component r) {
    Create1x1(&r, 1);
  }

  enum Content : uint8_t {
    kContentSolid0,   // All values 0.0.
    kContentSolid1,   // All values 1.0.
    kContentSolid,    // All values constant in the closed range (0.0, 1.0).
    kContentBinary,   // All values 0.0 or 1.0.
    kContentVarying,  // Values vary over the open range [0.0, 1.0].
    kContentCount
  };

  static bool IsSolid(Content content) {
    return content == kContentSolid0 || content == kContentSolid1 ||
           content == kContentSolid;
  }

  static bool IsBinary(Content content) {
    return content == kContentSolid0 || content == kContentSolid1 ||
           content == kContentBinary;
  }

  static bool AreChannelsSolid(
      size_t channel_mask, const Content (&content)[kColorChannelCount]) {
    uint32_t bit = 1;
    for (uint32_t i = 0; i != kColorChannelCount; ++i) {
      if ((channel_mask & bit) && !IsSolid(content[i])) {
        return false;
      }
      bit = bit << 1;
    }
    return true;
  }

  // * If fix_accidental_alpha is set, ignore edge pixels for RGBA images to
  // work around accidental transparency (e.g. transparency introduced due to
  // resizing in Photoshop).
  void GetContents(Content (&out_content)[kColorChannelCount],
                   bool fix_accidental_alpha,
                   Component (&out_solid_color)[kColorChannelCount]) const;

  bool AreChannelsSolid(
      bool fix_accidental_alpha,
      Component (&out_solid_color)[kColorChannelCount]) const {
    Content content[kColorChannelCount];
    GetContents(content, fix_accidental_alpha, out_solid_color);
    const size_t channel_mask = (1 << channel_count_) - 1;
    return AreChannelsSolid(channel_mask, content);
  }

  // Normalize normal map vectors.
  void NormalizeNormals();

  // Apply alpha cutoff so all alpha values >= cutoff are 1.0, and 0.0
  // otherwise.
  void ApplyAlphaCutoff(Component cutoff);

  // Invert (1.0-src) each color component.
  void Invert();

 private:
  uint32_t width_;
  uint32_t height_;
  uint8_t channel_count_;
  std::vector<Component> buffer_;
};
}  // namespace ufg
#endif  // UFG_PROCESS_IMAGE_H_
