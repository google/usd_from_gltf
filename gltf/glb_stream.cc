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

#include "glb_stream.h"  // NOLINT: Silence relative path warning.

#include <sstream>
#include "disk_stream.h"  // NOLINT: Silence relative path warning.
#include "image_parsing.h"  // NOLINT: Silence relative path warning.
#include "internal_util.h"  // NOLINT: Silence relative path warning.
#include "memory_stream.h"  // NOLINT: Silence relative path warning.

bool IsGlbFilePath(const char* path) {
  return Gltf::StringEndsWithCI(path, strlen(path), ".glb");
}

GltfGlbStream::GltfGlbStream(
    GltfStream* impl_stream, bool own_impl, const char* gltf_path)
    : GltfStream(impl_stream->GetLogger()),
      impl_stream_(impl_stream),
      own_impl_(own_impl) {
  Open(gltf_path);
}

GltfGlbStream::GltfGlbStream(
    GltfLogger* logger, const char* gltf_path, const char* resource_dir)
    : GltfStream(logger),
      impl_stream_(new GltfDiskStream(logger, gltf_path, resource_dir)),
      own_impl_(true) {
  Open(gltf_path);
}

GltfGlbStream::GltfGlbStream(
    GltfLogger* logger, const void* data, size_t size, const char* log_name)
    : GltfStream(logger),
      impl_stream_(new GltfMemoryStream(logger, data, size)),
      own_impl_(true) {
  Open(log_name);
}

GltfGlbStream::~GltfGlbStream() {
  if (impl_stream_ && own_impl_) {
    delete impl_stream_;
  }
}

bool GltfGlbStream::IsOpen() const {
  return !gltf_path_.empty();
}

std::unique_ptr<std::istream> GltfGlbStream::GetGltfIStream() {
  std::string text(gltf_chunk_info_.size, 0);
  if (!ReadChunk(gltf_chunk_info_.start, gltf_chunk_info_.size, &text[0])) {
    return nullptr;
  }
  std::unique_ptr<std::istringstream> is(new std::istringstream(text));
  return std::unique_ptr<std::istream>(is.release());
}

bool GltfGlbStream::BufferExists(const Gltf& gltf, Gltf::Id buffer_id) const {
  const Gltf::Buffer* const buffer = Gltf::GetById(gltf.buffers, buffer_id);
  if (!buffer) {
    return false;
  }
  if (buffer->uri.IsSet()) {
    return impl_stream_->BufferExists(gltf, buffer_id);
  }
  const ChunkInfo* const chunk_info =
      Gltf::GetById(bin_chunk_infos_, buffer_id);
  if (!chunk_info) {
    return false;
  }
  if (chunk_info->size < buffer->byteLength) {
    return false;
  }
  return true;
}

bool GltfGlbStream::ImageExists(const Gltf& gltf, Gltf::Id image_id) const {
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    return false;
  }
  if (image->bufferView == Gltf::Id::kNull) {
    return impl_stream_->ImageExists(gltf, image_id);
  }
  if (image->mimeType == Gltf::Image::kMimeUnset) {
    return false;
  }
  const Gltf::BufferView* const view =
      Gltf::GetById(gltf.bufferViews, image->bufferView);
  if (!view) {
    return false;
  }
  return BufferExists(gltf, view->buffer);
}

bool GltfGlbStream::IsImageAtPath(const Gltf& gltf, Gltf::Id image_id,
                                  const char* dir, const char* name) const {
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    return false;
  }
  if (image->bufferView != Gltf::Id::kNull) {
    return false;
  }
  return impl_stream_->IsImageAtPath(gltf, image_id, dir, name);
}

