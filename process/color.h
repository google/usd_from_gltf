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

#ifndef UFG_PROCESS_COLOR_H_
#define UFG_PROCESS_COLOR_H_

#include "common/common.h"
#include "common/config.h"
#include "common/logging.h"

namespace ufg {
// Channel indices, indicating RGBA component ordering in the uncompressed
// buffer.
enum ColorChannel : uint8_t {
  kColorChannelR,
  kColorChannelG,
  kColorChannelB,
  kColorChannelA,
  kColorChannelCount
};

// Floating-point color.
struct ColorF {
  union {
    float c[kColorChannelCount];
    struct {
      float r, g, b, a;
    };
  };

  static const ColorF kZero;
  static const ColorF kOne;

  void Assign(float r, float g, float b, float a) {
    this->r = r;
    this->g = g;
    this->b = b;
    this->a = a;
  }

  void Assign(const float (&rgb)[3], float a) {
    this->r = rgb[kColorChannelR];
    this->g = rgb[kColorChannelG];
    this->b = rgb[kColorChannelB];
    this->a = a;
  }

  void Assign(const float (&rgba)[4]) {
    this->r = rgba[kColorChannelR];
    this->g = rgba[kColorChannelG];
    this->b = rgba[kColorChannelB];
    this->a = rgba[kColorChannelA];
  }

  template <size_t kCount>
  void SetChannels(const float (&f)[kCount]) {
    static_assert(kCount <= kColorChannelCount, "");
    for (size_t i = 0; i != kCount; ++i) {
      c[i] = f[i];
    }
  }

  ColorF& operator=(float f) {
    r = f;
    g = f;
    b = f;
    a = f;
    return *this;
  }

  GfVec4f ToVec4() const {
    return GfVec4f(c[kColorChannelR], c[kColorChannelG], c[kColorChannelB],
                   c[kColorChannelA]);
  }
  GfVec3f ToVec3() const {
    return GfVec3f(c[kColorChannelR], c[kColorChannelG], c[kColorChannelB]);
  }
};

template <typename Vec> Vec ColorToVec(const ColorF& color);
template <>
inline GfVec4f ColorToVec<GfVec4f>(const ColorF& color) {
  return color.ToVec4();
}
template <>
inline GfVec3f ColorToVec<GfVec3f>(const ColorF& color) {
  return color.ToVec3();
}
template <>
inline float ColorToVec<float>(const ColorF& color) {
  return color.r;
}

// Integer color.
struct ColorI {
  int c[kColorChannelCount];

  static ColorI FromFloat(const float (&color)[4], uint32_t units);

  static bool Equal(const ColorI& a, const ColorI& b, size_t count) {
    UFG_ASSERT_LOGIC(count <= UFG_ARRAY_SIZE(a.c));
    for (size_t i = 0; i != count; ++i) {
      if (a.c[i] != b.c[i]) {
        return false;
      }
    }
    return true;
  }

  // Defines a strict less-than ordering, for use with comparison operators.
  static bool Less(const ColorI& a, const ColorI& b, size_t count) {
    UFG_ASSERT_LOGIC(count <= UFG_ARRAY_SIZE(a.c));
    for (size_t i = 0; i != count; ++i) {
      const int ac = a.c[i];
      const int bc = b.c[i];
      if (ac != bc) {
        return ac < bc;
      }
    }
    return false;
  }

  friend bool operator==(const ColorI& a, const ColorI& b) {
    return Equal(a, b, UFG_ARRAY_SIZE(a.c));
  }

  friend bool operator!=(const ColorI& a, const ColorI& b) {
    return !(a == b);
  }

  friend bool operator<(const ColorI& a, const ColorI& b) {
    return Less(a, b, UFG_ARRAY_SIZE(a.c));
  }
};

void SpecDiffToMetalBase(
    const float* spec, const float* diff, float* out_metal, float* out_base);
}  // namespace ufg

#endif  // UFG_PROCESS_COLOR_H_
