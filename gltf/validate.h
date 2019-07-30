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

#ifndef GLTF_VALIDATE_H_
#define GLTF_VALIDATE_H_

#include "gltf.h"  // NOLINT: Silence relative path warning.
#include "load.h"  // NOLINT: Silence relative path warning.
#include "message.h"  // NOLINT: Silence relative path warning.
#include "stream.h"  // NOLINT: Silence relative path warning.

// Perform glTF structure validation.
//
// This performs a thorough check for issues that are likely to cause conversion
// issues, rather than a strict enforcement of the schema.
//
// Notable exceptions are:
// 1) Any validation done earlier in GltfLoad is not repeated here.
// 2) It does not perform inspection of binary resources (including Draco-
//    compressed meshes).
// 3) Recoverable issues are treated as warnings rather than errors.
// 4) Fields extraneous to conversion may be ignored.
bool GltfValidate(const Gltf& gltf, GltfLogger* logger);

bool GltfLoadAndValidate(
    GltfStream* gltf_stream, const char* name, const GltfLoadSettings& settings,
    Gltf* out_gltf, GltfLogger* logger);

#endif  // GLTF_VALIDATE_H_
