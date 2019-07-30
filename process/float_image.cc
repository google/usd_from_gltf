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

#include "process/float_image.h"

#include "process/math.h"

namespace ufg {
namespace {
struct ResizeSum1 {
  static constexpr size_t kChannelCount = 1;
  float sum_r;
  ResizeSum1() : sum_r(0.0f) {}
  void AddScaled(const float* src_pixel, float scale) {
    sum_r += src_pixel[kColorChannelR] * scale;
  }
  void StoreScaled(float scale, float* dst) const {
    dst[kColorChannelR] = sum_r * scale;
  }
};

struct ResizeSum2 {
  static constexpr size_t kChannelCount = 2;
  float sum_r, sum_g;
  ResizeSum2() : sum_r(0.0f), sum_g(0.0f) {}
  void AddScaled(const float* src_pixel, float scale) {
    sum_r += src_pixel[kColorChannelR] * scale;
    sum_g += src_pixel[kColorChannelG] * scale;
  }
  void StoreScaled(float scale, float* dst) const {
    dst[kColorChannelR] = sum_r * scale;
    dst[kColorChannelG] = sum_g * scale;
  }
};

struct ResizeSum3 {
  static constexpr size_t kChannelCount = 3;
  float sum_r, sum_g, sum_b;
  ResizeSum3() : sum_r(0.0f), sum_g(0.0f), sum_b(0.0f) {}
  void AddScaled(const float* src_pixel, float scale) {
    sum_r += src_pixel[kColorChannelR] * scale;
    sum_g += src_pixel[kColorChannelG] * scale;
    sum_b += src_pixel[kColorChannelB] * scale;
  }
  void StoreScaled(float scale, float* dst) const {
    dst[kColorChannelR] = sum_r * scale;
    dst[kColorChannelG] = sum_g * scale;
    dst[kColorChannelB] = sum_b * scale;
  }
};

struct ResizeSum4 {
  static constexpr size_t kChannelCount = 4;
  float sum_r, sum_g, sum_b, sum_a;
  ResizeSum4() : sum_r(0.0f), sum_g(0.0f), sum_b(0.0f), sum_a(0.0f) {}
  void AddScaled(const float* src_pixel, float scale) {
    sum_r += src_pixel[kColorChannelR] * scale;
    sum_g += src_pixel[kColorChannelG] * scale;
    sum_b += src_pixel[kColorChannelB] * scale;
    sum_a += src_pixel[kColorChannelA] * scale;
  }
  void StoreScaled(float scale, float* dst) const {
    dst[kColorChannelR] = sum_r * scale;
    dst[kColorChannelG] = sum_g * scale;
    dst[kColorChannelB] = sum_b * scale;
    dst[kColorChannelA] = sum_a * scale;
  }
};

struct ResizeSum4Premul {
  static constexpr size_t kChannelCount = 4;
  float sum_r, sum_g, sum_b, sum_a;
  float premul_sum_r, premul_sum_g, premul_sum_b;

  ResizeSum4Premul()
      : sum_r(0.0f),
        sum_g(0.0f),
        sum_b(0.0f),
        sum_a(0.0f),
        premul_sum_r(0.0f),
        premul_sum_g(0.0f),
        premul_sum_b(0.0f) {}

  void AddScaled(const float* src_pixel, float scale) {
    const float r = src_pixel[kColorChannelR];
    const float g = src_pixel[kColorChannelG];
    const float b = src_pixel[kColorChannelB];
    const float a = src_pixel[kColorChannelA];
    const float a_scaled = a * scale;
    sum_r += r * scale;
    sum_g += g * scale;
    sum_b += b * scale;
    sum_a += a_scaled;
    premul_sum_r += r * a_scaled;
    premul_sum_g += g * a_scaled;
    premul_sum_b += b * a_scaled;
  }

