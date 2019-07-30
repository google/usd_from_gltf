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

#include "convert/convert_util.h"

#include "common/common_util.h"
#include "common/config.h"
#include "pxr/base/tf/stringUtils.h"

namespace ufg {
std::string MakeValidUsdName(const char* prefix, const std::string& in,
                             size_t index) {
  return in.empty() ? AppendNumber(prefix, index)
                    : PXR_NS::TfMakeValidIdentifier(in);
}

UsdTimeCode GetTimeCode(float t) {
  const float s = kFps * t;
  const float r = std::roundf(s);
  return UsdTimeCode(std::abs(s - r) < kSnapTimeCodeTol ? r : s);
}

void PathTable::Clear() {
  paths_.clear();
}

std::string PathTable::MakeUnique(const SdfPath& parent_path,
                                  const char* prefix, const std::string& in,
                                  size_t index) {
  const std::string orig_name = MakeValidUsdName(prefix, in, index);
  std::string name = orig_name;
  size_t duplicate_count = 0;
  for (;;) {
    const SdfPath path = parent_path.AppendElementString(name);
    const std::string& path_str = path.GetString();
    const auto insert_result = paths_.insert(path_str);
    if (insert_result.second) {
      return path_str;
    }
    name = AppendNumber(orig_name + '_', ++duplicate_count);
  }
}

}  // namespace ufg
