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

#include "process/animation.h"

#include "process/access.h"
#include "process/process_util.h"

namespace ufg {
namespace {
// Find the next key time index that is <= t.
inline int FindNextTimeBefore(const std::vector<float>& times, int start,
                              float t) {
  const int count = static_cast<int>(times.size());
  int i1 = start + 1;
  for (; i1 < count && times[i1] <= t; ++i1) {}
  return i1 - 1;
}

inline bool ShouldPruneTranslation(
    const GfVec3f& p0, const GfVec3f& p1, const GfVec3f& p2, float s) {
  // Get error tolerance proportional to the distances between interpolation
  // points.
  const GfVec3f offset01 = p1 - p0;
  const GfVec3f offset02 = p2 - p0;
  const float dist01_sq = offset01.GetLengthSq();
  const float dist02_sq = offset02.GetLengthSq();
  const float dist_max_sq = std::max(dist01_sq, dist02_sq);
  const float proportional_tol_sq =
      dist_max_sq * kPruneTranslationProportionalSq;

  // Get the error between p1 and the linearly-interpolated point.
  const GfVec3f offset = offset02 * s;
  const GfVec3f diff = offset - offset01;
  const float error_sq = diff.GetLengthSq();

  // Prune if the error is small enough.
  return error_sq <= proportional_tol_sq ||
         error_sq <= kPruneTranslationAbsoluteSq;
}

inline bool ShouldPruneEuler(
    const GfVec3f& p0, const GfVec3f& p1, const GfVec3f& p2, float s) {
  const GfVec3f p = Lerp(p0, p2, s);
  return NearlyEqual(p, p1, kPruneRotationComponent);
}

inline bool ShouldPruneQuat(
    const GfQuatf& p0, const GfQuatf& p1, const GfQuatf& p2, float s) {
  // Don't prune angles near 180º, as that can cause ambiguous interpolation
  // direction.
  static constexpr float kAngleMax = 0.99f * Constants<float>::kPi;
  const float d02 = GetQuatDeltaAngle(p0, p2);
  if (d02 > kAngleMax) {
    return false;
  }
  const GfQuatf p = GfSlerp(p0, p2, s);
  const float e = GetQuatAbsMinDeltaAngle(p, p1);
  return e < kPruneRotationComponent;
}

inline bool ShouldPruneScale(
    const GfVec3f& p0, const GfVec3f& p1, const GfVec3f& p2, float s) {
  const GfVec3f p = Lerp(p0, p2, s);
  return NearlyEqual(p, p1, kPruneScaleComponent);
}

void GetAnimationTimeRange(
    const Gltf& gltf, const Gltf::Animation& animation,
    GltfCache* gltf_cache, float* out_min, float* out_max) {
  float anim_min = std::numeric_limits<float>::max();
  float anim_max = std::numeric_limits<float>::lowest();
  for (const Gltf::Animation::Sampler& sampler : animation.samplers) {
    size_t time_count, component_count;
    const float* const times =
        gltf_cache->Access<float>(sampler.input, &time_count, &component_count);
    UFG_ASSERT_FORMAT(times);
    UFG_ASSERT_FORMAT(time_count > 0);
    UFG_ASSERT_FORMAT(component_count == 1);
    const float time_min = times[0];
    const float time_max = times[time_count - 1];
    anim_min = std::min(anim_min, time_min);
    anim_max = std::max(anim_max, time_max);
  }
  *out_min = anim_min;
  *out_max = anim_max;
}
}  // namespace

void NodeInfo::SetStatic(const Srt& srt) {
  translation_times.clear();
  rotation_times.clear();
  scale_times.clear();
  translation_points.assign({ srt.translation });
  rotation_points.assign({ srt.rotation });
  scale_points.assign({ srt.scale });
}

inline bool TranslationPrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  return ShouldPruneTranslation(
      src_points[i0], src_points[i1], src_points[i2], s);
}

bool TranslationPrunerStream::IsPrunedConstant() const {
  if (times.size() != 2) {
    return times.size() < 2;
  }
  const GfVec3f offset = points[1] - points[0];
  return offset.GetLengthSq() <= kPruneTranslationAbsoluteSq;
}

inline bool EulerPrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  return ShouldPruneEuler(
      src_points[i0], src_points[i1], src_points[i2], s);
}

bool EulerPrunerStream::IsPrunedConstant() const {
  if (times.size() != 2) {
    return times.size() < 2;
  }
  return NearlyEqual(points[0], points[1], kPruneRotationComponent);
}

