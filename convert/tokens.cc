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

#include "convert/tokens.h"

namespace ufg {
const TfToken kTokEmpty;

const TfToken kTokColors("colors");
const TfToken kTokFallback("fallback");
const TfToken kTokFile("file");
const TfToken kTokFilterMin("minFilter");
const TfToken kTokFilterMag("magFilter");
const TfToken kTokPreviewSurface("UsdPreviewSurface");
const TfToken kTokPrimvarFloat2("UsdPrimvarReader_float2");
const TfToken kTokResult("result");
const TfToken kTokScale("scale");
const TfToken kTokSt("st");
const TfToken kTokSurface("surface");
const TfToken kTokUvTexture("UsdUVTexture");
const TfToken kTokVarname("varname");
const TfToken kTokWrapS("wrapS");
const TfToken kTokWrapT("wrapT");

// Color components.
const TfToken kTokR("r");
const TfToken kTokA("a");
const TfToken kTokRgb("rgb");

// PBR shader inputs.
const TfToken kTokInputDiffuseColor("diffuseColor");
const TfToken kTokInputEmissiveColor("emissiveColor");
const TfToken kTokInputGlossiness("glossiness");
const TfToken kTokInputMetallic("metallic");
const TfToken kTokInputNormal("normal");
const TfToken kTokInputOcclusion("occlusion");
const TfToken kTokInputOpacity("opacity");
const TfToken kTokInputRoughness("roughness");
const TfToken kTokInputSpecularColor("specularColor");
const TfToken kTokInputUseSpecular("useSpecularWorkflow");

// Texture wrap states.
const TfToken kTokWrapClamp("clamp");
const TfToken kTokWrapMirror("mirror");
const TfToken kTokWrapRepeat("repeat");

// Texture filter states.
const TfToken kTokFilterNearest("nearest");
const TfToken kTokFilterLinear("linear");
const TfToken kTokFilterNearestMipmapNearest("nearestMipmapNearest");
const TfToken kTokFilterLinearMipmapNearest("linearMipmapNearest");
const TfToken kTokFilterNearestMipmapLinear("nearestMipmapLinear");
const TfToken kTokFilterLinearMipmapLinear("linearMipmapLinear");

const TfToken& ToToken(Gltf::Sampler::WrapMode wrap_mode) {
  switch (wrap_mode) {
  case Gltf::Sampler::kWrapClamp:
    return kTokWrapClamp;
  case Gltf::Sampler::kWrapMirror:
    return kTokWrapMirror;
  case Gltf::Sampler::kWrapRepeat:
  case Gltf::Sampler::kWrapUnset:
  default:
    return kTokWrapRepeat;
  }
}

const TfToken& ToToken(Gltf::Sampler::MinFilter filter) {
  switch (filter) {
  case Gltf::Sampler::kMinFilterNearest:
    return kTokFilterNearest;
  case Gltf::Sampler::kMinFilterLinear:
    return kTokFilterLinear;
  case Gltf::Sampler::kMinFilterNearestMipmapNearest:
    return kTokFilterNearestMipmapNearest;
  case Gltf::Sampler::kMinFilterLinearMipmapNearest:
    return kTokFilterLinearMipmapNearest;
  case Gltf::Sampler::kMinFilterNearestMipmapLinear:
    return kTokFilterNearestMipmapLinear;
  case Gltf::Sampler::kMinFilterLinearMipmapLinear:
  case Gltf::Sampler::kMinFilterUnset:
  default:
    return kTokFilterLinearMipmapLinear;
  }
}

const TfToken& ToToken(Gltf::Sampler::MagFilter filter) {
  switch (filter) {
  case Gltf::Sampler::kMagFilterNearest:
    return kTokFilterNearest;
  case Gltf::Sampler::kMagFilterLinear:
  case Gltf::Sampler::kMagFilterUnset:
  default:
    return kTokFilterLinear;
  }
}

}  // namespace ufg
