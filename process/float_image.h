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

#ifndef UFG_PROCESS_FLOAT_IMAGE_H_
#define UFG_PROCESS_FLOAT_IMAGE_H_

#include <vector>
#include "process/image.h"

namespace ufg {
enum ColorSpace : uint8_t {
  kColorSpaceLinear,
  kColorSpaceSrgb,
};

// Image data stored as linear floating-point values, for use in texture
// reprocessing.
class FloatImage {
 public:
  FloatImage();
  FloatImage(const Image& src, ColorSpace src_color_space);

  bool IsValid() const { return width_ != 0; }
  uint32_t GetWidth() const { return width_; }
  uint32_t GetHeight() const { return height_; }
  uint32_t GetChannelCount() const { return channel_count_; }

  // Copy quantized image into this image, converting sRGB->linear if necessary.
  void CopyFrom(const Image& src, ColorSpace src_color_space);

  // Copy this image into a quantized image, converting linear->sRGB if
  // necessary.
  void CopyTo(ColorSpace dst_color_space, Image* dst) const;

  // Scale and bias pixel colors.
  void ScaleBias(const ColorF& scale, const ColorF& bias);

  // Scale and bias normal-map values in normal vector space ([0,1] -> [-1,1]),
  // and rescaling as necessary to prevent quantization clamping.
  void ScaleBiasNormals(const ColorF& scale, const ColorF& bias);

  // Convert specular+diffuse --> metallic+base.
  // * Diffuse is converted to base in-place, preserving the alpha channel if it
  //   exists.
  static void ConvertSpecDiffToMetalBase(
      const FloatImage& in_spec,
      FloatImage* in_diff_out_base, FloatImage* out_metal);

  // Filtered image resize.
  void Resize(size_t width, size_t height, bool premul_alpha);

 private:
  uint32_t width_;
  uint32_t height_;
  uint32_t channel_count_;
  std::vector<float> pixels_;

  void Reset(size_t width, size_t height, size_t channel_count) {
    width_ = static_cast<uint32_t>(width);
    height_ = static_cast<uint32_t>(height);
    channel_count_ = static_cast<uint32_t>(channel_count);
    pixels_.clear();
    pixels_.resize(width * height * channel_count);
  }
};
}  // namespace ufg
#endif  // UFG_PROCESS_FLOAT_IMAGE_H_
