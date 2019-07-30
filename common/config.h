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

#ifndef UFG_COMMON_CONFIG_H_
#define UFG_COMMON_CONFIG_H_

#include "common/common.h"
#include "gltf/load.h"

namespace ufg {
using PXR_NS::GfVec3f;
using PXR_NS::GfVec4f;

template <typename Scalar>
struct Constants {
  static constexpr Scalar kPi = static_cast<Scalar>(M_PI);
  static constexpr Scalar kHalfPi = kPi / 2;
  static constexpr Scalar kTwoPi = 2 * kPi;
  static constexpr Scalar kRecipTwoPi = 1 / kTwoPi;
  static constexpr Scalar kRadToDeg = 180 / kPi;
  static constexpr Scalar kDegToRad = kPi / 180;
  static constexpr Scalar kAcosTol = static_cast<Scalar>(0.9999999);
};

// We eliminate skin influences with weights less than this.
constexpr float kSkinWeightZeroTol = 0.01f;

// DT considered too small to calculate key interpolants.
constexpr float kAnimDtMin = 0.00001f;

// Tolerances used to prune redundant animation keys.
constexpr float kPruneTranslationProportionalSq = Square(0.01f);
constexpr float kPruneTranslationAbsoluteSq = Square(0.001f);
constexpr float kPruneRotationComponent = 0.01f * Constants<float>::kDegToRad;
constexpr float kPruneScaleComponent = 0.001f;

// Animation frames per second.
// * This number is fairly arbitrary since it only affects the scale of time
//   codes (which need not be integers), so should only be apparent when viewing
//   the model in usdview. I've chosen 120 as the LCM of 24, 30, and 60 so that
//   keys will usually be integers.
// TODO: We could try to infer a frame rate from animation DTs (e.g.
// round(1/min(all dts)))
constexpr float kFps = 120.0f;

// Tolerance to snap animation time codes to integer values.
constexpr float kSnapTimeCodeTol = 0.001f / kFps;

extern const GfVec3f kColorBlack;

// Tolerance used to compare color components.
constexpr float kColorTol = 0.001f;

// Fallback values for material inputs.
extern const GfVec4f kFallbackBase;
extern const GfVec3f kFallbackDiffuse;
extern const GfVec3f kFallbackNormal;
extern const GfVec3f kFallbackEmissive;
extern const GfVec3f kFallbackSpecular;
constexpr float kFallbackMetallic = 0.0f;
constexpr float kFallbackRoughness = 1.0f;
constexpr float kFallbackOcclusion = 1.0f;

struct ImageResizeSettings {
  static constexpr size_t kDefaultSizeMin = 1;
  static constexpr size_t kDefaultSizeMax = 16 * 1024;
  bool force_power_of_2 = false;
  float scale = 1.0f;
  uint32_t size_min = kDefaultSizeMin;
  uint32_t size_max = kDefaultSizeMax;

  bool IsDefault() const {
    return force_power_of_2 == false && scale == 1.0f &&
           size_min == kDefaultSizeMin && size_max == kDefaultSizeMax;
  }
};

struct ConvertSettings {
  // If set, export all nodes rather than a single scene.
  // * This overrides scene_index.
  bool all_nodes = false;

  // If set, export this scene index rather than the default specified in the
  // glTF.
  // * This has no effect if all_nodes is set.
  // * If this exceeds the number of scenes in the glTF file, the default scene
  //   is used.
  int scene_index = -1;

  // Set the animation to export.
  // * If this is not a valid index, animation export is disabled.
  // TODO: Add mode to merge multiple/all animations into the output.
  int anim_index = 0;

  // Apply root model scale. This is 100 by default to convert from meters(glTF)
  // to centimeters(USD).
  float root_scale = 100.0f;

  // Adjust scale so the scene bounding-box is no larger than this in any
  // dimension. Values <= 0 indicate no limit.
  float limit_bounds = 0.0f;

  // JPG compression quality [1=worst, 100=best].
  // * The practical range appears to be between 75-90: <75 and there isn't
  //   significant memory savings, and >90 the images become very large.
  uint8_t jpg_quality = 85;

  // JPG compression quality, for normal-maps [1=worst, 100=best].
  // * This should usually be larger than jpg_quality, because normal-maps are
  //   more sensitive to errors.
  uint8_t jpg_quality_norm = 96;

  // JPG chroma subsampling method [0=best, 2=worst].
  // * This is only applicable to RGB textures. Grayscale and normal-map
  //   textures do not use chroma subsampling.
  // * This allows chrominance to be stored at a lower resolution than luminance
  //   (the former being generally less perceptible).
  //   See: https://en.wikipedia.org/wiki/Chroma_subsampling
  //   0 -> 4:4:4 - No subsampling.
  //   1 -> 4:2:2 - One chroma value per 2x1 pixel block.
  //   2 -> 4:2:0 - One chroma value per 2x2 pixel block.
  // * This appears to have far less influence on file size than jpg_quality.
  uint8_t jpg_subsamp = 0;

  // PNG compression level [0=fastest, 9=smallest].
  uint8_t png_level = 9;

  // Explicit image size settings.
  ImageResizeSettings image_resize;

  // Limit the total size of all images after decompression. If the total
  // exceeds this limit, images will be uniformly scaled to fit within the
  // limit.
  // * Images will be scaled per-axis by powers of the scale increment, so the
  //   resized total will only coarsely match this size.
  // * The default setting was chosen empirically to be less than the limit on
  //   the iOS viewer. It's undocumented, but appears to be ~200MB, accounting
  //   for waste due to mip-mapping and alignment.
  // * Set to 0 to disable this limit.
  uint32_t limit_total_image_decompressed_size = 160 * 1024 * 1024;

