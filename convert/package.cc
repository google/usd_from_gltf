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

#include "convert/package.h"

#include "common/common_util.h"
#include "convert/converter.h"
#include "gltf/gltf.h"
#include "gltf/message.h"
#include "gltf/validate.h"
#include "pxr/base/plug/plugin.h"
#include "pxr/base/plug/registry.h"
#include "pxr/base/tf/diagnosticMgr.h"
#include "pxr/usd/usdUtils/dependencies.h"

#ifdef _MSC_VER
#include <direct.h>
#endif  // _MSC_VER

#if defined(_MSC_VER) && defined(USD_STATICALLY_LINKED)
PXR_NAMESPACE_OPEN_SCOPE
void Arch_InitTmpDir();
PXR_NAMESPACE_CLOSE_SCOPE
#endif  // defined(_MSC_VER) && defined(USD_STATICALLY_LINKED)

namespace ufg {
namespace {
using PXR_NS::GfHalf;
using PXR_NS::PlugPluginPtr;
using PXR_NS::PlugRegistry;
using PXR_NS::SdfAssetPath;
using PXR_NS::SdfLayer;
using PXR_NS::TfCallContext;
using PXR_NS::TfDiagnosticMgr;
using PXR_NS::TfError;
using PXR_NS::TfStatus;
using PXR_NS::TfType;
using PXR_NS::TfWarning;

void UfgDeleteFile(const char* path, Logger* logger) {
  if (remove(path) != 0) {
    Log<UFG_WARN_IO_DELETE>(logger, "", path);
  }
}

void UfgDeleteDirectory(const char* path) {
  // This will fail if the directory is non-empty. Just ignore it because it's
  // non-critical and it's likely to happen for the root output directory (which
  // contains the usd file and shouldn't be deleted).
#ifdef _MSC_VER
  _rmdir(path);
#else  // _MSC_VER
  rmdir(path);
#endif  // _MSC_VER
}

// Search system paths for a file.
std::string FindFileInSearchPaths(const char* file_name) {
#if _MSC_VER
  char path[_MAX_PATH];
  const DWORD found_len =
      SearchPathA(nullptr, file_name, nullptr, sizeof(path), path, nullptr);
  return found_len > 0 ? path : "";
#else  // _MSC_VER
  // TODO: This doesn't resolve system paths or executable-relative
  // paths.
  char* const path = realpath(file_name, nullptr);
  const std::string path_str = path ? path : "";
  if (path) {
    free(path);
  }
  return path_str;
#endif  // _MSC_VER
}

std::string GetDefaultPluginPath() {
  const std::string info_path = FindFileInSearchPaths("usd/plugInfo.json");
  const std::string dir = GetFileDirectory(info_path);
  return dir.empty() ? std::string() : (dir + "/*");
}

// Utility to clean up generated files on convert completion.
struct CleanerSentry {
  const Converter* converter;
  Logger* logger;

  CleanerSentry(const Converter* converter, Logger* logger)
      : converter(converter), logger(logger) {}

  ~CleanerSentry() {
    if (!converter) {
      return;
    }

    // Delete written files.
    const std::vector<std::string>& written = converter->GetWritten();
    for (const std::string& path : written) {
      UfgDeleteFile(path.c_str(), logger);
    }

    // Delete directories in the reverse order they were created so they're
    // empty when we attempt to delete them.
    const std::vector<std::string>& created_dirs =
        converter->GetCreatedDirectories();
    for (size_t i = created_dirs.size(); i != 0;) {
      --i;
      UfgDeleteDirectory(created_dirs[i].c_str());
    }
  }

  void KeepFiles() {
    converter = nullptr;
  }
};

// Redirects USD messages to our own logging system in a local function scope.
class UsdMessageHandler : public TfDiagnosticMgr::Delegate {
 public:
  explicit UsdMessageHandler(Logger* logger) : logger_(logger) {
    TfDiagnosticMgr& diagnostics = TfDiagnosticMgr::GetInstance();
    diagnostics.SetQuiet(true);
    diagnostics.AddDelegate(this);
  }

  ~UsdMessageHandler() override {
    TfDiagnosticMgr& diagnostics = TfDiagnosticMgr::GetInstance();
    diagnostics.SetQuiet(false);
    diagnostics.RemoveDelegate(this);
  }

  void IssueError(const TfError &err) override {
    const char* const commentary = err.GetCommentary().c_str();
    const char* const function = err.GetContext().GetFunction();
    Log<UFG_ERROR_USD>(logger_, "", commentary, function);
  }

  void IssueFatalError(const TfCallContext& context,
                       const std::string& msg) override {
    const char* const commentary = msg.c_str();
    const char* const function = context.GetFunction();
    Log<UFG_ERROR_USD_FATAL>(logger_, "", commentary, function);
  }

  void IssueStatus(const TfStatus &status) override {
    const char* const commentary = status.GetCommentary().c_str();
    const char* const function = status.GetContext().GetFunction();
    Log<UFG_INFO_USD>(logger_, "", commentary, function);
  }

  void IssueWarning(const TfWarning &warning) override {
    const char* const commentary = warning.GetCommentary().c_str();
    const char* const function = warning.GetContext().GetFunction();
    Log<UFG_WARN_USD>(logger_, "", commentary, function);
  }