  void StoreScaled(float scale, float* dst) const {
    const float a = sum_a;
    if (a < kColorTol) {
      // Alpha of 0 is not invertible, so use the non-premultiplied RGB.
      dst[kColorChannelR] = sum_r * scale;
      dst[kColorChannelG] = sum_g * scale;
      dst[kColorChannelB] = sum_b * scale;
    } else {
      // Invert premultiplication.
      const float s = 1.0f / a;
      dst[kColorChannelR] = premul_sum_r * s;
      dst[kColorChannelG] = premul_sum_g * s;
      dst[kColorChannelB] = premul_sum_b * s;
    }
    dst[kColorChannelA] = a * scale;
  }
};

// Resize an image using an averaging filter.
// * This works for arbitrary resizing, but acts as a nearest-neighbor filter
//   when upscaling. Fortunately, we only shrink images so this works well for
//   our use case.
template <typename Sum>
void ResizeImageT(size_t src_width, size_t src_height, const float* src_pixels,
                  size_t dst_width, size_t dst_height, float* dst_pixels) {
  UFG_ASSERT_LOGIC(src_width > 0);
  UFG_ASSERT_LOGIC(src_height > 0);
  UFG_ASSERT_LOGIC(dst_width > 0);
  UFG_ASSERT_LOGIC(dst_height > 0);

  constexpr size_t kChannelCount = Sum::kChannelCount;

  // Each destination pixel maps to a fixed-size area in the source image.
  const float dst_to_src_scale_x =
      static_cast<float>(src_width) / static_cast<float>(dst_width);
  const float dst_to_src_scale_y =
      static_cast<float>(src_height) / static_cast<float>(dst_height);
  const float recip_area = 1.0f / (dst_to_src_scale_x * dst_to_src_scale_y);

  const size_t src_row_stride = src_width * kChannelCount;

  for (size_t dst_iy = 0; dst_iy != dst_height; ++dst_iy) {
    // Calculate source Y range overlapping the destination pixel.
    const float src_y0 = dst_iy * dst_to_src_scale_y;
    const float src_y1 = src_y0 + dst_to_src_scale_y;
    const size_t src_iy_begin = static_cast<size_t>(src_y0);
    const size_t src_iy_end =
        std::min(static_cast<size_t>(src_y1) + 1, src_height);
    UFG_ASSERT_LOGIC(src_iy_begin < src_iy_end);
    const size_t src_iy_last = src_iy_end - 1;

    float* const dst_row = dst_pixels + dst_iy * dst_width * kChannelCount;
    for (size_t dst_ix = 0; dst_ix != dst_width; ++dst_ix) {
      // Calculate source X range overlapping the destination pixel.
      const float src_x0 = dst_ix * dst_to_src_scale_x;
      const float src_x1 = src_x0 + dst_to_src_scale_x;
      const size_t src_ix_begin = static_cast<size_t>(src_x0);
      const size_t src_ix_end =
          std::min(static_cast<size_t>(src_x1) + 1, src_width);
      UFG_ASSERT_LOGIC(src_ix_begin < src_ix_end);
      const size_t src_ix_last = src_ix_end - 1;

      // Sum source pixels overlapping the current destination pixel.
      Sum sum;
      if (src_ix_begin == src_ix_last) {
        // Destination is entirely within a single source column.
        // TODO: This should only really happen when magnifying the
        // texture, so we could specialize this function for the common
        // minifying case.
        const float src_dx = src_x1 - src_x0;
        if (src_iy_begin == src_iy_last) {
          // Destination is entirely within a single source row.
          const float* const src_row =
              src_pixels + src_iy_begin * src_row_stride;
          const float weight_y = src_y1 - src_y0;
          sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                        weight_y * src_dx);
        } else {
          // Destination overlaps 2 or more source rows.
          {
            // First partial column.
            const float* const src_row =
                src_pixels + src_iy_begin * src_row_stride;
            const float weight_y = 1.0f - src_y0 + src_iy_begin;
            sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                          weight_y * src_dx);
          }
          for (size_t src_iy = src_iy_begin + 1; src_iy != src_iy_last;
               ++src_iy) {
            // Interior whole columns.
            const float* const src_row = src_pixels + src_iy * src_row_stride;
            sum.AddScaled(src_row + src_ix_begin * kChannelCount, src_dx);
          }
          {
            // Last partial column.
            const float* const src_row =
                src_pixels + src_iy_last * src_row_stride;
            const float weight_y = 1.0f - src_iy_end + src_y1;
            sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                          weight_y * src_dx);
          }
        }
      } else {
        // Destination overlaps 2 or more source columns.
        if (src_iy_begin == src_iy_last) {
          // Destination is entirely within a single source row.
          const float* const src_row =
              src_pixels + src_iy_begin * src_row_stride;
          const float weight_y = src_y1 - src_y0;
          sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                        weight_y * (1.0f - src_x0 + src_ix_begin));
          for (size_t src_ix = src_ix_begin + 1; src_ix != src_ix_last;
               ++src_ix) {
            sum.AddScaled(src_row + src_ix * kChannelCount, weight_y);
          }
          sum.AddScaled(src_row + src_ix_last * kChannelCount,
                        weight_y * (1.0f - src_ix_end + src_x1));
        } else {
          // Destination overlaps 2 or more source rows.
          {
            // First partial column.
            const float* const src_row =
                src_pixels + src_iy_begin * src_row_stride;
            const float weight_y = 1.0f - src_y0 + src_iy_begin;
            sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                          weight_y * (1.0f - src_x0 + src_ix_begin));
            for (size_t src_ix = src_ix_begin + 1; src_ix != src_ix_last;
                 ++src_ix) {
              sum.AddScaled(src_row + src_ix * kChannelCount, weight_y);
            }
            sum.AddScaled(src_row + src_ix_last * kChannelCount,
                          weight_y * (1.0f - src_ix_end + src_x1));
          }
          for (size_t src_iy = src_iy_begin + 1; src_iy != src_iy_last;
               ++src_iy) {
            // Interior whole columns.
            const float* const src_row = src_pixels + src_iy * src_row_stride;
            sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                          1.0f - src_x0 + src_ix_begin);
            for (size_t src_ix = src_ix_begin + 1; src_ix != src_ix_last;
                 ++src_ix) {
              sum.AddScaled(src_row + src_ix * kChannelCount, 1.0f);
            }
            sum.AddScaled(src_row + src_ix_last * kChannelCount,
                          1.0f - src_ix_end + src_x1);
          }
          {
            // Last partial column.
            const float* const src_row =
                src_pixels + src_iy_last * src_row_stride;
            const float weight_y = 1.0f - src_iy_end + src_y1;
            sum.AddScaled(src_row + src_ix_begin * kChannelCount,
                          weight_y * (1.0f - src_x0 + src_ix_begin));
            for (size_t src_ix = src_ix_begin + 1; src_ix != src_ix_last;
                 ++src_ix) {
              sum.AddScaled(src_row + src_ix * kChannelCount, weight_y);
            }
            sum.AddScaled(src_row + src_ix_last * kChannelCount,
                          weight_y * (1.0f - src_ix_end + src_x1));
          }
        }
      }

      // Store pixel average by dividing the sum by area.
      sum.StoreScaled(recip_area, dst_row + dst_ix * kChannelCount);
    }
  }
}

