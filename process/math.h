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

#ifndef UFG_PROCESS_MATH_H_
#define UFG_PROCESS_MATH_H_

#include "common/common.h"
#include "common/config.h"
#include "gltf/gltf.h"

namespace ufg {
using PXR_NS::GfBBox3d;
using PXR_NS::GfMatrix3d;
using PXR_NS::GfMatrix3f;
using PXR_NS::GfMatrix4d;
using PXR_NS::GfMatrix4f;
using PXR_NS::GfQuatd;
using PXR_NS::GfQuatf;
using PXR_NS::GfRange3d;
using PXR_NS::GfRange3f;
using PXR_NS::GfRotation;
using PXR_NS::GfVec2d;
using PXR_NS::GfVec2f;
using PXR_NS::GfVec3d;
using PXR_NS::GfVec3f;
using PXR_NS::GfVec3h;
using PXR_NS::GfVec4d;
using PXR_NS::GfVec4f;

// Round up to a power-of-2.
inline uint32_t Power2Ceil(uint32_t v) {
  --v;
  v |= v >> 1;
  v |= v >> 2;
  v |= v >> 4;
  v |= v >> 8;
  v |= v >> 16;
  return v + 1;
}

// Round down to a power-of-2.
inline uint32_t Power2Floor(uint32_t v) {
  const uint32_t c = Power2Ceil(v);
  return c == v ? c : c / 2;
}

// Align up to the nearest interval of 'align' (must be power-of-2).
template <typename T>
inline T AlignUp(T value, size_t align) {
  const T mask = static_cast<T>(align) - 1;
  return (value + mask) & ~mask;
}

template <typename Matrix, typename Scalar>
inline Matrix ToMatrix4(const Scalar* m) {
  return Matrix(m[0 * 4 + 0], m[0 * 4 + 1], m[0 * 4 + 2], m[0 * 4 + 3],
                m[1 * 4 + 0], m[1 * 4 + 1], m[1 * 4 + 2], m[1 * 4 + 3],
                m[2 * 4 + 0], m[2 * 4 + 1], m[2 * 4 + 2], m[2 * 4 + 3],
                m[3 * 4 + 0], m[3 * 4 + 1], m[3 * 4 + 2], m[3 * 4 + 3]);
}

inline GfMatrix4d ToMatrix4d(const float* m) {
  return ToMatrix4<GfMatrix4d>(m);
}
inline GfMatrix4f ToMatrix4f(const float* m) {
  return ToMatrix4<GfMatrix4f>(m);
}

inline GfMatrix3f ToMatrix3f(const GfMatrix4d& m) {
  return GfMatrix3f(
      static_cast<float>(m[0][0]),
      static_cast<float>(m[0][1]),
      static_cast<float>(m[0][2]),
      //
      static_cast<float>(m[1][0]),
      static_cast<float>(m[1][1]),
      static_cast<float>(m[1][2]),
      //
      static_cast<float>(m[2][0]),
      static_cast<float>(m[2][1]),
      static_cast<float>(m[2][2]));
}

inline GfVec3f ToVec3(const GfVec4f v4) {
  return GfVec3f(v4[0], v4[1], v4[2]);
}

inline GfQuatd ToQuatd(const float (&q)[4]) {
  return GfQuatd(q[3], q[0], q[1], q[2]);
}
inline GfQuatf ToQuatf(const float (&q)[4]) {
  return GfQuatf(q[3], q[0], q[1], q[2]);
}

// Normalized lerp for quaternions.  This is an approximation of Slerp that
// works for relatively small rotations (and it's what the iOS viewer uses, so
// we use it for error metrics).
template <typename Quat>
inline Quat Nlerp(const Quat& a, const Quat& b, typename Quat::ScalarType s) {
  return (a * (1 - s) + b * s).GetNormalized();
}

// Returns scale*rotate*translate matrix.
// * This is reversed order from glTF, because USD uses the opposite
//   multiplication ordering.
inline GfMatrix4d SrtToMatrix4d(
    const float (&s)[3], const float (&r)[4], const float (&t)[3]) {
  const GfMatrix3d m(ToQuatd(r));
  return GfMatrix4d(
    m[0][0] * s[0], m[0][1] * s[0], m[0][2] * s[0], 0.0,
    m[1][0] * s[1], m[1][1] * s[1], m[1][2] * s[1], 0.0,
    m[2][0] * s[2], m[2][1] * s[2], m[2][2] * s[2], 0.0,
    t[0], t[1], t[2], 1);
}

// yaw (Z), pitch (Y), roll (X)
GfQuatd EulerToQuat(const GfVec3d& e);

GfVec3d QuatToEuler(const GfQuatd& q);
inline GfVec3f QuatToEuler(const GfQuatf& q) {
  return GfVec3f(QuatToEuler(GfQuatd(q)));
}

// The vector library doesn't have component-wise multiply and instead overloads
// '*' to be a dot product.
inline GfVec3f Multiply(const GfVec3f& a, const GfVec3f& b) {
  return GfVec3f(a[0] * b[0], a[1] * b[1], a[2] * b[2]);
}

inline GfVec3f Recip(const GfVec3f& v) {
  return GfVec3f(1.0f / v[0], 1.0f / v[1], 1.0f / v[2]);
}

template <typename T>
inline T AngleMod(T a) {
  const T f = std::floor(a * Constants<T>::kRecipTwoPi + static_cast<T>(0.5));
  return a - f * Constants<T>::kTwoPi;
}

inline GfVec3d AngleMod(const GfVec3d& e) {
  return GfVec3d(AngleMod(e[0]), AngleMod(e[1]), AngleMod(e[2]));
}

template <typename T>
inline T Max3(T a, T b, T c) {
  return std::max(a, std::max(b, c));
}
template <typename T>
inline T Max4(T a, T b, T c, T d) {
  return std::max(std::max(a, b), std::max(c, d));
}

inline double MaxComponent(const GfVec3d& v) {
  return Max3(v[0], v[1], v[2]);
}

inline double AbsMaxComponent(const GfVec3d& v) {
  return Max3(std::abs(v[0]), std::abs(v[1]), std::abs(v[2]));
}

template <typename Vec>
inline Vec GetNearestEulerDelta(const Vec& e0, const Vec& e1) {
  using Scalar = typename Vec::ScalarType;
  static constexpr Scalar kPi = Constants<Scalar>::kPi;
  const Vec e1_equiv(kPi + e1[0], kPi - e1[1], kPi + e1[2]);
  const Vec d0 = AngleMod(e1 - e0);
  const Vec d1 = AngleMod(e1_equiv - e0);
  return d0.GetLengthSq() <= d1.GetLengthSq() ? d0 : d1;
}

template <typename Vec>
inline Vec EulerStep(const Vec& e0, const Vec& e1) {
  return e0 + GetNearestEulerDelta(e0, e1);
}

template <typename Vec>
inline Vec RadToDeg(const Vec& rad) {
  using Scalar = typename Vec::ScalarType;
  return rad * Constants<Scalar>::kRadToDeg;
}

template <typename Vec>
inline Vec DegToRad(const Vec& deg) {
  using Scalar = typename Vec::ScalarType;
  return deg * Constants<Scalar>::kDegToRad;
}

template <typename T, typename S>
inline T Lerp(T a, T b, S s) {
  return a * (1 - s) + b * s;
}

template <typename T>
inline T Clamp(T value, T lower, T upper) {
  return std::max(std::min(value, upper), lower);
}

template <typename T>
inline bool NearlyEqual(T a, T b, T tol) {
  return std::abs(a - b) <= tol;
}

inline bool NearlyEqual(const GfVec3f& a, const GfVec3f& b, float tol) {
  return NearlyEqual(a[0], b[0], tol) &&
         NearlyEqual(a[1], b[1], tol) &&
         NearlyEqual(a[2], b[2], tol);
}
inline bool NearlyEqual(const GfQuatf& a, const GfQuatf& b, float tol) {
  return NearlyEqual(a.GetImaginary(), b.GetImaginary(), tol) &&
         NearlyEqual(a.GetReal(), b.GetReal(), tol);
}

template <typename T>
inline bool AllNearlyEqual(const T* a, size_t a_count, T b, T tol) {
  for (size_t i = 0; i != a_count; ++i) {
    if (!NearlyEqual(a[i], b, tol)) {
      return false;
    }
  }
  return true;
}

template <typename T>
inline bool AllNearlyEqual(const std::vector<T>& a, T b, T tol) {
  return AllNearlyEqual(a.data(), a.size(), b, tol);
}

template <typename T>
inline bool ArraysNearlyEqual(const T* a, const T* b, size_t count, T tol) {
  for (size_t i = 0; i != count; ++i) {
    if (!NearlyEqual(a[i], b[i], tol)) {
      return false;
    }
  }
  return true;
}

inline bool NearlyEqual(const GfMatrix4d& a, const GfMatrix4d& b, double tol) {
  return ArraysNearlyEqual(a.data(), b.data(), 16, tol);
}

// Returns half of the cosine of the delta angle between two quats.  I.e. cos(q0
// * q1⁻¹)/2
template <typename Quat>
inline typename Quat::ScalarType GetQuatHalfCosDeltaAngle(
    const Quat& q0, const Quat& q1) {
  return q0.GetReal() * q1.GetReal() +
         GfDot(q0.GetImaginary(), q1.GetImaginary());
}

// Returns the delta from q0 to q1, in the range [0, 2pi].
template <typename Quat>
inline typename Quat::ScalarType GetQuatDeltaAngle(
    const Quat& q0, const Quat& q1) {
  // acos requires input in the range [0, 1], but w may slightly exceed that due
  // to floating-point imprecision.
  using Scalar = typename Quat::ScalarType;
  const Scalar w = GetQuatHalfCosDeltaAngle(q0, q1);
  const Scalar result =
      std::abs(w) < Constants<Scalar>::kAcosTol ? 2 * std::acos(w) : Scalar(0);
  return result;
}

// Returns the absolute value of the delta between two quaternions, in the
// shortest direction.  This will be a value in the range [0, pi].
template <typename Quat>
inline typename Quat::ScalarType GetQuatAbsMinDeltaAngle(
    const Quat& q0, const Quat& q1) {
  // acos requires input in the range [0, 1], but w may slightly exceed that due
  // to floating-point imprecision.
  using Scalar = typename Quat::ScalarType;
  const Scalar w = std::abs(GetQuatHalfCosDeltaAngle(q0, q1));
  const Scalar result =
      w < Constants<Scalar>::kAcosTol ? 2 * std::acos(w) : Scalar(0);
  return result;
}

struct Srt {
  GfVec3f scale;
  GfQuatf rotation;
  GfVec3f translation;
};
Srt GetNodeSrt(const Gltf::Node& node);

// Eliminate matrix rotation while preserving scale and translation.
GfMatrix4d Unrotate(const GfMatrix4d& mat);

// Get transformed box size from a bounding box. I.e. this is the AABB of the
// OBB without rotation.
GfVec3d GetBoxSize(const GfBBox3d& bound);

GfRange3f BoundPoints(const GfVec3f* points, size_t point_count);

void FlipVs(size_t uv_count, GfVec2f* uvs);
void TransformUvs(const Gltf::Material::Texture::Transform& transform,
                  size_t uv_count, GfVec2f* uvs);

void ConvertRotationKeys(
    const std::vector<float>& src_times, const std::vector<GfQuatf>& src_quats,
    std::vector<float>* dst_times, std::vector<GfVec3f>* dst_eulers);
}  // namespace ufg

#endif  // UFG_PROCESS_MATH_H_
