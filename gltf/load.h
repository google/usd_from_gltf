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

#ifndef GLTF_LOAD_H_
#define GLTF_LOAD_H_

#include <istream>
#include <vector>
#include "gltf.h"  // NOLINT: Silence relative path warning.
#include "message.h"  // NOLINT: Silence relative path warning.

struct GltfLoadSettings {
  // Null terminated array used to disable warnings for unrecognized glTF
  // extensions.
  std::vector<std::string> nowarn_extension_prefixes;
};

// Load glTF from an input stream.
bool GltfLoad(
    std::istream& is, const char* name, const GltfLoadSettings& settings,
    Gltf* out_gltf, GltfLogger* logger);

#endif  // GLTF_LOAD_H_
