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

#include "process/math.h"

#include "common/logging.h"

namespace ufg {
GfQuatd EulerToQuat(const GfVec3d& e) {
  const double x = e[0];
  const double y = e[1];
  const double z = e[2];

  const double cz = std::cos(z * 0.5), sz = std::sin(z * 0.5);
  const double cy = std::cos(y * 0.5), sy = std::sin(y * 0.5);
  const double cx = std::cos(x * 0.5), sx = std::sin(x * 0.5);

  const double qw = cz * cy * cx + sz * sy * sx;
  const double qx = cz * cy * sx - sz * sy * cx;
  const double qy = sz * cy * sx + cz * sy * cx;
  const double qz = sz * cy * cx - cz * sy * sx;
  return GfQuatd(qw, qx, qy, qz);
}

GfVec3d QuatToEuler(const GfQuatd& q) {
  const double qw = q.GetReal();
  const double qx = q.GetImaginary()[0];
  const double qy = q.GetImaginary()[1];
  const double qz = q.GetImaginary()[2];

  // x (x-axis rotation)
  const double sx_cy = 2 * (qw * qx + qy * qz);
  const double cx_cy = 1 - 2 * (qx * qx + qy * qy);
  const double x = atan2(sx_cy, cx_cy);

  // y (y-axis rotation)
  const double sy = 2 * (qw * qy - qz * qx);
  const double y = std::abs(sy) < 1.0f ?
      std::asin(sy) : copysign(Constants<double>::kHalfPi, sy);

  // z (z-axis rotation)
  const double sz_cy = 2 * (qw * qz + qx * qy);
  const double cz_cy = 1 - 2 * (qy * qy + qz * qz);
  const double z = std::atan2(sz_cy, cz_cy);

  return GfVec3d(x, y, z);
}

Srt GetNodeSrt(const Gltf::Node& node) {
  Srt srt;
  if (node.is_matrix) {
    // Convert matrix to SRT.
    const GfMatrix4d mat = ToMatrix4d(node.matrix);
    GfMatrix4d scale_orient_mat, rot_mat, persp_mat;
    GfVec3d scale_d, translation_d;
    UFG_VERIFY(mat.Factor(
        &scale_orient_mat, &scale_d, &rot_mat, &translation_d, &persp_mat));
    UFG_VERIFY(rot_mat.Orthonormalize());
    srt.rotation = GfQuatf(rot_mat.ExtractRotation().GetQuat());
    srt.scale = GfVec3f(scale_d);
    srt.translation = GfVec3f(translation_d);
  } else {
    // Source is SRT.
    srt.scale = GfVec3f(node.scale);
    srt.rotation = GfQuatf(
        node.rotation[3], node.rotation[0], node.rotation[1], node.rotation[2]);
    srt.translation = GfVec3f(node.translation);
  }
  return srt;
}

GfMatrix4d Unrotate(const GfMatrix4d& mat) {
  GfMatrix4d scale_orient_mat, rot_mat, persp_mat;
  GfVec3d scale, translate;
  UFG_VERIFY(mat.Factor(
      &scale_orient_mat, &scale, &rot_mat, &translate, &persp_mat));
  GfMatrix4d scale_mat, translate_mat;
  scale_mat.SetScale(scale);
  translate_mat.SetTranslate(translate);
  return translate_mat * scale_mat;
}

GfVec3d GetBoxSize(const GfBBox3d& bound) {
  const GfMatrix4d mat = bound.GetMatrix();
  const GfMatrix4d unrotated_mat = Unrotate(mat);
  const GfBBox3d unrotated_bound(bound.GetRange(), unrotated_mat);
  const GfRange3d aabb = unrotated_bound.ComputeAlignedRange();
  return aabb.GetSize();
}

GfRange3f BoundPoints(const GfVec3f* points, size_t point_count) {
  GfRange3f aabb;
  for (size_t point_index = 0; point_index != point_count; ++point_index) {
    aabb.UnionWith(points[point_index]);
  }
  return aabb;
}

void FlipVs(size_t uv_count, GfVec2f* uvs) {
  for (GfVec2f* uv = uvs, *const uv_end = uvs + uv_count; uv != uv_end; ++uv) {
    (*uv)[1] = 1.0f - (*uv)[1];
  }
}

void TransformUvs(const Gltf::Material::Texture::Transform& transform,
                  size_t uv_count, GfVec2f* uvs) {
  if (transform.IsIdentity()) {
    return;
  }
  const float sx = transform.scale[0];
  const float sy = transform.scale[1];
  const float rx = std::cos(transform.rotation);
  const float ry = std::sin(transform.rotation);
  const float tx = transform.offset[0];
  const float ty = transform.offset[1];
  const float m00 = sx * rx, m01 = -sy * ry, m02 = tx - m01;
  const float m10 = sx * ry, m11 = sy * rx, m12 = 1.0f - ty - m11;
  for (GfVec2f* uv = uvs, *const uv_end = uvs + uv_count; uv != uv_end; ++uv) {
    const float u = (*uv)[0], v = (*uv)[1];
    (*uv)[0] = u * m00 + v * m01 + m02;
    (*uv)[1] = u * m10 + v * m11 + m12;
  }
}

// TODO: Support rotation basis transform to reduce/prevent gimbal
// lock.
// * In practice, we get away without it because we super-sample rotations to
//   correct for errors. Choosing a better basis would reduce animation size by
//   a lot in some cases though, and prevent singularities near the Euler poles.
// * A simple method for choosing the basis would be to try orienting along each
//   of the 3 cardinal axes and choosing the one that results in the smallest
//   animation. We could further refine that using a bisection search
//   (recursively split the triangle formed by the 3 vectors at its midpoint).
void ConvertRotationKeys(
    const std::vector<float>& src_times, const std::vector<GfQuatf>& src_quats,
    std::vector<float>* dst_times, std::vector<GfVec3f>* dst_eulers) {
  // Length (in radians) to subdivide rotation arcs for error tests.
  // * Decreasing this reduces the chance of missing errors at the cost of
  //   processing time. Should be less that 90º to ensure we don't miss errors
  //   in local maxima.
  static constexpr float kSubdivAngleInterval =
      15.0f * Constants<float>::kDegToRad;
  // Maximum allowable error (in radians).  If the arc between two Euler
  // rotations deviates from the quaternion rotation by this much or more, a new
  // point is added along the quaternion arc to correct the error.
  // * Decreasing this improves animation quality at the cost of animation size.
  static constexpr float kErrorMax = 0.1f * Constants<float>::kDegToRad;
  // Tolerance (in radians) used to refine the search for split points.
  // * Decreasing this improves animation size (slightly) at the cost of
  //   processing time.
  static constexpr float kErrorRefineTol = kErrorMax / 100.0f;
  // Quaternion slerp is erratic near 180º because the rotation direction is
  // arbitrary (there are two possible shortest paths). This may cause us to
  // choose the wrong direction in Euler space, preventing convergence.  So to
  // work around this we split near-180º rotations at the midpoint to make
  // interpolation direction consistent.
  static constexpr float kQuat180Tol = Constants<float>::kPi * 0.9f;
  // Maximum number of search iterations to refine split points.
  // * The refinement uses a bisection search, so each iteration should halve
  //   the error.  Theoretically, this limit shouldn't be necessary as the
  //   refinement converges quickly, but it acts a failsafe to prevent infinite
  //   looping due to floating-point imprecision.
  static constexpr size_t kErrorRefineLimit = 20;
  // Minimum DT between generated keys.
  static constexpr double kDtMin = 1.0f / 120.0f;

  const size_t src_count = src_times.size();
  if (src_count < 2) {
    // Fewer than 2 points: just copy as-is to the output.
    *dst_times = src_times;
    dst_eulers->resize(src_count);
    for (size_t i = 0; i != src_count; ++i) {
      (*dst_eulers)[i] = GfVec3f(QuatToEuler(GfQuatd(src_quats[i])));
    }
    return;
  }

  std::vector<float> times;
  std::vector<GfVec3f> eulers;
  times.reserve(src_count);
  eulers.reserve(src_count);

  double t0 = src_times[0];
  GfQuatd q0(src_quats[0]);
  GfVec3d e0 = QuatToEuler(q0);

  for (size_t i1 = 1; i1 != src_count; ) {
    times.push_back(static_cast<float>(t0));
    eulers.push_back(GfVec3f(e0));

    double t1 = src_times[i1];
    GfQuatd q1(src_quats[i1]);
    const bool half_step = GetQuatAbsMinDeltaAngle(q0, q1) > kQuat180Tol;
    if (half_step) {
      t1 = Lerp(t0, t1, 0.5);
      q1 = GfSlerp(q0, q1, 0.5);
    }

    const GfVec3d e1 = EulerStep(e0, QuatToEuler(q1));

    const double delta_angle = GetQuatAbsMinDeltaAngle(q0, q1);
    UFG_ASSERT_LOGIC(std::abs(delta_angle) < Constants<double>::kPi * 1.01);
    const size_t subdiv_count = std::max(static_cast<size_t>(1),
        static_cast<size_t>(std::ceil(
            delta_angle * (1.0 / kSubdivAngleInterval))));
    const double subdiv_scale = 1.0 / subdiv_count;
    for (size_t subdiv_index = 0; ; ++subdiv_index) {
      const double s = (subdiv_index + 1) * subdiv_scale;
      const GfQuatd qs = GfSlerp(q0, q1, s);
      const GfVec3d es = Lerp(e0, e1, s);
      const GfQuatd e2qs = EulerToQuat(es);
      const double error = GetQuatAbsMinDeltaAngle(qs, e2qs);
      const double dt = (t1 - t0) * s;
      const bool exceeds_error = error > kErrorMax && dt > kDtMin;
      const bool final_segment = subdiv_index + 1 == subdiv_count;
      if (exceeds_error || final_segment) {
        // Exceeded the error tolerance or reached the end of the sequence.

        // Refine point using bisection search.
        double s_lower = s - subdiv_scale;
        double s_upper = s;
        if (exceeds_error && std::abs(error - kErrorMax) > kErrorRefineTol) {
          for (size_t refine_limit = kErrorRefineLimit; refine_limit;
               --refine_limit) {
            const double s_mid = 0.5 * (s_lower + s_upper);
            const GfQuatd q_mid = GfSlerp(q0, q1, s_mid);
            const GfVec3d e_mid = Lerp(e0, e1, s_mid);
            const GfQuatd e2q_mid = EulerToQuat(e_mid);
            const double error_mid = GetQuatAbsMinDeltaAngle(q_mid, e2q_mid);
            if (std::abs(error_mid - kErrorMax) <= kErrorRefineTol) {
              // Stop when we're near enough to the goal.
              s_upper = s_mid;
              break;
            }
            if (error_mid < kErrorMax) {
              s_lower = s_mid;
            } else {
              s_upper = s_mid;
            }
          }
        }

        // Add a new point.
        const double dt_upper = (t1 - t0) * s_upper;
        if (dt_upper < kDtMin) {
          // Force a minimum dt to prevent the animation from growing overly
          // large in cases where we can't precisely fit the quaternion
          // interpolation to Euler. If this happens, it likely indicates that
          // the quaternion is blending through an Euler singularity. In this
          // case the resulting animation is technically incorrect, but the
          // error should be mostly unnoticeable due to the high frame-rate.
          // TODO: This could be improved or eliminated with the use
          // of an Euler orientation matrix.
          s_upper = (t0 + kDtMin < t1) ? kDtMin / (t1 - t0) : 1.0;
        }
        t0 = Lerp(t0, t1, s_upper);
        q0 = GfSlerp(q0, q1, s_upper);
        e0 = EulerStep(e0, QuatToEuler(q0));
        if (final_segment && !half_step) {
          ++i1;
        }
        break;
      }
    }
  }

  times.push_back(static_cast<float>(t0));
  eulers.push_back(GfVec3f(e0));

  dst_times->swap(times);
  dst_eulers->swap(eulers);
}
}  // namespace ufg
