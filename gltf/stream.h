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

#ifndef GLTF_STREAM_H_
#define GLTF_STREAM_H_

#include <stddef.h>
#include <functional>
#include <istream>
#include <memory>
#include <string>
#include <vector>
#include "gltf.h"  // NOLINT: Silence relative path warning.
#include "message.h"  // NOLINT: Silence relative path warning.

// Stream interface to abstract access to bin and image files.
class GltfStream {
 public:
  struct ImageAttributes {
    // True if the image data actually exists (whether or not it contains valid
    // image data).
    bool exists = false;
    // The type of the image indicated by its file name (or MIME type in the
    // glTF for embedded images).
    Gltf::Image::MimeType file_type = Gltf::Image::kMimeUnset;
    // The real image type as indicated by header data. If this doesn't match
    // file_type, this is not a compliant glTF (though usd_from_gltf and some
    // renderers may handle it anyway).
    Gltf::Image::MimeType real_type = Gltf::Image::kMimeUnset;
    // Image width.
    uint32_t width = 0;
    // Image height.
    uint32_t height = 0;
    // File size of the compressed source image.
    size_t file_size = 0;
    // Relative path of the image. Only set if the image is path-based (as
    // opposed to embedded in a data URI or GLB).
    std::string path;
    // If the path had to be sanitized to be located, this is set to the
    // original unsanitized path.
    std::string unsanitized_path;
  };

  // Open a stream from either a glTF on disk or packed in a GLB.
  static std::unique_ptr<GltfStream> Open(
      GltfLogger* logger, const char* gltf_path, const char* resource_dir);

  virtual ~GltfStream() {}

  virtual std::unique_ptr<std::istream> GetGltfIStream();
  virtual bool BufferExists(const Gltf& gltf, Gltf::Id buffer_id) const;
  virtual bool ImageExists(const Gltf& gltf, Gltf::Id image_id) const;
  virtual bool IsImageAtPath(
      const Gltf& gltf, Gltf::Id image_id,
      const char* dir, const char* name) const;

  virtual bool ReadBuffer(
      const Gltf& gltf, Gltf::Id buffer_id, size_t start, size_t limit,
      std::vector<uint8_t>* out_data);
  virtual bool ReadImage(
      const Gltf& gltf, Gltf::Id image_id,
      std::vector<uint8_t>* out_data, Gltf::Image::MimeType* out_mime_type);
  virtual ImageAttributes ReadImageAttributes(
      const Gltf& gltf, Gltf::Id image_id);
  virtual bool CopyImage(
      const Gltf& gltf, Gltf::Id image_id, const char* dst_path);

  virtual bool IsSourcePath(const char* path) const;

  virtual bool WriteBinary(const std::string& dst_path,
                           const void* data, size_t size);

  // Open/Read/Close the backing GLB file.
  // This is used by GlbStream to decode GLB packets from another stream (from
  // disk or in memory).
  virtual bool GlbOpen(const char* path);
  virtual bool GlbIsOpen() const;
  virtual size_t GlbGetFileSize() const;
  virtual size_t GlbRead(size_t size, void* out_data);
  virtual bool GlbSeekRelative(size_t size);
  virtual void GlbClose();

  struct GlbSentry {
    GltfStream* stream;
    explicit GlbSentry(GltfStream* stream, const char* path) : stream(stream) {
      stream->GlbOpen(path);
    }
    ~GlbSentry() {
      if (stream->GlbIsOpen()) {
        stream->GlbClose();
      }
    }
  };

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) const {
    logger_->Add(GltfGetMessage<kWhat>("", args...));
  }

  GltfLogger* GetLogger() const { return logger_; }

 protected:
  GltfLogger* logger_;

  explicit GltfStream(GltfLogger* logger) : logger_(logger) {}
};

#endif  // GLTF_STREAM_H_