inline bool QuatPrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  return ShouldPruneQuat(
      src_points[i0], src_points[i1], src_points[i2], s);
}

bool QuatPrunerStream::IsPrunedConstant() const {
  if (times.size() != 2) {
    return times.size() < 2;
  }
  return GetQuatAbsMinDeltaAngle(points[0], points[1]) <
         kPruneRotationComponent;
}

inline bool ScalePrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  const GfVec3f& p0 = src_points[i0];
  const GfVec3f& p1 = src_points[i1];
  const GfVec3f& p2 = src_points[i2];
  const GfVec3f p = Lerp(p0, p2, s);
  return NearlyEqual(p, p1, kPruneScaleComponent);
}

bool ScalePrunerStream::IsPrunedConstant() const {
  if (times.size() != 2) {
    return times.size() < 2;
  }
  return NearlyEqual(points[0], points[1], kPruneScaleComponent);
}

inline bool TranslationKeyPrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  const TranslationKey& k0 = src_keys[i0];
  const TranslationKey& k1 = src_keys[i1];
  const TranslationKey& k2 = src_keys[i2];
  for (size_t i = 0, count = k0.p.size(); i != count; ++i) {
    if (!ShouldPruneTranslation(k0.p[i], k1.p[i], k2.p[i], s)) {
      return false;
    }
  }
  return true;
}

bool TranslationKeyPrunerStream::IsPrunedConstant() const {
  if (keys.size() != 2) {
    return keys.size() < 2;
  }
  const TranslationKey& k0 = keys[0];
  const TranslationKey& k1 = keys[1];
  for (size_t i = 0, count = k0.p.size(); i != count; ++i) {
    const GfVec3f offset = k1.p[i] - k0.p[i];
    if (offset.GetLengthSq() > kPruneTranslationAbsoluteSq) {
      return false;
    }
  }
  return true;
}

inline bool RotationKeyPrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  const RotationKey& k0 = src_keys[i0];
  const RotationKey& k1 = src_keys[i1];
  const RotationKey& k2 = src_keys[i2];
  for (size_t i = 0, count = k0.p.size(); i != count; ++i) {
    const GfQuatf& p0 = k0.p[i];
    const GfQuatf& p1 = k1.p[i];
    const GfQuatf& p2 = k2.p[i];
    // Note we use Nlerp instead of Slerp to match the iOS viewer. Nlerp is less
    // accurate, so we can't prune as effectively.
    const GfQuatf p = Nlerp(p0, p2, s);
    const float e = GetQuatAbsMinDeltaAngle(p, p1);
    if (e > kPruneRotationComponent) {
      return false;
    }
  }
  return true;
}

bool RotationKeyPrunerStream::IsPrunedConstant() const {
  if (keys.size() != 2) {
    return keys.size() < 2;
  }
  const RotationKey& k0 = keys[0];
  const RotationKey& k1 = keys[1];
  for (size_t i = 0, count = k0.p.size(); i != count; ++i) {
    if (GetQuatAbsMinDeltaAngle(k0.p[i], k1.p[i]) > kPruneRotationComponent) {
      return false;
    }
  }
  return true;
}

inline bool ScaleKeyPrunerStream::ShouldPrune(
    size_t i0, size_t i1, size_t i2, float s) const {
  const ScaleKey& k0 = src_keys[i0];
  const ScaleKey& k1 = src_keys[i1];
  const ScaleKey& k2 = src_keys[i2];
  for (size_t i = 0, count = k0.p.size(); i != count; ++i) {
    const GfVec3f& p0 = k0.p[i];
    const GfVec3f& p1 = k1.p[i];
    const GfVec3f& p2 = k2.p[i];
    const GfVec3f p = Lerp(p0, p2, s);
    if (!NearlyEqual(p, p1, kPruneScaleComponent)) {
      return false;
    }
  }
  return true;
}

bool ScaleKeyPrunerStream::IsPrunedConstant() const {
  if (keys.size() != 2) {
    return keys.size() < 2;
  }
  const ScaleKey& k0 = keys[0];
  const ScaleKey& k1 = keys[1];
  for (size_t i = 0, count = k0.p.size(); i != count; ++i) {
    if (!NearlyEqual(k0.p[i], k1.p[i], kPruneScaleComponent)) {
      return false;
    }
  }
  return true;
}

