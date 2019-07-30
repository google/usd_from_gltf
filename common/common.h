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

#ifndef UFG_COMMON_COMMON_H_
#define UFG_COMMON_COMMON_H_

#include <stdint.h>
#include "common/platform.h"

// Disable warnings originating in the USD headers.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244)  // Conversion from 'double' to 'float'.
#pragma warning(disable : 4305)  // Truncation from 'double' to 'float'.
#endif  // _MSC_VER
#include "pxr/base/gf/bbox3d.h"
#include "pxr/base/gf/matrix3d.h"
#include "pxr/base/gf/matrix3f.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/quath.h"
#include "pxr/base/gf/range3f.h"
#include "pxr/base/gf/rotation.h"
#include "pxr/base/gf/vec2d.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3h.h"
#include "pxr/base/gf/vec4d.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/vec4h.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif  // _MSC_VER

// Enable asserts.
// * This only increases executation time by ~5%, so it's enabled in all builds.
#define UFG_ASSERTS 1

#if _MSC_VER
#define UFG_BREAK_ON_ASSERT (1 && UFG_ASSERTS)
#else  // _MSC_VER
#define UFG_BREAK_ON_ASSERT 0
#endif  // _MSC_VER

namespace ufg {
// NOLINTNEXTLINE: Disable warning about old-style cast (it's not a cast!)
template <class T, size_t LEN> char(&ArraySizeHelper(T(&)[LEN]))[LEN];
#define UFG_ARRAY_SIZE(array) (sizeof(ufg::ArraySizeHelper(array)))

#define UFG_CONST_STRLEN(s) (UFG_ARRAY_SIZE(s) - 1)

template <typename T>
inline constexpr T Square(T a) {
  return a * a;
}
}  // namespace ufg

#endif  // UFG_COMMON_COMMON_H_
