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

#include "process/image.h"

#include "common/common_util.h"
#include "gltf/stream.h"
#include "process/image_fallback.h"
#include "process/image_gif.h"
#include "process/image_jpg.h"
#include "process/image_png.h"
#include "process/math.h"

namespace ufg {
namespace {

// https://en.wikipedia.org/wiki/SRGB
float LinearToSrgb(float lin) {
  return lin <= 0.0031308f ? (lin * 12.92f)
                           : (1.055f * std::pow(lin, 1.0f / 2.4f) - 0.055f);
}

float SrgbToLinear(float srgb) {
  static constexpr float kScale = 1.0f / 1.055f;
  static constexpr float kBias = 0.055f / 1.055f;
  static constexpr float kLin = 1.0f / 12.92f;
  return srgb <= 0.04045f ? srgb * kLin : std::pow(srgb * kScale + kBias, 2.4f);
}

// Look-up table for sRGB->linear conversion.
struct SrgbToLinearTable {
  float linear_values[Image::kComponentMax + 1];
  SrgbToLinearTable() {
    for (size_t srgb = 0; srgb != UFG_ARRAY_SIZE(linear_values); ++srgb) {
      linear_values[srgb] = SrgbToLinear(
          Image::ComponentToFloat(static_cast<Image::Component>(srgb)));
    }
  }
};
const SrgbToLinearTable kSrgbToLinearTable;

// Hash table for linear->sRGB conversion.
struct LinearToSrgbTable {
  // Table size chosen to be large enough to prevent hash conflicts between
  // sRGB-as-linear values. For u8 [0, 255], this works out to:
  //   1 / (SrgbToLinear(1/255)-SrgbToLinear(0)) = 1 / 0.000303526991 < 3296.
  // TODO: This method won't work if we ever use 16-bit inputs,
  // because kEntryCount will be too large.
  static_assert(Image::kComponentMax == 255, "");
  static constexpr size_t kEntryCount = 3296;
  static constexpr float kEntryToLinScale = kEntryCount - 1;

  // Hash table where each entry describes the lower value of the quantized sRGB
  // result, and the linear threshold at which it transitions to lower+1.
  // * Note, we use a non-minimal perfect hash function (i.e. contains gaps but
  //   no collisions) that just linearly maps floating point inputs in the range
  //   [0, 1] to array indices. Because we don't have to deal with collisions,
  //   each entry just contains a single result value, and gaps are populated
  //   with duplicates. The 'lin_upper' threshold allows us to handle cases
  //   where a hash bucket spans multiple output values ('srgb_lower' and +1).
  struct Entry {
    Image::Component srgb_lower;
    float lin_upper;
  };
  Entry entries[kEntryCount];

  LinearToSrgbTable() {
    // Bias by 0.5 to account for rounding.
    constexpr float kLinUpperBias = 0.5f * Image::kComponentToFloatScale;
    size_t entry_index = 0;
    for (Image::Component srgb_lower = 0; srgb_lower != Image::kComponentMax;
         ++srgb_lower) {
      const float lin_upper = SrgbToLinear(
          srgb_lower * Image::kComponentToFloatScale + kLinUpperBias);
      const size_t entry_upper =
          static_cast<size_t>(lin_upper * kEntryToLinScale);
      for (; entry_index <= entry_upper; ++entry_index) {
        Entry& entry = entries[entry_index];
        entry.srgb_lower = srgb_lower;
        entry.lin_upper = lin_upper;
      }
    }
    for (; entry_index != UFG_ARRAY_SIZE(entries); ++entry_index) {
      Entry& entry = entries[entry_index];
      entry.srgb_lower = Image::kComponentMax;
      entry.lin_upper = std::numeric_limits<float>::max();
    }
  }

