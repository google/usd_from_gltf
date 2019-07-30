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

#ifndef GLTF_GLB_STREAM_H_
#define GLTF_GLB_STREAM_H_

#include "stream.h"  // NOLINT: Silence relative path warning.

struct GlbFileHeader {
  static constexpr uint32_t kMagic = 0x46546c67u;  // 'glTF'
  static constexpr uint32_t kVersion = 2;
  uint32_t magic;
  uint32_t version;
  uint32_t length;
};

struct GlbChunkHeader {
  static constexpr uint32_t kTypeBin = 0x004e4942u;  // 'BIN'
  static constexpr uint32_t kTypeJson = 0x4e4f534au;  // 'JSON'
  uint32_t length;
  uint32_t type;
};

bool IsGlbFilePath(const char* path);

// GltfStream implementation that reads from a GLB file.
class GltfGlbStream : public GltfStream {
 public:
  // Open a GLB, wrapping another stream.
  GltfGlbStream(GltfStream* impl_stream, bool own_impl, const char* gltf_path);

  // Open a GLB from a file on disk.
  GltfGlbStream(
      GltfLogger* logger, const char* gltf_path, const char* resource_dir);

  // Open a GLB from a file in memory.
  GltfGlbStream(
      GltfLogger* logger, const void* data, size_t size, const char* log_name);

  ~GltfGlbStream() override;
  bool IsOpen() const;
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
  bool WriteBinary(const std::string& dst_path,
                   const void* data, size_t size) override;

 private:
  struct ChunkInfo {
    size_t start;
    size_t size;
  };
  GltfStream* impl_stream_;
  bool own_impl_;
  std::string gltf_path_;
  ChunkInfo gltf_chunk_info_ = {};
  std::vector<ChunkInfo> bin_chunk_infos_;
  void Open(const char* gltf_path);
  bool ReadChunk(size_t start, size_t size, void* out_data);
};

#endif  // GLTF_GLB_STREAM_H_
