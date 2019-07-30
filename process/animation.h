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

#ifndef UFG_PROCESS_ANIMATION_H_
#define UFG_PROCESS_ANIMATION_H_

#include "common/common.h"
#include "common/config.h"
#include "common/logging.h"
#include "gltf/cache.h"
#include "gltf/gltf.h"
#include "process/math.h"

namespace ufg {

// Separate passes over the node hierarchy.
// This is required to deal with the different skinned transform semantics used
// by GLTF and USD:
// - In GLTF, a skinned mesh uses only the transforms in the skeleton to which
//   it's bound, and ignores the transforms containing the mesh itself.
// - In USD, the mesh transforms are applied cumulatively on top of the skeleton
//   transforms.
//   * Also note that the iOS viewer has a peculiarity that it frames the model
//     only according to the transform hierarchy, ignoring the joint hierarchy.
//     So if animation changes the model bounds drastically (e.g. if we encoded
//     the 1/100 cm scale into it), the framing may be totally off causing the
//     model to appear way too big, small, or offscreen.
// We handle this by reanchoring the mesh to the node containing its top-level
// joint.  This ensures that the original mesh transforms are not applied, and
// preserves the higher-up hierarchy transforms so the iOS viewer can (usually)
// determine the correct bounds.
enum Pass : uint8_t { kPassRigid, kPassSkinned, kPassCount };

struct NodeInfo {
  bool is_animated = false;
  bool passes_used[kPassCount] = { false, false };
  Gltf::Id root_skin_id = Gltf::Id::kNull;
  std::vector<float> translation_times;
  std::vector<GfVec3f> translation_points;
  std::vector<float> rotation_times;
  std::vector<GfQuatf> rotation_points;
  std::vector<float> scale_times;
  std::vector<GfVec3f> scale_points;

  // A list of IDs to nodes containing a mesh+skin pair for which the joint root
  // points to this node. Used to reanchor skinned meshes so they are in the
  // skeleton hierarchy (instead of the original mesh hierarchy which is unused
  // for skinned meshes).
  std::vector<Gltf::Id> skinned_node_ids;

  // Set animation keys to static (non-animated) values.
  void SetStatic(const Srt& srt);
};

struct AnimInfo {
  Gltf::Id id = Gltf::Id::kNull;
  float time_min = 0.0f;
  float time_max = 0.0f;
  std::vector<bool> nodes_animated;

  void Clear() {
    id = Gltf::Id::kNull;
    time_min = 0.0f;
    time_max = 0.0f;
    nodes_animated.clear();
  }
};

struct TranslationKey {
  using Point = GfVec3f;
  float t;
  std::vector<Point> p;

  static const std::vector<float>& GetNodeTimes(const NodeInfo& node_info) {
    return node_info.translation_times;
  }
  static const std::vector<Point>& GetNodePoints(const NodeInfo& node_info) {
    return node_info.translation_points;
  }
  static Point Blend(const Point& a, const Point& b, float s) {
    return Lerp(a, b, s);
  }
};

struct RotationKey {
  using Point = GfQuatf;
  float t;
  std::vector<Point> p;

  static const std::vector<float>& GetNodeTimes(const NodeInfo& node_info) {
    return node_info.rotation_times;
  }
  static const std::vector<Point>& GetNodePoints(const NodeInfo& node_info) {
    return node_info.rotation_points;
  }
  static Point Blend(const Point& a, const Point& b, float s) {
    return GfSlerp(a, b, s);
  }
};

struct ScaleKey {
  using Point = GfVec3f;
  float t;
  std::vector<Point> p;

  static const std::vector<float>& GetNodeTimes(const NodeInfo& node_info) {
    return node_info.scale_times;
  }
  static const std::vector<Point>& GetNodePoints(const NodeInfo& node_info) {
    return node_info.scale_points;
  }
  static Point Blend(const Point& a, const Point& b, float s) {
    return Lerp(a, b, s);
  }
};

template <typename Point>
struct SeparatePrunerStream {
  const float* src_times;
  const Point* src_points;
  std::vector<float> times;
  std::vector<Point> points;