 private:
  Logger* logger_ = nullptr;
};
}  // namespace

bool RegisterPlugins(const std::string& path, Logger* logger) {
  UsdMessageHandler usd_message_handler(logger);

  std::string search_path;
  PlugRegistry& registry = PlugRegistry::GetInstance();
  if (!path.empty()) {
    search_path = path;
    registry.RegisterPlugins(path);
  } else {
    const PlugPluginPtr usd_plugin = registry.GetPluginWithName("usd");
    if (!usd_plugin) {
      // Expected plugins aren't registered. Search system paths.
      search_path = GetDefaultPluginPath();
      if (!search_path.empty()) {
        registry.RegisterPlugins(search_path);
      }
    }
  }

  if (!registry.GetPluginWithName("usd")) {
    const std::string why =
        search_path.empty() ? "Could not locate plugin search path."
                            : "No plugins found at search path: " + search_path;
    Log<UFG_ERROR_LOAD_PLUGINS>(logger, "", why.c_str());
    return false;
  }

  // In statically-linked builds of the USD library this type doesn't get
  // automatically registered because it relies on a static constructor being
  // called in an otherwise unreferenced object file (gf/half.cpp).
  if (TfType::Find<GfHalf>() == TfType::GetUnknownType()) {
    TfType::Define<GfHalf>();
  }

#if defined(_MSC_VER) && defined(USD_STATICALLY_LINKED)
  // Same issue as with the GfHalf registration: This function should be called
  // via a static constructor in arch/initConfig.cpp, but it's getting stripped
  // because the object file is not otherwise referenced.
  // * Note, there are other setup functions called in initConfig.cpp, but they
  //   don't affect us.
  PXR_NS::Arch_InitTmpDir();
#endif  // defined(_MSC_VER) && defined(USD_STATICALLY_LINKED)
  return true;
}

bool ConvertGltfToUsd(const char* src_gltf_path, const char* dst_usd_path,
                      const ConvertSettings& settings, Logger* logger) {
  UsdMessageHandler usd_message_handler(logger);

  std::string src_dir, src_name;
  Gltf::SplitPath(src_gltf_path, &src_dir, &src_name);

  std::unique_ptr<GltfStream> gltf_stream =
      GltfStream::Open(logger, src_gltf_path, src_dir.c_str());
  if (!gltf_stream) {
    return false;
  }

  Gltf gltf;
  if (!GltfLoadAndValidate(gltf_stream.get(), src_gltf_path,
                           settings.gltf_load_settings, &gltf, logger)) {
    return false;
  }

  const bool is_both = Gltf::StringEndsWithCI(dst_usd_path, ".usd-");
  const bool is_usdz = is_both ||
      Gltf::StringEndsWithCI(dst_usd_path, ".usdz");

  std::string dst_path, dst_usdz_path, dst_usda_path;
  if (is_usdz) {
    dst_path.assign(dst_usd_path,
                    strlen(dst_usd_path) - UFG_CONST_STRLEN(".usdz"));
    dst_usdz_path = dst_path + ".usdz";
    dst_usda_path = dst_path + ".usda";
    dst_path += ".usdc";
  } else {
    dst_path = dst_usd_path;
  }
  std::string dst_dir, dst_name;
  Gltf::SplitPath(dst_path, &dst_dir, &dst_name);
  const SdfLayerRefPtr gltf_layer = SdfLayer::CreateAnonymous(src_name);
  if (!gltf_layer) {
    Log<UFG_ERROR_LAYER_CREATE>(logger, "", src_name.c_str(), dst_path.c_str());
    return false;
  }
  Converter converter;
  const bool convert_success = converter.Convert(
      settings, gltf, gltf_stream.get(), src_dir, dst_dir, gltf_layer, logger);
  CleanerSentry cleaner_sentry(&converter, logger);
  if (!convert_success) {
    // Error message already logged on failure.
    return false;
  }

  gltf_layer->Export(dst_path);

  // Save again as USDA.
  // * Note, this has to occur before packing to USDZ, because
  //   UsdUtilsCreateNewARKitUsdzPackage somehow modifies resource paths of the
  //   currently open layer.
  if (is_both) {
    if (!gltf_layer->Export(dst_usda_path)) {
      Log<UFG_ERROR_IO_WRITE_USD>(logger, "", dst_usda_path.c_str());
      return false;
    }
  }

  if (is_usdz) {
    // The package function will encode full paths in the zip if the package
    // path is not under the current working directory (even when both the
    // package and the contents are in the same directory). This breaks the iOS
    // viewer because it requires package contents to be at the root.  So change
    // the current working directory to work around this.
    const std::string old_dir = GetCwd();
    SetCwd(dst_dir.c_str());
    if (!UsdUtilsCreateNewARKitUsdzPackage(SdfAssetPath(dst_name),
                                           GetFileName(dst_usdz_path))) {
      Log<UFG_ERROR_IO_WRITE_USD>(logger, "", dst_usdz_path.c_str());
      return false;
    }
    SetCwd(old_dir.c_str());

    // Delete unused USDC file.
    if (settings.delete_unused || settings.delete_generated) {
      UfgDeleteFile(dst_path.c_str(), logger);
    }
  }

  // Keep generated files on success if requested.
  const bool is_usda = !is_usdz || is_both;
  if (!settings.delete_generated && (!settings.delete_unused || is_usda)) {
    cleaner_sentry.KeepFiles();
  }

  return true;
}
}  // namespace ufg
