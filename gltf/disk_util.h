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

#ifndef GLTF_DISK_UTIL_H_
#define GLTF_DISK_UTIL_H_

#include <stdio.h>
#include <string>
#include <vector>

// Creates a directory for a file (including any necessary ancestors). Returns
// paths of the directories created, in the order they were created.
std::vector<std::string> GltfDiskCreateDirectoryForFile(
    const std::string& file_path);

// Write a whole binary file to disk.
bool GltfDiskWriteBinary(const std::string& dst_path,
                         const void* data, size_t size);

struct GltfDiskFileSentry {
  FILE* fp;

  GltfDiskFileSentry() : fp(nullptr) {}

  GltfDiskFileSentry(const char* path, const char* mode) : fp(nullptr) {
    Open(path, mode);
  }

  ~GltfDiskFileSentry() {
    Close();
  }

  void Close() {
    if (fp) {
      fclose(fp);
      fp = nullptr;
    }
  }

  bool Open(const char* path, const char* mode) {
    Close();
    fp = fopen(path, mode);
    return fp != nullptr;
  }
};

#endif  // GLTF_DISK_UTIL_H_