void PropagatePassesUsed(
    Gltf::Id node_id, const Gltf::Node* nodes, NodeInfo* node_infos) {
  const size_t node_index = Gltf::IdToIndex(node_id);
  const Gltf::Node& node = nodes[node_index];
  NodeInfo& node_info = node_infos[node_index];
  for (const Gltf::Id child_id : node.children) {
    PropagatePassesUsed(child_id, nodes, node_infos);
    const NodeInfo& child_node_info = node_infos[Gltf::IdToIndex(child_id)];
    for (size_t pass = 0; pass != kPassCount; ++pass) {
      if (child_node_info.passes_used[pass]) {
        node_info.passes_used[pass] = true;
      }
    }
  }
}

AnimInfo GetAnimInfo(
    const Gltf& gltf, Gltf::Id anim_id, GltfCache* gltf_cache) {
  AnimInfo anim_info;
  const Gltf::Animation* const anim = Gltf::GetById(gltf.animations, anim_id);
  UFG_ASSERT_LOGIC(anim);
  anim_info.id = anim_id;
  GetAnimationTimeRange(gltf, *anim, gltf_cache,
                        &anim_info.time_min, &anim_info.time_max);
  anim_info.nodes_animated.resize(gltf.nodes.size(), false);
  const size_t channel_count = anim->channels.size();
  for (size_t ci = 0; ci != channel_count; ++ci) {
    const Gltf::Animation::Channel& channel = anim->channels[ci];
    if (channel.target.node != Gltf::Id::kNull) {
      MarkAffectedNodes(gltf.nodes, channel.target.node,
                        &anim_info.nodes_animated);
    }
  }
  return anim_info;
}

void SanitizeRotations(size_t count, GfQuatf* quats) {
  if (count == 0) {
    return;
  }
  quats[0].Normalize();
  if (count == 1) {
    return;
  }

  GfQuatf* const quat_end = quats + count;
  for (GfQuatf* q1 = quats + 1; q1 != quat_end; ++q1) {
    GfQuatf* q0 = q1 - 1;
    q1->Normalize();

    // The iOS viewer doesn't ensure quaternions are interpolated along the
    // minimal arc, so we need to do this here.
    const float delta = GetQuatHalfCosDeltaAngle(*q0, *q1);
    if (delta < 0.0f) {
      *q1 = -*q1;
    }
  }
}

std::vector<const NodeInfo*> GetJointNodeInfos(
    const std::vector<Gltf::Id>& joint_to_node_map,
    const std::vector<NodeInfo>& node_infos) {
  const size_t joint_count = joint_to_node_map.size();
  std::vector<const NodeInfo*> joint_infos(joint_count);
  for (size_t joint_index = 0; joint_index != joint_count; ++joint_index) {
    const size_t node_index = Gltf::IdToIndex(joint_to_node_map[joint_index]);
    joint_infos[joint_index] = &node_infos[node_index];
  }
  return joint_infos;
}

template <typename Key>
void GenerateSkinAnimKeys(
    size_t joint_count, const NodeInfo* const* joint_infos,
    std::vector<Key>* out_keys) {
  using Point = typename Key::Point;

  // TODO: Add special handling for looping by blending between the
  // end and start keys.
  // * The looping behavior is ambiguous if joints have different time ranges
  //   from each other (where do we loop? we can choose the time range for the
  //   whole file, for the specific animation, or for the specific channel). I'm
  //   not sure if the GLTF spec defines this, so maybe it just never happens in
  //   practice.

  static constexpr float kTimeMax = std::numeric_limits<float>::max();
  UFG_ASSERT_LOGIC(joint_count > 0);

  std::vector<Key> keys;
  std::vector<int> src_its(joint_count, -1);
  for (;;) {
    // Find the next source key time >t.
    float t = kTimeMax;
    for (size_t joint_index = 0; joint_index != joint_count; ++joint_index) {
      const NodeInfo& info = *joint_infos[joint_index];
      const std::vector<float>& times = Key::GetNodeTimes(info);
      const size_t next_src_it = src_its[joint_index] + 1;
      if (next_src_it < times.size()) {
        t = std::min(t, times[next_src_it]);
      }
    }

    // If we didn't find a next time value, we're at the end of the sequence.
    if (t == kTimeMax) {
      break;
    }

    // Generate new key by evaluating the animation at this time.
    keys.push_back(Key());
    Key& key = keys.back();
    key.t = t;
    key.p.resize(joint_count);
    for (size_t joint_index = 0; joint_index != joint_count; ++joint_index) {
      const NodeInfo& info = *joint_infos[joint_index];

      // Find the source key range bounding the current time.
      const std::vector<float>& times = Key::GetNodeTimes(info);
      const int i0 = FindNextTimeBefore(times, src_its[joint_index], t);
      src_its[joint_index] = i0;

      // Interpolate between source points bounding the current time.
      const size_t i1 = i0 + 1;
      const std::vector<Point>& points = Key::GetNodePoints(info);
      Point p;
      if (i0 < 0) {
        p = points[i1];
      } else if (i1 >= times.size()) {
        UFG_ASSERT_LOGIC(i0 < points.size());
        p = points[i0];
      } else {
        const float t0 = times[i0];
        const float t1 = times[i1];
        const float dt = t1 - t0;
        const float s = dt < kAnimDtMin ? 0.0f : (t - t0) / dt;
        p = Key::Blend(points[i0], points[i1], s);
      }
      key.p[joint_index] = p;
    }
  }

  out_keys->swap(keys);
}