  // When limiting total image size, reduce per-axis scale by this amount until
  // we find a total that fits.
  // * A setting of 1/2 is good for preserving power-of-2 texture sizes, but it
  //   makes scaling very coarse (reduced total will be between 25-100% of the
  //   limit).
  // * Max value is 0.5, but we continue to scale down exponentially until we
  //   find a total that fits (or we reach the global size_min for all
  //   textures). For instance, with a factor of 0.25, we will try:
  //   0.75, 0.5, 0.25, 0.25*0.75, 0.25*0.5, 0.25*0.25, 0.25*0.25*0.75, ...
  float limit_total_image_scale_step = 0.5f;

  // Add debug meshes to each transform node to visualize animation. This is
  // useful for debugging skinned animations in Usdview, because it doesn't
  // support skinning.
  bool add_debug_bone_meshes = false;

  // Bake texture color scale and bias into the texture, because the iOS viewer
  // currently ignores these fields.
  bool bake_texture_color_scale_bias = true;

  // Bake alpha cutoff into textures, because it's unsupported by the USD
  // preview surface spec.
  bool bake_alpha_cutoff = true;

  // Bake skinned vertex normals to the first frame of animation. This is a
  // work-around for the iOS viewer because it doesn't skin normals. It's not
  // correct, but looks a lot better for models where the animation is very
  // different from the bind-pose.
  // TODO: If the iOS viewer is ever fixed, this flag will cause
  // incorrect lighting and should be disabled.
  bool bake_skin_normals = true;

  // If the occlusion channel is pure black, replace it with pure white. This
  // works around a disparity in the iOS viewer: occlusion is used to modulate
  // the output color rather than just the ambient, so if occlusion is pure
  // black it will cause the whole material to be black.
  bool black_occlusion_is_white = true;

  // Delete unused intermediate output files.
  bool delete_unused = true;

  // Delete all generated files except the output usda/usdz file. Note, this
  // will render usda files unusable due to missing dependencies, so it's mostly
  // useful for testing.
  bool delete_generated = false;

  // For compatibility with the iOS viewer (which doesn't support multiple UV
  // sets), replace textures referencing secondary UV sets with a solid color.
  // This works reasonably well for our most common use-case: AO, which will be
  // replaced with solid white.
  bool disable_multiple_uvsets = true;

  // Output emissive textures in linear space. Emissive textures should be sRGB,
  // but the iOS viewer treats them as linear. Without this work around,
  // emissive textures will appear washed-out.
  bool emissive_is_linear = true;

  // If set, emulate double-sided geometry by duplicating single-sided geometry.
  // This is necessary for compatibility with Apple's viewer which doesn't
  // currently respect the 'doubleSided' mesh attribute.
  bool emulate_double_sided = true;

  // If set, convert diffuse+specular+glossiness to an approximation of
  // albedo+metallic+roughness. This is necessary for compatibility with the iOS
  // viewer which doesn't currently respect the 'useSpecularWorkflow' material
  // attribute.
  bool emulate_specular_workflow = true;

  // Fix accidental alpha (e.g. transparency introduced due to resizing in
  // Photoshop) by ignoring edge pixels during texture classification.
  bool fix_accidental_alpha = true;

  // Work around iOS viewer not skinning normals. A true fix is not possible,
  // but this may look better (possibly at the cost of larger animation size).
  bool fix_skinned_normals = true;

  // Merge materials with identical parameters, irrespective of material name.
  bool merge_identical_materials = true;

  // Merge multiple skeletons into one. This is necessary for compatibility with
  // Apple's viewer which doesn't support multiple skeletons.
  bool merge_skeletons = true;

  // The iOS renderer doesn't internally normalize normal map vectors, so we do
  // it.
  bool normalize_normals = true;

  // Normalize the skin root joint scale to 1.0 by applying it to the parent
  // transform. This is a work around for Apple's viewer which does not take
  // into account animation scale when computing model bounds.
  bool normalize_skin_scale = true;

  // If set, prefer saving images as jpeg. All images except those requiring
  // transparency will be converted to jpeg.
  bool prefer_jpeg = true;

  // Print conversion time stats.
  bool print_timing = false;

  // Remove geometry that's invisible due to material state. This saves space,
  // and on iOS fixes cases where invisible geometry shows up anyway due to
  // lighting.
  bool remove_invisible = true;

  // Remove nodes matching any of these prefixes (case insensitive).
  std::vector<std::string> remove_node_prefixes;

  // The iOS viewer doesn't correctly support inverse scale, causing incorrect
  // culling. When set, we reverse polygon winding during conversion to
  // compensate for inverse scale. Note, this only works for statically-inverted
  // transforms - animating to an inverse scale will still break.
  bool reverse_culling_for_inverse_scale = true;

  // Emit warnings for features that are incompatible with the iOS viewer (such
  // as texture sampling states, vertex colors, multiple UV sets, etc). These
  // will build correctly, but display incorrectly in the iOS viewer.
  bool warn_ios_incompat = true;

  // Settings specific to the glTF loader.
  GltfLoadSettings gltf_load_settings;

  // Paths to USD plugins (may contain wildcards). If unset, the plugins are
  // loaded using the system path.
  std::string plugin_path;

  static const ConvertSettings kDefault;
};

}  // namespace ufg

#endif  // UFG_COMMON_CONFIG_H_