void ResizeImage(size_t channel_count, bool premul_alpha, size_t src_width,
                 size_t src_height, const float* src_pixels, size_t dst_width,
                 size_t dst_height, float* dst_pixels) {
  switch (channel_count) {
    case 1:
      ResizeImageT<ResizeSum1>(src_width, src_height, src_pixels,
                               dst_width, dst_height, dst_pixels);
      break;
    case 2:
      ResizeImageT<ResizeSum2>(src_width, src_height, src_pixels,
                               dst_width, dst_height, dst_pixels);
      break;
    case 3:
      ResizeImageT<ResizeSum3>(src_width, src_height, src_pixels,
                               dst_width, dst_height, dst_pixels);
      break;
    case 4:
      if (premul_alpha) {
        ResizeImageT<ResizeSum4Premul>(src_width, src_height, src_pixels,
                                       dst_width, dst_height, dst_pixels);
      } else {
        ResizeImageT<ResizeSum4>(src_width, src_height, src_pixels,
                                 dst_width, dst_height, dst_pixels);
      }
      break;
    default:
      UFG_ASSERT_LOGIC(false);
      break;
  }
}
}  // namespace

FloatImage::FloatImage() : width_(0), height_(0), channel_count_(0) {}

FloatImage::FloatImage(const Image& src, ColorSpace src_color_space) {
  CopyFrom(src, src_color_space);
}