template void GenerateSkinAnimKeys(size_t joint_count,
    const NodeInfo* const* joint_infos, std::vector<TranslationKey>* out_keys);
template void GenerateSkinAnimKeys(size_t joint_count,
    const NodeInfo* const* joint_infos, std::vector<RotationKey>* out_keys);
template void GenerateSkinAnimKeys(size_t joint_count,
    const NodeInfo* const* joint_infos, std::vector<ScaleKey>* out_keys);

template <typename PrunerStream>
void PruneAnimationKeys(size_t src_count, PrunerStream* stream) {
  UFG_ASSERT_LOGIC(src_count > 0);
  if (src_count == 1) {
    stream->Resize(1);
    stream->SetKey(0, 0);
    return;
  }
  size_t dst_count = 0;
  stream->Resize(src_count);
  stream->SetKey(0, dst_count++);

  // Find runs of keys for which all points between those keys can be linearly
  // interpolated, and prune the interior keys. [i_begin, i_end] denotes the run
  // range. Each time through the outer loop we increase the length of the run
  // by incrementing i_end, and only move forward i_begin when we complete the
  // run by adding a new non-pruned key to the destination.
  //
  // Note, we need to check all points between (i_begin, i_end) because
  // interpolation error may accumulate. For example, a tessellated circle may
  // have small interpolation error between successive points, but very large
  // error linearly interpolating between opposite ends of the circle.
  //
  // TODO: This is O(n²), which could be prohibitive for very long
  // animations with a lot of redundant keys. We might get away with an O(n)
  // implementation that samples just a subset (e.g. the midpoint and the point
  // to be pruned).
  size_t i_begin = 0;
  for (size_t i_end = 2; i_end != src_count; ++i_end) {
    const float t_begin = stream->GetTime(i_begin);
    const float t_end = stream->GetTime(i_end);
    const float dt = t_end - t_begin;
    bool prune;
    if (dt <= kAnimDtMin) {
      // We cannot prune keys with very small DTs because they may represent a
      // discontinuity.
      prune = false;
    } else {
      // Check that all points between i_begin and i_end can be pruned.
      prune = true;
      const float recip_dt = 1.0f / dt;
      for (size_t i = i_begin + 1; i != i_end; ++i) {
        const float t = stream->GetTime(i);
        const float s = (t - t_begin) * recip_dt;
        if (!stream->ShouldPrune(i_begin, i, i_end, s)) {
          prune = false;
          break;
        }
      }
    }

    // Add non-pruned keys to the output.
    if (!prune) {
      // The end of the current run is the beginning of the next.
      i_begin = i_end - 1;
      stream->SetKey(i_begin, dst_count++);
    }
  }

  stream->SetKey(src_count - 1, dst_count++);
  stream->Resize(dst_count);
}

template void PruneAnimationKeys(
    size_t src_count, TranslationPrunerStream* stream);
template void PruneAnimationKeys(
    size_t src_count, EulerPrunerStream* stream);
template void PruneAnimationKeys(
    size_t src_count, QuatPrunerStream* stream);
template void PruneAnimationKeys(
    size_t src_count, ScalePrunerStream* stream);
template void PruneAnimationKeys(
    size_t src_count, TranslationKeyPrunerStream* stream);
template void PruneAnimationKeys(
    size_t src_count, RotationKeyPrunerStream* stream);
template void PruneAnimationKeys(
    size_t src_count, ScaleKeyPrunerStream* stream);

