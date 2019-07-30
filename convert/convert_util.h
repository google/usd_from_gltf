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

#ifndef UFG_CONVERT_CONVERT_UTIL_H_
#define UFG_CONVERT_CONVERT_UTIL_H_

#include <string>
#include <unordered_set>
#include "convert/convert_common.h"
#include "convert/tokens.h"
#include "gltf/gltf.h"
#include "pxr/base/tf/token.h"
#include "pxr/usd/sdf/types.h"
#include "pxr/usd/usd/timeCode.h"
#include "pxr/usd/usdShade/shader.h"

namespace ufg {
using PXR_NS::TfToken;
using PXR_NS::UsdShadeShader;
using PXR_NS::UsdTimeCode;
using PXR_NS::SdfPath;
using PXR_NS::SdfValueTypeNames;

std::string MakeValidUsdName(const char* prefix, const std::string& in,
                             size_t index);

inline std::string MakeValidUsdName(const char* prefix, const std::string& in,
                                    Gltf::Id id) {
  return MakeValidUsdName(prefix, in, Gltf::IdToIndex(id));
}

template <typename T>
void CreateEnumInput(const TfToken& name_tok, T value,
                     const TfToken& default_tok, UsdShadeShader* tex) {
  const TfToken& value_tok = ToToken(value);
  if (value_tok != default_tok) {
    tex->CreateInput(name_tok, SdfValueTypeNames->Token).Set(value_tok);
  }
}

UsdTimeCode GetTimeCode(float t);

class PathTable {
 public:
  void Clear();
  std::string MakeUnique(const SdfPath& parent_path, const char* prefix,
                         const std::string& in, size_t index);

 private:
  std::unordered_set<std::string> paths_;
};
}  // namespace ufg

#endif  // UFG_CONVERT_CONVERT_UTIL_H_
