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

#ifndef UFG_CONVERT_CONVERT_COMMON_H_
#define UFG_CONVERT_CONVERT_COMMON_H_

#include "common/common.h"

// Disable warnings originating in the USD headers.
#ifdef _MSC_VER
#pragma warning(push)
// not enough actual parameters for macro 'BOOST_*'
#pragma warning(disable : 4003)
// conversion from 'Py_ssize_t' to 'unsigned int', possible loss of data
#pragma warning(disable : 4244)
// conversion from 'size_t' to 'int', possible loss of data
#pragma warning(disable : 4267)
#endif  // _MSC_VER

#include "pxr/usd/usdGeom/scope.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdShade/material.h"
#include "pxr/usd/usdShade/shader.h"

#ifdef _MSC_VER
#pragma warning(pop)
#endif  // _MSC_VER

#endif  // UFG_CONVERT_CONVERT_COMMON_H_