  SeparatePrunerStream(const float* src_times, const Point* src_points)
      : src_times(src_times), src_points(src_points) {}

  void SetKey(size_t src_index, size_t dst_index) {
    times[dst_index] = src_times[src_index];
    points[dst_index] = src_points[src_index];
  }

  void Resize(size_t size) {
    times.resize(size);
    points.resize(size);
  }

  float GetTime(size_t i) const {
    return src_times[i];
  }
};

struct TranslationPrunerStream : SeparatePrunerStream<GfVec3f> {
  TranslationPrunerStream(const float* src_times, const GfVec3f* src_points)
      : SeparatePrunerStream(src_times, src_points) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

struct EulerPrunerStream : SeparatePrunerStream<GfVec3f> {
  EulerPrunerStream(const float* src_times, const GfVec3f* src_points)
      : SeparatePrunerStream(src_times, src_points) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

struct QuatPrunerStream : SeparatePrunerStream<GfQuatf> {
  QuatPrunerStream(const float* src_times, const GfQuatf* src_points)
      : SeparatePrunerStream(src_times, src_points) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

struct ScalePrunerStream : SeparatePrunerStream<GfVec3f> {
  ScalePrunerStream(const float* src_times, const GfVec3f* src_points)
      : SeparatePrunerStream(src_times, src_points) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

template <typename Key>
struct KeyPrunerStream {
  const Key* src_keys;
  std::vector<Key> keys;

  explicit KeyPrunerStream(const Key* src_keys) : src_keys(src_keys) {}

  void SetKey(size_t src_index, size_t dst_index) {
    keys[dst_index] = src_keys[src_index];
  }

  void Resize(size_t size) {
    keys.resize(size);
  }

  float GetTime(size_t i) const {
    return src_keys[i].t;
  }
};

struct TranslationKeyPrunerStream : KeyPrunerStream<TranslationKey> {
  explicit TranslationKeyPrunerStream(const TranslationKey* src_keys)
      : KeyPrunerStream(src_keys) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

struct RotationKeyPrunerStream : KeyPrunerStream<RotationKey> {
  explicit RotationKeyPrunerStream(const RotationKey* src_keys)
      : KeyPrunerStream(src_keys) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

struct ScaleKeyPrunerStream : KeyPrunerStream<ScaleKey> {
  explicit ScaleKeyPrunerStream(const ScaleKey* src_keys)
      : KeyPrunerStream(src_keys) {}
  bool ShouldPrune(size_t i0, size_t i1, size_t i2, float s) const;
  bool IsPrunedConstant() const;
};

void PropagatePassesUsed(Gltf::Id node_id, const Gltf::Node* nodes,
                         NodeInfo* node_infos);
AnimInfo GetAnimInfo(const Gltf& gltf, Gltf::Id anim_id, GltfCache* gltf_cache);
void SanitizeRotations(size_t count, GfQuatf* quats);
std::vector<const NodeInfo*> GetJointNodeInfos(
    const std::vector<Gltf::Id>& joint_to_node_map,
    const std::vector<NodeInfo>& node_infos);

template <typename Key>
void GenerateSkinAnimKeys(
    size_t joint_count, const NodeInfo* const* joint_infos,
    std::vector<Key>* out_keys);

template <typename PrunerStream>
void PruneAnimationKeys(size_t src_count, PrunerStream* stream);


struct TranslationKeyConverter {
  using Point = GfVec3f;
  static bool ShouldPrune(
      const Point& p0, const Point& p1, const Point& p2, float s);
};

struct QuatKeyConverter {
  using Point = GfQuatf;
  static bool ShouldPrune(
      const Point& p0, const Point& p1, const Point& p2, float s);
};

struct ScaleKeyConverter {
  using Point = GfVec3f;
  static bool ShouldPrune(
      const Point& p0, const Point& p1, const Point& p2, float s);
};

template <typename Converter>
void ConvertAnimKeysToLinear(
    Gltf::Animation::Sampler::Interpolation interpolation,
    std::vector<float>* times, std::vector<typename Converter::Point>* points);

}  // namespace ufg

#endif  // UFG_PROCESS_ANIMATION_H_