bool TranslationKeyConverter::ShouldPrune(
    const Point& p0, const Point& p1, const Point& p2, float s) {
  return ShouldPruneTranslation(p0, p1, p2, s);
}

bool QuatKeyConverter::ShouldPrune(
    const Point& p0, const Point& p1, const Point& p2, float s) {
  return ShouldPruneQuat(p0, p1, p2, s);
}

bool ScaleKeyConverter::ShouldPrune(
    const Point& p0, const Point& p1, const Point& p2, float s) {
  return ShouldPruneScale(p0, p1, p2, s);
}

// Cubic splines have 3 points per key to define curve gradients.
// https://github.com/KhronosGroup/glTF/blob/master/specification/2.0/README.md#appendix-c-spline-interpolation
enum SplineElement : uint8_t {
  kSplineElementInTangent,
  kSplineElementPoint,
  kSplineElementOutTangent,
  kSplineElementCount
};

template <typename Vec>
Vec EvalSpline(const Vec& p0, const Vec& m0,
               const Vec& p1, const Vec& m1, float t) {
  const float t2 = t * t;
  const float t3 = t2 * t;
  const float a = 2.0f * t3 - 3.0f * t2 + 1.0f;
  const float b = t3 - 2.0f * t2 + t;
  const float c = 3.0f * t2 - 2.0f * t3;
  const float d = t3 - t2;
  return a * p0 + b * m0 + c * p1 + d * m1;
}

GfVec3f SampleSpline(const GfVec3f* key0, float t0,
                     const GfVec3f* key1, float t1, float s) {
  const float dt = t1 - t0;
  const GfVec3f& p0 = key0[kSplineElementPoint];
  const GfVec3f& p1 = key1[kSplineElementPoint];
  const GfVec3f m0 = dt * key0[kSplineElementOutTangent];
  const GfVec3f m1 = dt * key1[kSplineElementInTangent];
  return EvalSpline(p0, m0, p1, m1, s);
}

GfQuatf SampleSpline(const GfQuatf* key0, float t0,
                     const GfQuatf* key1, float t1, float s) {
  const float dt = t1 - t0;
  const GfQuatf& p0 = key0[kSplineElementPoint];
  const GfQuatf m0 = dt * key0[kSplineElementOutTangent];

  // Interpolate along the minimal arc.
  GfQuatf p1 = key1[kSplineElementPoint];
  GfQuatf m1 = dt * key1[kSplineElementInTangent];
  const float delta = GetQuatHalfCosDeltaAngle(p0, p1);
  if (delta < 0.0f) {
    p1 = -p1;
    m1 = -m1;
  }

  return EvalSpline(p0, m0, p1, m1, s);
}

template <typename Converter>
void AddSplinePoints(float t0, const typename Converter::Point* key0,
                     float t1, const typename Converter::Point* key1,
                     std::vector<float>* times,
                     std::vector<typename Converter::Point>* points) {
  using Point = typename Converter::Point;

  // Sampling frame-rate used for the linear search. Larger values are slower,
  // but result in a tighter fit.
  constexpr float kSampleFps = 300.0f;
  constexpr float kStepMin = 0.1f;
  constexpr float kStepMinDt = 1.0f / (kSampleFps * kStepMin);
  const float dt = t1 - t0;
  const float s_step =
      dt < kStepMinDt ? kStepMin : (kStepMin * kStepMinDt) / dt;

  // Add points in fixed intervals, pruning redundant ones as we go.
  // The current segment is in the fractional range [s_begin, s_end], and we
  // iterate s_end forward through each iteration of the loop. When we find a
  // new point, we add it and set s_begin to s_end to start a new segment.
  // TODO: We could do this in fewer steps and find a better fit using
  // bisection searches.
  float s_begin = 0.0f;
  float s_end = s_step;
  Point p_begin = key0[kSplineElementPoint];
  Point p_end = SampleSpline(key0, t0, key1, t1, s_end);
  do {
    const float next_s_end = std::min(s_end + s_step, 1.0f);
    const float next_s_mid = 0.5f * (s_begin + next_s_end);
    const Point next_p_mid = SampleSpline(key0, t0, key1, t1, next_s_mid);
    const Point next_p_end = SampleSpline(key0, t0, key1, t1, next_s_end);
    if (!Converter::ShouldPrune(p_begin, next_p_mid, next_p_end, 0.5f)) {
      // Error exceeds the tolerance. Add a new point and start a new segment.
      times->push_back(Lerp(t0, t1, s_end));
      points->push_back(p_end);
      s_begin = s_end;
      p_begin = p_end;
    }
    s_end = next_s_end;
    p_end = next_p_end;
  } while (s_end != 1.0f);

  // Add the final point.
  times->push_back(t1);
  points->push_back(key1[kSplineElementPoint]);
}