bool GltfGlbStream::ReadBuffer(
    const Gltf& gltf, Gltf::Id buffer_id, size_t start, size_t limit,
    std::vector<uint8_t>* out_data) {
  out_data->clear();
  const Gltf::Buffer* const buffer = Gltf::GetById(gltf.buffers, buffer_id);
  if (!buffer) {
    Log<GLTF_ERROR_BAD_BUFFER_ID>(Gltf::IdToIndex(buffer_id));
    return false;
  }
  if (buffer->uri.IsSet()) {
    return impl_stream_->ReadBuffer(gltf, buffer_id, start, limit, out_data);
  }
  const ChunkInfo* const chunk_info =
      Gltf::GetById(bin_chunk_infos_, buffer_id);
  if (!chunk_info) {
    Log<GLTF_ERROR_GLB_MISSING_CHUNK>(Gltf::IdToIndex(buffer_id));
    return false;
  }
  const size_t size = buffer->byteLength;
  if (size > chunk_info->size) {
    Log<GLTF_ERROR_GLB_SIZE_MISMATCH>(
        Gltf::IdToIndex(buffer_id), size, chunk_info->size);
    return false;
  }
  const size_t chunk_end = chunk_info->start + chunk_info->size;
  const size_t read_start = chunk_info->start + start;
  if (read_start > chunk_end) {
    Log<GLTF_ERROR_GLB_READ_LONG>(
        read_start, Gltf::IdToIndex(buffer_id), chunk_end);
    return false;
  }
  const size_t read_size_max = chunk_end - read_start;
  const size_t read_size =
      limit == 0 ? read_size_max : std::min(limit, read_size_max);
  out_data->resize(read_size);
  if (!ReadChunk(read_start, read_size, out_data->data())) {
    return false;
  }
  return true;
}

bool GltfGlbStream::ReadImage(
    const Gltf& gltf, Gltf::Id image_id,
    std::vector<uint8_t>* out_data, Gltf::Image::MimeType* out_mime_type) {
  // This function is only ever called for URI-based images, because otherwise
  // the cache loads the buffer directly.
  return impl_stream_->ReadImage(gltf, image_id, out_data, out_mime_type);
}

struct ParseImageReadContext {
  GltfStream* stream;
  const Gltf* gltf;
  Gltf::Id buffer_id;
  size_t view_offset;
  size_t view_size;
};

static bool ParseImageRead(void* user_context, size_t start, size_t limit,
                           std::vector<uint8_t>* out_data) {
  const ParseImageReadContext& ctx =
      *static_cast<ParseImageReadContext*>(user_context);
  if (start > ctx.view_size) {
    return false;
  }
  const size_t read_offset = ctx.view_offset + start;
  const size_t read_size_max = ctx.view_size - start;
  const size_t read_size = std::min(limit, read_size_max);
  return ctx.stream->ReadBuffer(
      *ctx.gltf, ctx.buffer_id, read_offset, read_size, out_data);
}

GltfStream::ImageAttributes GltfGlbStream::ReadImageAttributes(
    const Gltf& gltf, Gltf::Id image_id) {
  GltfStream::ImageAttributes attrs;
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    return attrs;
  }

  // If the image doesn't have a buffer view, it's not in the GLB. Fall back to
  // loading from the URI.
  if (image->bufferView == Gltf::Id::kNull) {
    return impl_stream_->ReadImageAttributes(gltf, image_id);
  }

  // Read image header from the buffer in the GLB.
  const Gltf::BufferView* const view =
      Gltf::GetById(gltf.bufferViews, image->bufferView);
  if (!view) {
    return attrs;
  }

  const std::string name = "image" + std::to_string(Gltf::IdToIndex(image_id));
  attrs.exists = true;
  attrs.file_type = image->mimeType;
  attrs.file_size = view->byteLength;

  ParseImageReadContext ctx =
      {this, &gltf, view->buffer, view->byteOffset, view->byteLength};
  attrs.real_type =
      GltfParseImage(ParseImageRead, &ctx, name.c_str(), GetLogger(),
                     &attrs.width, &attrs.height);
  return attrs;
}

bool GltfGlbStream::CopyImage(const Gltf& gltf, Gltf::Id image_id,
                              const char* dst_path) {
  // This function is only ever called for URI-based images, because otherwise
  // the cache loads the buffer directly.
  return impl_stream_->CopyImage(gltf, image_id, dst_path);
}

bool GltfGlbStream::IsSourcePath(const char* path) const {
  return impl_stream_->IsSourcePath(path);
}

bool GltfGlbStream::WriteBinary(const std::string& dst_path,
                                const void* data, size_t size) {
  return impl_stream_->WriteBinary(dst_path, data, size);
}

