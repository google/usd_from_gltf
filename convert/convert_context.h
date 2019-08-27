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

#ifndef UFG_CONVERT_CONVERT_CONTEXT_H_
#define UFG_CONVERT_CONVERT_CONTEXT_H_

#include "common/common.h"
#include "common/common_util.h"
#include "common/config.h"
#include "common/logging.h"
#include "convert/convert_util.h"
#include "gltf/cache.h"
#include "gltf/gltf.h"

namespace ufg {
using PXR_NS::UsdStageRefPtr;

struct ConvertContext {
  std::string src_dir;
  std::string dst_dir;
  ConvertSettings settings;
  const Gltf* gltf;
  PathTable path_table;
  GltfCache gltf_cache;
  Logger* logger;
  GltfOnceLogger once_logger;
  UsdStageRefPtr stage;
  SdfPath root_path;

  void Reset(Logger* logger) {
    src_dir.clear();
    dst_dir.clear();
    settings = ConvertSettings::kDefault;
    gltf = nullptr;
    path_table.Clear();
    gltf_cache.Reset();
    this->logger = logger;
    once_logger.Reset(logger);
    stage = UsdStageRefPtr();
    root_path = SdfPath::EmptyPath();
  }
};
}  // namespace ufg

#endif  // UFG_CONVERT_CONVERT_CONTEXT_H_