template <typename Converter>
void ConvertAnimKeysToLinear(
    Gltf::Animation::Sampler::Interpolation interpolation,
    std::vector<float>* times, std::vector<typename Converter::Point>* points) {
  using Point = typename Converter::Point;
  const size_t src_count = times->size();
  UFG_ASSERT_LOGIC(src_count > 0);
  const float* const src_times = times->data();
  const Point* const src_points = points->data();
  switch (interpolation) {
  case Gltf::Animation::Sampler::kInterpolationLinear:
    // Already linear. Leave it as-is.
    UFG_ASSERT_LOGIC(points->size() == src_count);
    break;

  case Gltf::Animation::Sampler::kInterpolationStep: {
    // Convert to step interpolation by inserting a new key between each segment
    // to snap between values.
    UFG_ASSERT_LOGIC(points->size() == src_count);
    const size_t dst_count = 2 * src_count;
    std::vector<float> dst_time_buffer(dst_count);
    std::vector<Point> dst_point_buffer(dst_count);
    float* const dst_times = dst_time_buffer.data();
    Point* const dst_points = dst_point_buffer.data();
    for (size_t src_i0 = 0; src_i0 != src_count; ++src_i0) {
      // Get the source segment.
      const size_t src_i1 = src_i0 + 1;
      const float src_t0 = src_times[src_i0];
      const float src_t1 = src_i1 == src_count ? src_t0 : src_times[src_i1];
      const Point& src_p0 = src_points[src_i0];
      const Point& src_p1 = src_i1 == src_count ? src_p0 : src_points[src_i1];
      const float src_dt = src_t1 - src_t0;

      // Form destination segment by adding two identical points very close
      // together in time.
      constexpr float kAnimLinearToStepFraction = 0.001f;
      const float dst_dt =
          std::min(src_dt * kAnimLinearToStepFraction, kAnimDtMin);
      const size_t dst_i0 = 2 * src_i0;
      const size_t dst_i1 = dst_i0 + 1;
      dst_times[dst_i0] = src_t0;
      dst_times[dst_i1] = src_t0 + dst_dt;
      dst_points[dst_i0] = src_p0;
      dst_points[dst_i1] = src_p1;
    }
    times->swap(dst_time_buffer);
    points->swap(dst_point_buffer);
    break;
  }

  case Gltf::Animation::Sampler::kInterpolationCubicSpline: {
    // Tessellate cubic spline into a linear approximation.
    const size_t src_point_count = src_count * kSplineElementCount;
    UFG_ASSERT_LOGIC(points->size() == src_point_count);

    // Add the first point.
    std::vector<float> dst_times;
    std::vector<Point> dst_points;
    dst_times.push_back(src_times[0]);
    dst_points.push_back(src_points[kSplineElementPoint]);

    // Tessellate curve segments.
    const size_t src_last = src_count - 1;
    for (size_t src_i0 = 0; src_i0 != src_last; ++src_i0) {
      const size_t src_i1 = src_i0 + 1;
      const float t0 = src_times[src_i0];
      const float t1 = src_times[src_i1];
      const Point* const key0 = src_points + src_i0 * kSplineElementCount;
      const Point* const key1 = src_points + src_i1 * kSplineElementCount;
      AddSplinePoints<Converter>(t0, key0, t1, key1, &dst_times, &dst_points);
    }

    times->swap(dst_times);
    points->swap(dst_points);
    break;
  }

  default:
    UFG_ASSERT_FORMAT(false);
    break;
  }
}

template void ConvertAnimKeysToLinear<TranslationKeyConverter>(
    Gltf::Animation::Sampler::Interpolation interpolation,
    std::vector<float>* times, std::vector<GfVec3f>* points);
template void ConvertAnimKeysToLinear<QuatKeyConverter>(
    Gltf::Animation::Sampler::Interpolation interpolation,
    std::vector<float>* times, std::vector<GfQuatf>* points);
template void ConvertAnimKeysToLinear<ScaleKeyConverter>(
    Gltf::Animation::Sampler::Interpolation interpolation,
    std::vector<float>* times, std::vector<GfVec3f>* points);

}  // namespace ufg