void FloatImage::CopyFrom(const Image& src, ColorSpace src_color_space) {
  width_ = src.GetWidth();
  height_ = src.GetHeight();
  channel_count_ = src.GetChannelCount();
  pixels_ = src.ToFloat(src_color_space == kColorSpaceSrgb);
}

void FloatImage::CopyTo(ColorSpace dst_color_space, Image* dst) const {
  dst->CreateFromFloat(pixels_.data(), width_, height_, channel_count_,
                       dst_color_space == kColorSpaceSrgb);
}

void FloatImage::ScaleBias(const ColorF& scale, const ColorF& bias) {
  const size_t width = width_;
  const size_t height = height_;
  const size_t channel_count = channel_count_;
  float* const pixels = pixels_.data();
  float* const pixel_end = pixels + width * height * channel_count;
  const float s0 = scale.c[0];
  const float s1 = scale.c[1];
  const float s2 = scale.c[2];
  const float s3 = scale.c[3];
  const float b0 = bias.c[0];
  const float b1 = bias.c[1];
  const float b2 = bias.c[2];
  const float b3 = bias.c[3];
  switch (channel_count) {
  case 1:
    for (float* pixel = pixels; pixel != pixel_end; pixel += 1) {
      pixel[0] = pixel[0] * s0 + b0;
    }
    break;
  case 2:
    for (float* pixel = pixels; pixel != pixel_end; pixel += 2) {
      pixel[0] = pixel[0] * s0 + b0;
      pixel[1] = pixel[1] * s1 + b1;
    }
    break;
  case 3:
    for (float* pixel = pixels; pixel != pixel_end; pixel += 3) {
      pixel[0] = pixel[0] * s0 + b0;
      pixel[1] = pixel[1] * s1 + b1;
      pixel[2] = pixel[2] * s2 + b2;
    }
    break;
  case 4:
    for (float* pixel = pixels; pixel != pixel_end; pixel += 4) {
      pixel[0] = pixel[0] * s0 + b0;
      pixel[1] = pixel[1] * s1 + b1;
      pixel[2] = pixel[2] * s2 + b2;
      pixel[3] = pixel[3] * s3 + b3;
    }
    break;
  default:
    UFG_ASSERT_LOGIC(false);
    break;
  }
}

void FloatImage::ScaleBiasNormals(const ColorF& scale, const ColorF& bias) {
  const size_t width = width_;
  const size_t height = height_;
  const size_t channel_count = channel_count_;
  float* const pixels = pixels_.data();
  UFG_ASSERT_FORMAT(channel_count >= 3);
  float* const pixel_end = pixels + width * height * channel_count;

  // Normals need to be converted from the [0, 1]-space to [-1, 1]-space when
  // scaling and biasing, then converted back to the [0, 1]-space when stored.
  // This transform only affects bias though, so we can transform it into [0,
  // 1]-space up front.
  const float s0 = scale.c[0], s1 = scale.c[1], s2 = scale.c[2];
  const float b0 = 0.5f * (bias.c[0] - s0);
  const float b1 = 0.5f * (bias.c[1] - s1);
  const float b2 = 0.5f * (bias.c[2] - s2);

  for (float* pixel = pixels; pixel != pixel_end; pixel += channel_count) {
    const float x = pixel[0] * s0 + b0;
    const float y = pixel[1] * s1 + b1;
    const float z = pixel[2] * s2 + b2;

    // Scale normal so it's no larger than ±0.5, to fit in quantized color
    // components when biased by +0.5.
    const float m = Max4(0.5f, std::abs(x), std::abs(y), std::abs(z));
    const float dst_scale = 0.5f / m;

    pixel[0] = x * dst_scale + 0.5f;
    pixel[1] = y * dst_scale + 0.5f;
    pixel[2] = z * dst_scale + 0.5f;
  }
}

