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

#ifndef GLTF_DISK_STREAM_H_
#define GLTF_DISK_STREAM_H_

#include <stdio.h>
#include <set>
#include "stream.h"  // NOLINT: Silence relative path warning.

// GltfStream implementation that reads from disk.
class GltfDiskStream : public GltfStream {
 public:
  GltfDiskStream(
      GltfLogger* logger, const char* gltf_path, const char* resource_dir);

  std::unique_ptr<std::istream> GetGltfIStream() override;
  bool BufferExists(const Gltf& gltf, Gltf::Id buffer_id) const override;
  bool ImageExists(const Gltf& gltf, Gltf::Id image_id) const override;
  bool IsImageAtPath(const Gltf& gltf, Gltf::Id image_id, const char* dir,
                     const char* name) const override;
  bool ReadBuffer(
      const Gltf& gltf, Gltf::Id buffer_id, size_t start, size_t limit,
      std::vector<uint8_t>* out_data) override;
  bool ReadImage(const Gltf& gltf, Gltf::Id image_id,
                 std::vector<uint8_t>* out_data,
                 Gltf::Image::MimeType* out_mime_type) override;
  ImageAttributes ReadImageAttributes(
      const Gltf& gltf, Gltf::Id image_id) override;
  bool CopyImage(const Gltf& gltf, Gltf::Id image_id,
                 const char* dst_path) override;
  bool IsSourcePath(const char* path) const override;
  bool WriteBinary(
      const std::string& dst_path, const void* data, size_t size) override;

  bool GlbOpen(const char* path) override;
  bool GlbIsOpen() const override;
  size_t GlbGetFileSize() const override;
  size_t GlbRead(size_t size, void* out_data) override;
  bool GlbSeekRelative(size_t size) override;
  void GlbClose() override;

  // Get canonical absolute path.
  // * If comparable is true, the path is converted to lower-case for
  //   case-insensitive IO on Windows.
  static std::string GetCanonicalPath(const char* path, bool comparable);

  // Like GetCanonicalPath, but also sanitizes invalid characters.
  static std::string GetCanonicalSanitizedPath(
      const char* dir, const char* name, bool comparable);

  // Compare two canonical sanitized paths (split into directory and name).
  static bool SanitizedPathsEqual(const char* dir0, const char* name0,
                                  const char* dir1, const char* name1);

 private:
  std::string gltf_path_;
  std::string path_prefix_;
  std::set<std::string> src_paths_;
  FILE* glb_file_;

  // Read binary file, in the range [start, start+size).
  // * Set 'size' to -1 to read to the end of the file.
  bool ReadBinary(const char* rel_path, size_t start, ptrdiff_t size,
                  std::vector<uint8_t>* out_data);

  bool CopyBinary(const char* src_rel_path, const char* dst_path);
};

#endif  // GLTF_DISK_STREAM_H_
