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

#ifndef UFG_CONVERT_TOKENS_H_
#define UFG_CONVERT_TOKENS_H_

#include "common/common.h"
#include "gltf/gltf.h"
#include "pxr/base/tf/token.h"

namespace ufg {
using PXR_NS::TfToken;

extern const TfToken kTokEmpty;

extern const TfToken kTokColors;  // "colors"
extern const TfToken kTokFallback;  // "fallback"
extern const TfToken kTokFile;  // "file"
extern const TfToken kTokFilterMin;  // "minFilter"
extern const TfToken kTokFilterMag;  // "magFilter"
extern const TfToken kTokPreviewSurface;  // "UsdPreviewSurface"
extern const TfToken kTokPrimvarFloat2;  // "UsdPrimvarReader_float2"
extern const TfToken kTokResult;  // "result"
extern const TfToken kTokScale;  // "scale"
extern const TfToken kTokSt;  // "st"
extern const TfToken kTokSurface;  // "surface"
extern const TfToken kTokUvTexture;  // "UsdUVTexture"
extern const TfToken kTokVarname;  // "varname"
extern const TfToken kTokWrapS;  // "wrapS"
extern const TfToken kTokWrapT;  // "wrapT"

// Color components.
extern const TfToken kTokR;  // "r"
extern const TfToken kTokA;  // "a"
extern const TfToken kTokRgb;  // "rgb"

// PBR shader inputs.
extern const TfToken kTokInputDiffuseColor;  // "diffuseColor"
extern const TfToken kTokInputEmissiveColor;  // "emissiveColor"
extern const TfToken kTokInputGlossiness;  // "glossiness"
extern const TfToken kTokInputMetallic;  // "metallic"
extern const TfToken kTokInputNormal;  // "normal"
extern const TfToken kTokInputOcclusion;  // "occlusion"
extern const TfToken kTokInputOpacity;  // "opacity"
extern const TfToken kTokInputRoughness;  // "roughness"
extern const TfToken kTokInputSpecularColor;  // "specularColor"
extern const TfToken kTokInputUseSpecular;  // "useSpecularWorkflow"

// Texture wrap states.
extern const TfToken kTokWrapClamp;  // "clamp"
extern const TfToken kTokWrapMirror;  // "mirror"
extern const TfToken kTokWrapRepeat;  // "repeat"

// Texture filter states.
extern const TfToken kTokFilterNearest;  // "nearest"
extern const TfToken kTokFilterLinear;  // "linear"
extern const TfToken kTokFilterNearestMipmapNearest;  // "nearestMipmapNearest"
extern const TfToken kTokFilterLinearMipmapNearest;  // "linearMipmapNearest"
extern const TfToken kTokFilterNearestMipmapLinear;  // "nearestMipmapLinear"
extern const TfToken kTokFilterLinearMipmapLinear;  // "linearMipmapLinear"

const TfToken& ToToken(Gltf::Sampler::WrapMode wrap_mode);
const TfToken& ToToken(Gltf::Sampler::MinFilter filter);
const TfToken& ToToken(Gltf::Sampler::MagFilter filter);

}  // namespace ufg

#endif  // UFG_CONVERT_TOKENS_H_
