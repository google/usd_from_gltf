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

#include "stream.h"  // NOLINT: Silence relative path warning.

#include "disk_stream.h"  // NOLINT: Silence relative path warning.
#include "glb_stream.h"  // NOLINT: Silence relative path warning.

std::unique_ptr<GltfStream> GltfStream::Open(
    GltfLogger* logger, const char* gltf_path, const char* resource_dir) {
  if (IsGlbFilePath(gltf_path)) {
    std::unique_ptr<GltfGlbStream> stream(
        new GltfGlbStream(logger, gltf_path, resource_dir));
    return stream->IsOpen() ?
        std::unique_ptr<GltfStream>(stream.release()) : nullptr;
  } else {
    return std::unique_ptr<GltfStream>(
        new GltfDiskStream(logger, gltf_path, resource_dir));
  }
}

std::unique_ptr<std::istream> GltfStream::GetGltfIStream() {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GetGltfIStream");
  return nullptr;
}

bool GltfStream::BufferExists(const Gltf& gltf, Gltf::Id buffer_id) const {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("BufferExists");
  return false;
}

bool GltfStream::ImageExists(const Gltf& gltf, Gltf::Id image_id) const {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("ImageExists");
  return false;
}

bool GltfStream::IsImageAtPath(
    const Gltf& gltf, Gltf::Id image_id,
    const char* dir, const char* name) const {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("IsImageAtPath");
  return false;
}

bool GltfStream::ReadBuffer(
    const Gltf& gltf, Gltf::Id buffer_id, size_t start, size_t limit,
    std::vector<uint8_t>* out_data) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("ReadBuffer");
  return false;
}

bool GltfStream::ReadImage(
    const Gltf& gltf, Gltf::Id image_id,
    std::vector<uint8_t>* out_data, Gltf::Image::MimeType* out_mime_type) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("ReadImage");
  return false;
}

GltfStream::ImageAttributes GltfStream::ReadImageAttributes(
    const Gltf& gltf, Gltf::Id image_id) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("ReadImageAttributes");
  return GltfStream::ImageAttributes();
}

bool GltfStream::CopyImage(
    const Gltf& gltf, Gltf::Id image_id, const char* dst_path) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("CopyImage");
  return false;
}

bool GltfStream::IsSourcePath(const char* path) const {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("IsSourcePath");
  return false;
}

bool GltfStream::WriteBinary(
    const std::string& dst_path, const void* data, size_t size) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("WriteBinary");
  return false;
}

bool GltfStream::GlbOpen(const char* path) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GlbOpen");
  return false;
}

bool GltfStream::GlbIsOpen() const {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GlbIsOpen");
  return false;
}

size_t GltfStream::GlbGetFileSize() const {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GlbGetFileSize");
  return 0;
}

size_t GltfStream::GlbRead(size_t size, void* out_data) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GlbRead");
  return 0;
}

bool GltfStream::GlbSeekRelative(size_t size) {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GlbSeekRelative");
  return false;
}

void GltfStream::GlbClose() {
  Log<GLTF_ERROR_NOT_IMPLEMENTED>("GlbClose");
}