  inline Image::Component Lookup(float lin) const {
    // Perform a hash table lookup to find the sRGB lower bound.
    if (lin < 0.0f) {
      return 0;
    }
    const size_t entry_index =
        std::min(static_cast<size_t>(lin * kEntryToLinScale), kEntryCount - 1);
    const Entry& entry = entries[entry_index];

    // Refine result based on the linear threshold between sRGB values.
    const Image::Component srgb_lower = entry.srgb_lower;
    return lin < entry.lin_upper ? srgb_lower : srgb_lower + 1;
  }
};
const LinearToSrgbTable kLinearToSrgbTable;

template <Image::Transform::Type kTransformType>
uint8_t TransformChannelValue(uint8_t value, const Image::Transform& transform);
template <>
inline uint8_t TransformChannelValue<Image::Transform::kTypeNone>(
    uint8_t value, const Image::Transform& transform) {
  return value;
}
template <>
inline uint8_t TransformChannelValue<Image::Transform::kTypeInvert>(
    uint8_t value, const Image::Transform& transform) {
  return 255 - value;
}

template <Image::Transform::Type kTransformType>
void CopyChannelValuesT(
    size_t dst_size, int channel, size_t src_pixel_stride,
    const uint8_t* src_base, const Image::Transform& transform,
    uint8_t* dst_base) {
  const uint8_t* src = src_base + channel;
  for (uint8_t *dst = dst_base, *const dst_end = dst + dst_size; dst != dst_end;
       ++dst) {
    *dst = TransformChannelValue<kTransformType>(*src, transform);
    src += src_pixel_stride;
  }
}

void CopyChannelValues(
    size_t dst_size, int channel, size_t src_pixel_stride,
    const uint8_t* src_base, const Image::Transform& transform,
    uint8_t* dst_base) {
  switch (transform.type) {
    case Image::Transform::kTypeNone:
      CopyChannelValuesT<Image::Transform::kTypeNone>(
          dst_size, channel, src_pixel_stride, src_base, transform, dst_base);
      break;
    case Image::Transform::kTypeInvert:
      CopyChannelValuesT<Image::Transform::kTypeInvert>(
          dst_size, channel, src_pixel_stride, src_base, transform, dst_base);
      break;
    default:
      UFG_ASSERT_LOGIC(false);
      break;
  }
}
}  // namespace

const Image::Transform Image::Transform::kNone =
    {Image::Transform::kTypeNone};
const Image::Transform Image::Transform::kInvert =
    {Image::Transform::kTypeInvert};

Image::Image() : width_(0), height_(0), channel_count_(0) {
  Clear();
}

Image::~Image() {
  Clear();
}

void Image::Clear() {
  width_ = 0;
  height_ = 0;
  channel_count_ = 0;
  buffer_.clear();
}

bool Image::Read(
    const void* buffer, size_t size, Gltf::Image::MimeType mime_type,
    Logger* logger) {
  Clear();

  // A lot of source files intermingle images with incorrect file extensions, so
  // determine type from the header.
  if (HasPngHeader(buffer, size)) {
    return PngRead(
        buffer, size, &width_, &height_, &channel_count_, &buffer_, logger);
  }
  if (HasJpgHeader(buffer, size)) {
    return JpgRead(
        buffer, size, &width_, &height_, &channel_count_, &buffer_, logger);
  }
  if (HasGifHeader(buffer, size)) {
    return GifRead(
        buffer, size, &width_, &height_, &channel_count_, &buffer_, logger);
  }
  return ImageFallbackRead(
      buffer, size, &width_, &height_, &channel_count_, &buffer_, logger);
}

bool Image::Write(
    const char* path, const ConvertSettings& settings,
    Logger* logger, bool is_norm) const {
  UFG_ASSERT_LOGIC(IsValid());
  if (Gltf::StringEndsWithCI(path, ".png")) {
    return PngWrite(path, width_, height_, channel_count_, buffer_.data(),
                    settings.png_level, logger);
  } else {
    const int quality =
        is_norm ? settings.jpg_quality_norm : settings.jpg_quality;
    const int subsamp = is_norm ? 0 : settings.jpg_subsamp;
    return JpgWrite(path, width_, height_, channel_count_, buffer_.data(),
                    quality, subsamp, logger);
  }
}

void Image::CreateFromChannel(const Image& src, ColorChannel channel,
                              const Transform& transform) {
  UFG_ASSERT_LOGIC(src.IsValid());
  Clear();
  const uint32_t dst_size = src.width_ * src.height_;
  buffer_.resize(dst_size);
  CopyChannelValues(dst_size, channel, src.channel_count_, src.buffer_.data(),
                    transform, buffer_.data());
  width_ = src.width_;
  height_ = src.height_;
  channel_count_ = 1;
}

void Image::CreateFromRgb(const Image& src) {
  UFG_ASSERT_LOGIC(src.IsValid());
  Clear();
  constexpr size_t kDstChannelCount = 3;
  UFG_ASSERT_LOGIC(src.channel_count_ >= kDstChannelCount);
  const size_t src_channel_count = src.channel_count_;
  const size_t dst_size = src.width_ * src.height_ * kDstChannelCount;
  buffer_.resize(dst_size);
  const Component* s = src.buffer_.data();
  for (Component* d = buffer_.data(), *const end = d + dst_size; d != end; ) {
    d[kColorChannelR] = s[kColorChannelR];
    d[kColorChannelG] = s[kColorChannelG];
    d[kColorChannelB] = s[kColorChannelB];
    s += src_channel_count;
    d += kDstChannelCount;
  }
  width_ = src.width_;
  height_ = src.height_;
  channel_count_ = kDstChannelCount;
}

void Image::CreateFromRgba(const Image& src, Component default_alpha) {
  UFG_ASSERT_LOGIC(src.IsValid());
  Clear();
  constexpr size_t kDstChannelCount = 4;
  const size_t src_channel_count = src.channel_count_;
  const size_t dst_size = src.width_ * src.height_ * kDstChannelCount;
  buffer_.resize(dst_size);
  const Component* s = src.buffer_.data();
  if (src_channel_count == 3) {
    for (Component* d = buffer_.data(), *const end = d + dst_size; d != end; ) {
      d[kColorChannelR] = s[kColorChannelR];
      d[kColorChannelG] = s[kColorChannelG];
      d[kColorChannelB] = s[kColorChannelB];
      d[kColorChannelA] = default_alpha;
      s += src_channel_count;
      d += kDstChannelCount;
    }
  } else {
    UFG_ASSERT_LOGIC(src.channel_count_ == kDstChannelCount);
    for (Component* d = buffer_.data(), *const end = d + dst_size; d != end; ) {
      d[kColorChannelR] = s[kColorChannelR];
      d[kColorChannelG] = s[kColorChannelG];
      d[kColorChannelB] = s[kColorChannelB];
      d[kColorChannelA] = s[kColorChannelA];
      s += src_channel_count;
      d += kDstChannelCount;
    }
  }
  width_ = src.width_;
  height_ = src.height_;
  channel_count_ = kDstChannelCount;
}

void Image::CreateFromMasked(
    const Image& src, const Component (&keep_mask)[kColorChannelCount],
    const Component (&replace_value)[kColorChannelCount]) {
  UFG_ASSERT_LOGIC(src.IsValid());
  Clear();
  const size_t channel_count = src.channel_count_;
  const size_t pixel_count = src.width_ * src.height_;
  buffer_.resize(pixel_count * channel_count);

  const Component or_value[] = {
    static_cast<Component>(replace_value[0] & ~keep_mask[0]),
    static_cast<Component>(replace_value[1] & ~keep_mask[1]),
    static_cast<Component>(replace_value[2] & ~keep_mask[2]),
    static_cast<Component>(replace_value[3] & ~keep_mask[3]),
  };

  const Component* s = src.buffer_.data();
  Component* d = buffer_.data();
  for (size_t pixel_index = 0; pixel_index != pixel_count; ++pixel_index) {
    for (size_t i = 0; i != channel_count; ++i) {
      const Component c = *s++;
      *d++ = (c & keep_mask[i]) | or_value[i];
    }
  }

  width_ = src.width_;
  height_ = src.height_;
  channel_count_ = src.channel_count_;
}

bool Image::ChannelEquals(ColorChannel channel, Component value) const {
  UFG_ASSERT_LOGIC(IsValid());
  if (channel >= channel_count_) {
    return true;
  }
  const Component* it = buffer_.data() + channel;
  const Component* const end = it + width_ * height_ * channel_count_;
  for (; it != end; it += channel_count_) {
    if (*it != value) {
      return false;
    }
  }
  return true;
}

std::vector<float> Image::ToFloat(bool srgb_to_linear) const {
  UFG_ASSERT_LOGIC(IsValid());
  const size_t pixel_stride = channel_count_;
  const size_t size = width_ * height_ * pixel_stride;
  UFG_ASSERT_LOGIC(size == buffer_.size());
  std::vector<float> float_buffer(size);
  const Component* src = buffer_.data();
  const Component* const src_end = src + size;
  float* dst = float_buffer.data();
  if (srgb_to_linear) {
    const float* const srgb_to_linear = kSrgbToLinearTable.linear_values;
    if (pixel_stride == kColorChannelCount) {
      // Convert RGB from sRGB to linear, preserving linear A.
      for (; src != src_end;
           src += kColorChannelCount, dst += kColorChannelCount) {
        const Component r = src[kColorChannelR];
        const Component g = src[kColorChannelG];
        const Component b = src[kColorChannelB];
        const Component a = src[kColorChannelA];
        dst[kColorChannelR] = srgb_to_linear[r];
        dst[kColorChannelG] = srgb_to_linear[g];
        dst[kColorChannelB] = srgb_to_linear[b];
        dst[kColorChannelA] = ComponentToFloat(a);
      }
    } else {
      // Convert all components from sRGB to linear.
      for (; src != src_end; ++src, ++dst) {
        *dst = srgb_to_linear[*src];
      }
    }
  } else {
    // Convert to float as-is.
    for (; src != src_end; ++src, ++dst) {
      *dst = ComponentToFloat(*src);
    }
  }
  return float_buffer;
}

void Image::CreateFromFloat(const float* data, size_t width, size_t height,
                            size_t channel_count, bool linear_to_srgb) {
  Clear();
  const size_t size = width * height * channel_count;
  buffer_.resize(size);
  const float* src = data;
  const float* const src_end = data + size;
  Component* dst = buffer_.data();
  if (linear_to_srgb) {
    if (channel_count == kColorChannelCount) {
      // Convert RGB from sRGB to linear, preserving linear A.
      for (; src != src_end;
           src += kColorChannelCount, dst += kColorChannelCount) {
        const float r = src[kColorChannelR];
        const float g = src[kColorChannelG];
        const float b = src[kColorChannelB];
        const float a = src[kColorChannelA];
        dst[kColorChannelR] = kLinearToSrgbTable.Lookup(r);
        dst[kColorChannelG] = kLinearToSrgbTable.Lookup(g);
        dst[kColorChannelB] = kLinearToSrgbTable.Lookup(b);
        dst[kColorChannelA] = FloatToComponent(a);
      }
    } else {
      // Convert all components from sRGB to linear.
      for (; src != src_end; ++src, ++dst) {
        *dst = kLinearToSrgbTable.Lookup(*src);
      }
    }
  } else {
    // Convert to int as-is.
    for (; src != src_end; ++src, ++dst) {
      *dst = FloatToComponent(*src);
    }
  }
  width_ = static_cast<uint32_t>(width);
  height_ = static_cast<uint32_t>(height);
  channel_count_ = static_cast<uint8_t>(channel_count);
}

void Image::Create1x1(const Component *color, size_t channel_count) {
  UFG_ASSERT_LOGIC(channel_count > 0 && channel_count <= kColorChannelCount);
  Clear();
  buffer_.resize(channel_count);
  std::copy(color, color + channel_count, buffer_.data());
  width_ = 1;
  height_ = 1;
  channel_count_ = static_cast<uint8_t>(channel_count);
}

void Image::CreateWxH(size_t width, size_t height,
                      const Component* color, size_t channel_count) {
  UFG_ASSERT_LOGIC(width > 0);
  UFG_ASSERT_LOGIC(height > 0);
  UFG_ASSERT_LOGIC(channel_count > 0 && channel_count <= kColorChannelCount);
  Clear();
  const size_t pixel_count = width * height;
  buffer_.resize(pixel_count * channel_count);
  Component* dst = buffer_.data();
  for (size_t i = 0; i != pixel_count; ++i) {
    for (size_t j = 0; j != channel_count; ++j) {
      *dst++ = color[j];
    }
  }
  width_ = static_cast<uint32_t>(width);
  height_ = static_cast<uint32_t>(height);
  channel_count_ = static_cast<uint8_t>(channel_count);
}

void Image::GetContents(
    Content (&out_content)[kColorChannelCount], bool fix_accidental_alpha,
    Component (&out_solid_color)[kColorChannelCount]) const {
  // Default to opaque black.
  out_content[kColorChannelR] = kContentSolid0;
  out_content[kColorChannelG] = kContentSolid0;
  out_content[kColorChannelB] = kContentSolid0;
  out_content[kColorChannelA] = kContentSolid1;
  out_solid_color[kColorChannelR] = 0;
  out_solid_color[kColorChannelG] = 0;
  out_solid_color[kColorChannelB] = 0;
  out_solid_color[kColorChannelA] = kComponentMax;

  const size_t width = width_;
  const size_t height = height_;
  const size_t channel_count = channel_count_;
  const size_t component_total = width * height * channel_count;
  if (component_total == 0) {
    return;
  }
  const Image::Component* const data = buffer_.data();

  // Determine which values are used for each component.
  uint32_t mins = 0;
  uint32_t maxs = 0;
  uint32_t others = 0;
  uint32_t varyings = 0;
  const Image::Component* const pixel_end = data + component_total;

#define GET_PIXEL_CONTENT(op, i)               \
    constexpr uint32_t kBit##i = 1 << i;       \
    const Image::Component c##i = pixel[i];    \
    if (c##i == 0) {                           \
      mins op kBit##i;                         \
    } else if (c##i == Image::kComponentMax) { \
      maxs op kBit##i;                         \
    } else {                                   \
      others op kBit##i;                       \
    }                                          \
    if (c##i != k##i) {                        \
      varyings op kBit##i;                     \
    }

  switch (channel_count) {
  case 1: {
    const Image::Component k0 = data[0];
    for (const Image::Component* pixel = data; pixel != pixel_end; pixel += 1) {
      // '=' instead of '|=' because there's only one bit and it's less work.
      GET_PIXEL_CONTENT(=, 0);
    }
    out_solid_color[0] = k0;
    break;
  }
  case 2: {
    const Image::Component k0 = data[0], k1 = data[1];
    for (const Image::Component* pixel = data; pixel != pixel_end; pixel += 2) {
      GET_PIXEL_CONTENT(|=, 0);
      GET_PIXEL_CONTENT(|=, 1);
    }
    out_solid_color[0] = k0;
    out_solid_color[1] = k1;
    break;
  }
  case 3: {
    const Image::Component k0 = data[0], k1 = data[1], k2 = data[2];
    for (const Image::Component* pixel = data; pixel != pixel_end; pixel += 3) {
      GET_PIXEL_CONTENT(|=, 0);
      GET_PIXEL_CONTENT(|=, 1);
      GET_PIXEL_CONTENT(|=, 2);
    }
    out_solid_color[0] = k0;
    out_solid_color[1] = k1;
    out_solid_color[2] = k2;
    break;
  }
  case 4: {
    // Miminum texture size for which we detect and fix accidental alpha. We
    // impose a size limit because edge pixels may contribute to a significant
    // area proportion for small textures.
    constexpr size_t kAccidentalPadding = 1;
    constexpr size_t kAccidentalSizeMin = 32;
    const bool ignore_rgba_edges = fix_accidental_alpha &&
                                   width >= kAccidentalSizeMin &&
                                   height >= kAccidentalSizeMin;

    const size_t pad = ignore_rgba_edges ? kAccidentalPadding : 0;
    const size_t y_begin = pad, y_end = height - pad;
    const size_t x_begin = pad, x_end = width - pad;
    const size_t row_stride = width * 4;
    const size_t row_len = (x_end - x_begin) * 4;
    const Image::Component* row_begin =
        data + y_begin * row_stride + x_begin * 4;
    const Image::Component k0 = row_begin[0], k1 = row_begin[1],
                           k2 = row_begin[2], k3 = row_begin[3];
    for (size_t y = y_begin; y != y_end; ++y, row_begin += row_stride) {
      const Image::Component* const row_end = row_begin + row_len;
      for (const Image::Component* pixel = row_begin; pixel != row_end;
           pixel += 4) {
        GET_PIXEL_CONTENT(|=, 0);
        GET_PIXEL_CONTENT(|=, 1);
        GET_PIXEL_CONTENT(|=, 2);
        GET_PIXEL_CONTENT(|=, 3);
      }
    }
    out_solid_color[0] = k0;
    out_solid_color[1] = k1;
    out_solid_color[2] = k2;
    out_solid_color[3] = k3;
    break;
  }
  default:
    UFG_ASSERT_LOGIC(false);
    break;
  }

  // Assign content state from what values are used.
  for (uint32_t i = 0, bit = 1; i != channel_count; ++i, bit = bit << 1) {
    if (others & bit) {
      if (varyings & bit) {
        out_content[i] = kContentVarying;
      } else {
        out_content[i] = kContentSolid;
      }
    } else if ((mins & bit) && (maxs & bit)) {
      out_content[i] = kContentBinary;
    } else if (mins & bit) {
      out_content[i] = kContentSolid0;
    } else if (maxs & bit) {
      out_content[i] = kContentSolid1;
    } else {
      // Component unused. In this case, keep the default.
    }
  }
}

void Image::NormalizeNormals() {
  // TODO: Use SIMD instructions to perform rsqrt on on 4 pixels at a
  // time.
  constexpr float kInOffset = -0.5f * kComponentMax;
  constexpr float kOutScale = 0.5f * kComponentMax;
  constexpr float kOutOffset = 0.5f * kComponentMax + 0.5f;
  const size_t channel_count = channel_count_;
  UFG_ASSERT_FORMAT(channel_count >= 3);
  Component* const pixels = buffer_.data();
  Component* const pixel_end = pixels + width_ * height_ * channel_count;
  for (Component* pixel = pixels; pixel != pixel_end; pixel += channel_count) {
    const float x = pixel[0] + kInOffset;
    const float y = pixel[1] + kInOffset;
    const float z = pixel[2] + kInOffset;
    const float m = std::sqrt(x * x + y * y + z * z);
    // Note, m can never be 0 because 0 isn't precisely expressible in the
    // source format.
    const float s = kOutScale / m;
    pixel[0] = static_cast<Image::Component>(x * s + kOutOffset);
    pixel[1] = static_cast<Image::Component>(y * s + kOutOffset);
    pixel[2] = static_cast<Image::Component>(z * s + kOutOffset);
  }
}

void Image::ApplyAlphaCutoff(Component cutoff) {
  const size_t channel_count = channel_count_;
  UFG_ASSERT_LOGIC(channel_count > kColorChannelA);
  Component* const pixels = buffer_.data();
  Component* const pixel_end = pixels + width_ * height_ * channel_count;
  for (Component* pixel = pixels; pixel != pixel_end; pixel += channel_count) {
    pixel[kColorChannelA] = pixel[kColorChannelA] >= cutoff ? kComponentMax : 0;
  }
}

void Image::Invert() {
  Component* const pixels = buffer_.data();
  Component* const end = pixels + buffer_.size();
  for (Component* it = pixels; it != end; ++it) {
    *it = kComponentMax - *it;
  }
}
}  // namespace ufg
