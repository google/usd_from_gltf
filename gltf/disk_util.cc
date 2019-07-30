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

#include "disk_util.h"  // NOLINT: Silence relative path warning.

#include "internal_util.h"  // NOLINT: Silence relative path warning.

#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else  // _MSC_VER
#include <sys/stat.h>
#endif  // _MSC_VER

namespace {
bool DirectoryExists(const char* path) {
#ifdef _MSC_VER
  const DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY);
#else  // _MSC_VER
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
#endif  // _MSC_VER
}

size_t GetLengthOfExistingDirectoryForFile(const std::string& file_path) {
  for (size_t len = file_path.length(); len > 0; --len) {
    len = file_path.find_last_of("\\/", len);
    if (len == std::string::npos) {
      return 0;
    }
    const std::string dir = file_path.substr(0, len);
    if (DirectoryExists(dir.c_str())) {
      return len + 1;
    }
  }
  return 0;
}
}  // namespace

std::vector<std::string> GltfDiskCreateDirectoryForFile(
    const std::string& file_path) {
  size_t pos = GetLengthOfExistingDirectoryForFile(file_path);
  std::vector<std::string> created_dirs;
  for (;;) {
    pos = file_path.find_first_of("\\/", pos + 1);
    if (pos == std::string::npos) {
      break;
    }
    std::string dir = file_path.substr(0, pos);
    if (!dir.empty()) {
#ifdef _MSC_VER
      CreateDirectoryA(dir.c_str(), NULL);
#else  // _MSC_VER
      mkdir(dir.c_str(), 0777);
#endif  // _MSC_VER
      created_dirs.emplace_back(std::move(dir));
    }
  }
  return created_dirs;
}

bool GltfDiskWriteBinary(const std::string& dst_path,
                         const void* data, size_t size) {
  GltfDiskFileSentry file(dst_path.c_str(), "wb");
  return file.fp && fwrite(data, 1, size, file.fp) == size;
}

