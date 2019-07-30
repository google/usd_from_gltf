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

#include "process/color.h"

#include "process/math.h"

namespace ufg {
const ColorF ColorF::kZero = { { { 0.0f, 0.0f, 0.0f, 0.0f } } };
const ColorF ColorF::kOne = { { { 1.0f, 1.0f, 1.0f, 1.0f } } };

// Specular+diffuse --> metallic+base conversion based on:
// https://github.com/bghgary/glTF-Tools-for-Unity/blob/master/UnityProject/Assets/Gltf/PbrUtilities.cs#L24
// Linked from here: https://github.com/AnalyticalGraphicsInc/gltf-pipeline/issues/331
constexpr float kSpecDielectric = 0.04f;
constexpr float kSpecMetalZeroTol = 0.000001f;

static float GetPerceivedBrightness(float r, float g, float b) {
  return 0.299f * r + 0.587f * g + 0.114f * b;
}

static float SolveMetallic(float dielectric_spec, float diffuse,
                           float spec_bright, float inv_spec_max) {
  if (spec_bright < dielectric_spec) {
    return 0.0f;
  }
  const float a = dielectric_spec;
  const float b = diffuse * inv_spec_max / (1 - dielectric_spec) + spec_bright -
                  2.0f * dielectric_spec;
  const float c = dielectric_spec - spec_bright;
  const float det = b * b - 4.0f * a * c;
  const float result = (-b + std::sqrt(det)) / (2.0f * a);
  return Clamp(result, 0.0f, 1.0f);
}

void SpecDiffToMetalBase(
    const float* spec, const float* diff, float* out_metal, float* out_base) {
  const float spec_r = spec[kColorChannelR];
  const float spec_g = spec[kColorChannelG];
  const float spec_b = spec[kColorChannelB];
  const float diff_r = diff[kColorChannelR];
  const float diff_g = diff[kColorChannelG];
  const float diff_b = diff[kColorChannelB];

  const float spec_max = Max3(spec_r, spec_g, spec_b);
  const float spec_bright = GetPerceivedBrightness(spec_r, spec_g, spec_b);
  const float inv_spec_max = 1.0f - spec_max;
  const float diff_bright = GetPerceivedBrightness(diff_r, diff_g, diff_b);
  const float metal =
      SolveMetallic(kSpecDielectric, diff_bright, spec_bright, inv_spec_max);
  const float inv_metal = 1.0f - metal;
  const float metal_sq = metal * metal;
  const float a =
      (1.0f - metal_sq) * inv_spec_max /
      ((1.0f - kSpecDielectric) * std::max(inv_metal, kSpecMetalZeroTol));
  const float b = metal_sq / std::max(metal, kSpecMetalZeroTol);
  const float c = -kSpecDielectric * inv_metal * b;
  const float base_r = diff_r * a + spec_r * b + c;
  const float base_g = diff_g * a + spec_g * b + c;
  const float base_b = diff_b * a + spec_b * b + c;

  *out_metal = metal;
  out_base[kColorChannelR] = base_r;
  out_base[kColorChannelG] = base_g;
  out_base[kColorChannelB] = base_b;
}
}  // namespace ufg
