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

#include "common/config.h"

namespace ufg {
const GfVec3f kColorBlack(0.0f, 0.0f, 0.0f);
const GfVec4f kFallbackBase(1.0f, 0.0f, 1.0f, 1.0f);
const GfVec3f kFallbackDiffuse(1.0f, 0.0f, 1.0f);
const GfVec3f kFallbackNormal(0.0f, 0.0f, 1.0f);
const GfVec3f kFallbackEmissive(0.5f, 0.0f, 0.5f);
const GfVec3f kFallbackSpecular(1.0f, 0.0f, 1.0f);

const ConvertSettings ConvertSettings::kDefault;
}  // namespace ufg
