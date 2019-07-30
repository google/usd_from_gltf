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

#ifndef UFG_CONVERT_PACKAGE_H_
#define UFG_CONVERT_PACKAGE_H_

#include "common/common.h"
#include "common/config.h"
#include "common/logging.h"

namespace ufg {
// Register plugins at the given path.
// This is optional - if it is not called, plugins are loaded using the system
// path.
bool RegisterPlugins(const std::string& path, Logger* logger);

bool ConvertGltfToUsd(const char* src_gltf_path, const char* dst_usd_path,
                      const ConvertSettings& settings, Logger* logger);
}  // namespace ufg

#endif  // UFG_CONVERT_PACKAGE_H_