void GltfGlbStream::Open(const char* gltf_path) {
  GlbSentry sentry(impl_stream_, gltf_path);
  if (!impl_stream_->GlbIsOpen()) {
    Log<GLTF_ERROR_IO_OPEN_READ>(gltf_path);
    return;
  }

  // The file must be at least large enough to contain the header and the JSON
  // chunk.
  constexpr size_t kSizeMin = sizeof(GlbFileHeader) + sizeof(GlbChunkHeader);
  const size_t file_size = impl_stream_->GlbGetFileSize();
  if (file_size < kSizeMin) {
    Log<GLTF_ERROR_GLB_FILE_TOO_SMALL>(file_size, kSizeMin, gltf_path);
    return;
  }

  // Read and validate file header.
  GlbFileHeader file_header;
  if (impl_stream_->GlbRead(sizeof(file_header), &file_header) !=
      sizeof(file_header)) {
    Log<GLTF_ERROR_IO_READ>(sizeof(file_header), 0, gltf_path);
    return;
  }
  if (file_header.magic != GlbFileHeader::kMagic) {
    Log<GLTF_ERROR_GLB_BAD_FORMAT>(
        file_header.magic, GlbFileHeader::kMagic, gltf_path);
    return;
  }
  if (file_header.version != GlbFileHeader::kVersion) {
    Log<GLTF_ERROR_GLB_BAD_VERSION>(
        file_header.version, GlbFileHeader::kVersion, gltf_path);
    return;
  }
  if (file_header.length > file_size) {
    Log<GLTF_ERROR_GLB_BAD_SIZE>(
        file_header.length, file_size, gltf_path);
    return;
  }

  // Locate and validate chunks.
  ChunkInfo gltf_chunk_info = {};
  std::vector<ChunkInfo> bin_chunk_infos;
  size_t offset = sizeof(file_header);
  for (size_t index = 0; offset < file_header.length; ++index) {
    // Read the chunk header.
    GlbChunkHeader chunk_header;
    if (impl_stream_->GlbRead(sizeof(chunk_header), &chunk_header) !=
        sizeof(chunk_header)) {
      Log<GLTF_ERROR_IO_READ>(sizeof(chunk_header), offset, gltf_path);
      return;
    }
    offset += sizeof(chunk_header);

    // Validate length.
    const size_t remain = file_size - offset;
    if (chunk_header.length > remain) {
      Log<GLTF_ERROR_GLB_CHUNK_TOO_LARGE>(
          index, offset - sizeof(chunk_header), chunk_header.length, remain,
          gltf_path);
      return;
    }

    // Add chunk info based on type.
    const ChunkInfo chunk_info = { offset, chunk_header.length };
    switch (chunk_header.type) {
    case GlbChunkHeader::kTypeJson:
      if (index != 0) {
        // The spec indicates the first chunk must be glTF, so skip any
        // extraneous ones.
        Log<GLTF_WARN_GLB_EXTRA_CHUNK>(
            index, offset, chunk_header.length, gltf_path);
      } else {
        gltf_chunk_info = chunk_info;
      }
      break;
    case GlbChunkHeader::kTypeBin:
      bin_chunk_infos.push_back(chunk_info);
      break;
    default:
      Log<GLTF_INFO_GLB_UNKNOWN_CHUNK>(
          index, chunk_header.type, offset, chunk_header.length, gltf_path);
      break;
    }

    // Seek to the next chunk.
    if (!impl_stream_->GlbSeekRelative(chunk_header.length)) {
      Log<GLTF_ERROR_IO_SEEK>(offset + chunk_header.length, gltf_path);
      return;
    }
    offset += chunk_header.length;
  }

  // The glTF chunk is required.
  if (gltf_chunk_info.size == 0) {
    Log<GLTF_ERROR_GLB_NO_GLTF_CHUNK>(gltf_path);
  }

  // Success.
  gltf_path_ = gltf_path;
  gltf_chunk_info_ = gltf_chunk_info;
  bin_chunk_infos_.swap(bin_chunk_infos);
}

bool GltfGlbStream::ReadChunk(size_t start, size_t size, void* out_data) {
  GlbSentry sentry(impl_stream_, gltf_path_.c_str());
  if (!impl_stream_->GlbIsOpen()) {
    Log<GLTF_ERROR_IO_OPEN_READ>(gltf_path_.c_str());
    return false;
  }
  if (!impl_stream_->GlbSeekRelative(start)) {
    Log<GLTF_ERROR_IO_SEEK>(start, gltf_path_.c_str());
    return false;
  }
  const size_t read_size = impl_stream_->GlbRead(size, out_data);
  if (read_size != size) {
    Log<GLTF_ERROR_IO_READ>(size, start, gltf_path_.c_str());
    return false;
  }
  return true;
}