void FloatImage::ConvertSpecDiffToMetalBase(
    const FloatImage& in_spec,
    FloatImage* in_diff_out_base, FloatImage* out_metal) {
  static constexpr size_t kSpecChannelCount = 3;
  static constexpr size_t kMetalChannelCount = 1;

  const size_t spec_width = in_spec.width_;
  const size_t spec_height = in_spec.height_;
  UFG_ASSERT_LOGIC(in_spec.channel_count_ == kSpecChannelCount);
  const float* const spec_pixels = in_spec.pixels_.data();

  // Diffuse/base pixels converted in-place.
  const size_t diff_width = in_diff_out_base->width_;
  const size_t diff_height = in_diff_out_base->height_;
  const size_t diff_channel_count = in_diff_out_base->channel_count_;
  float* const diff_base_pixels = in_diff_out_base->pixels_.data();

  // Output metallic texture inherits dimensions from the specular texture.
  out_metal->Reset(spec_width, spec_height, kMetalChannelCount);
  float* const out_metal_pixels = out_metal->pixels_.data();

  if (spec_width == diff_width && spec_height == diff_height) {
    // If the two inputs are the same size, we can directly iterate over both
    // sets of pixels.
    const size_t pixel_count = spec_width * spec_height;
    for (size_t i = 0; i != pixel_count; ++i) {
      const float* const spec = spec_pixels + kSpecChannelCount * i;
      float* const diff_base = diff_base_pixels + diff_channel_count * i;
      float* const metal = out_metal_pixels + i;
      SpecDiffToMetalBase(spec, diff_base, metal, diff_base);
    }
  } else {
    // We can't update pixels in-place because they may be sampled multiple
    // times, so make a copy.
    std::vector<float> diff_buffer(
        diff_base_pixels,
        diff_base_pixels + diff_width * diff_height * diff_channel_count);
    const float* const diff_pixels = diff_buffer.data();

    // Iterate at a sample rate high enough to touch each pixel of both images.
    // TODO: Perform a resize up-front to simplify this and improve
    // sampling.
    const size_t max_width = std::max(spec_width, diff_width);
    const size_t max_height = std::max(spec_height, diff_height);
    const float x_to_u_scale = 1.0f / static_cast<float>(max_width);
    const float y_to_v_scale = 1.0f / static_cast<float>(max_height);
    const float spec_u_to_x_scale = static_cast<float>(spec_width);
    const float spec_v_to_y_scale = static_cast<float>(spec_height);
    const float diff_u_to_x_scale = static_cast<float>(diff_width);
    const float diff_v_to_y_scale = static_cast<float>(diff_height);
    const size_t spec_row_stride = kSpecChannelCount * spec_width;
    const size_t diff_row_stride = diff_channel_count * diff_width;
    for (uint32_t y = 0; y != max_height; ++y) {
      const float v = y * y_to_v_scale;
      const uint32_t spec_y =
          static_cast<uint32_t>(v * spec_v_to_y_scale + 0.5f);
      const uint32_t diff_y =
          static_cast<uint32_t>(v * diff_v_to_y_scale + 0.5f);
      const float* const row_spec = spec_pixels + spec_y * spec_row_stride;
      const float* const row_diff = diff_pixels + diff_y * diff_row_stride;
      float* const row_metal = out_metal_pixels + spec_y * spec_width;
      float* const row_base = diff_base_pixels + diff_y * diff_row_stride;
      for (uint32_t x = 0; x != max_width; ++x) {
        const float u = x * x_to_u_scale;
        const uint32_t spec_x =
            static_cast<uint32_t>(u * spec_u_to_x_scale + 0.5f);
        const uint32_t diff_x =
            static_cast<uint32_t>(u * diff_u_to_x_scale + 0.5f);
        const float* const spec = row_spec + spec_x * kSpecChannelCount;
        const float* const diff = row_diff + diff_x * diff_channel_count;
        float* const metal = row_metal + spec_x;
        float* const base = row_base + diff_x * diff_channel_count;
        SpecDiffToMetalBase(spec, diff, metal, base);
      }
    }
  }
}

void FloatImage::Resize(size_t width, size_t height, bool premul_alpha) {
  std::vector<float> dst_pixels(width * height * channel_count_);
  ResizeImage(channel_count_, premul_alpha,
              width_, height_, pixels_.data(),
              width, height, dst_pixels.data());
  width_ = static_cast<uint32_t>(width);
  height_ = static_cast<uint32_t>(height);
  pixels_.swap(dst_pixels);
}
}  // namespace ufg
