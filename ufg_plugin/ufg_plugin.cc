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

#include "ufg_plugin.h"  // NOLINT: Silence relative path warning.

#include "convert/converter.h"
#include "gltf/validate.h"
#include "pxr/usd/sdf/layer.h"
#include "pxr/usd/usd/usdaFileFormat.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_DEFINE_PUBLIC_TOKENS(UsdGltfFileFormatTokens, USDGLTF_FILE_FORMAT_TOKENS);

TF_REGISTRY_FUNCTION(TfType) {
  SDF_DEFINE_FILE_FORMAT(UsdGltfFileFormat, SdfFileFormat);
}

class UsdGltfFileFormatLogger : public GltfLogger {
 public:
  UsdGltfFileFormatLogger() : error_count_(0) {}

  void Add(const GltfMessage& message) override {
    const std::string text = message.ToString(false);
    switch (message.GetSeverity()) {
    case kGltfSeverityWarning:
      TF_DIAGNOSTIC_WARNING("%s", text.c_str());
      break;
    case kGltfSeverityError:
      TF_RUNTIME_ERROR("%s", text.c_str());
      ++error_count_;
      break;
    default:
      break;
    }
  }

  size_t GetErrorCount() const override {
    return error_count_;
  }

 private:
  size_t error_count_;
};

UsdGltfFileFormat::UsdGltfFileFormat()
    : SdfFileFormat(
          UsdGltfFileFormatTokens->Id, UsdGltfFileFormatTokens->Version,
          UsdGltfFileFormatTokens->Target, UsdGltfFileFormatTokens->Id) {}

UsdGltfFileFormat::~UsdGltfFileFormat() {}

bool UsdGltfFileFormat::CanRead(const std::string& path) const {
  std::string src_dir, src_name;
  Gltf::SplitPath(path, &src_dir, &src_name);
  GltfVectorLogger logger;
  std::unique_ptr<GltfStream> gltf_stream =
      GltfStream::Open(&logger, path.c_str(), src_dir.c_str());
  if (!gltf_stream) {
    return false;
  }
  const ufg::ConvertSettings& settings = ufg::ConvertSettings::kDefault;
  Gltf gltf;
  return GltfLoadAndValidate(
      gltf_stream.get(), path.c_str(), settings.gltf_load_settings,
      &gltf, &logger);
}

#if OLD_FORMAT_PLUGIN_API
bool UsdGltfFileFormat::Read(const SdfLayerBasePtr& layer_base,
    const std::string& resolved_path, bool metadata_only) const {
  SdfLayerHandle layer = TfDynamic_cast<SdfLayerHandle>(layer_base);
  if (!TF_VERIFY(layer)) {
    TF_RUNTIME_ERROR("Cannot create layer for GLTF file: %s",
                     resolved_path.c_str());
    return false;
  }
#else  // OLD_FORMAT_PLUGIN_API
bool UsdGltfFileFormat::Read(SdfLayer* layer,
    const std::string& resolved_path, bool metadata_only) const {
#endif  // OLD_FORMAT_PLUGIN_API

  UsdGltfFileFormatLogger logger;

  std::string src_dir, src_name;
  Gltf::SplitPath(resolved_path, &src_dir, &src_name);
  std::unique_ptr<GltfStream> gltf_stream = GltfStream::Open(
      &logger, resolved_path.c_str(), src_dir.c_str());
  if (!gltf_stream) {
    TF_RUNTIME_ERROR("Cannot open GLTF stream at: %s", resolved_path.c_str());
    return false;
  }

  const ufg::ConvertSettings& settings = ufg::ConvertSettings::kDefault;
  Gltf gltf;
  std::vector<GltfMessage> messages;
  const bool load_success = GltfLoadAndValidate(
      gltf_stream.get(), resolved_path.c_str(), settings.gltf_load_settings,
      &gltf, &logger);
  if (!load_success) {
    TF_RUNTIME_ERROR("Failed loading GLTF file: %s", resolved_path.c_str());
    return false;
  }

  const SdfLayerRefPtr gltf_layer = SdfLayer::CreateAnonymous(".usda");
  ufg::Converter converter;
  if (!converter.Convert(
          settings, gltf, gltf_stream.get(), src_dir, src_dir, gltf_layer,
          &logger)) {
    TF_RUNTIME_ERROR("Failed converting GLTF file: %s", resolved_path.c_str());
    return false;
  }

  layer->TransferContent(gltf_layer);
  return true;
}

#if OLD_FORMAT_PLUGIN_API
bool UsdGltfFileFormat::ReadFromString(const SdfLayerBasePtr& layer_base,
    const std::string& str) const {
  // GLTF can use multiple files and the reader assumes those files are on disk.
  TF_RUNTIME_ERROR("Cannot import GLTF from a string in memory.");
  return false;
}
#else  // OLD_FORMAT_PLUGIN_API
bool UsdGltfFileFormat::ReadFromString(SdfLayer* layer,
    const std::string& str) const {
  // GLTF can use multiple files and the reader assumes those files are on disk.
  TF_RUNTIME_ERROR("Cannot import GLTF from a string in memory.");
  return false;
}
#endif  // OLD_FORMAT_PLUGIN_API

#if OLD_FORMAT_PLUGIN_API
bool UsdGltfFileFormat::WriteToString(const SdfLayerBase* layer_base,
    std::string* str, const std::string& comment) const {
  // Write as USDA because we don't implement GLTF export.
  return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)
      ->WriteToString(layer_base, str, comment);
}
#else  // OLD_FORMAT_PLUGIN_API
bool UsdGltfFileFormat::WriteToString(const SdfLayer& layer,
    std::string* str, const std::string& comment) const {
  // Write as USDA because we don't implement GLTF export.
  return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)
      ->WriteToString(layer, str, comment);
}
#endif  // OLD_FORMAT_PLUGIN_API

bool UsdGltfFileFormat::WriteToStream(
    const SdfSpecHandle &spec, std::ostream& out, size_t indent) const {
  // Write as USDA because we don't implement GLTF export.
  return SdfFileFormat::FindById(UsdUsdaFileFormatTokens->Id)
      ->WriteToStream(spec, out, indent);
}

PXR_NAMESPACE_CLOSE_SCOPE
