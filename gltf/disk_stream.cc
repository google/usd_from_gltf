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

#include "disk_stream.h"  // NOLINT: Silence relative path warning.

#include <fstream>
#include "disk_util.h"  // NOLINT: Silence relative path warning.
#include "image_parsing.h"  // NOLINT: Silence relative path warning.
#include "internal_util.h"  // NOLINT: Silence relative path warning.

#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else  // _MSC_VER
#include <sys/stat.h>
#include <unistd.h>
#endif  // _MSC_VER

namespace {
bool FileExists(const char* path) {
#ifdef _MSC_VER
  const DWORD attr = GetFileAttributesA(path);
  return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
#else  // _MSC_VER
  struct stat st;
  return stat(path, &st) == 0 && !S_ISDIR(st.st_mode);
#endif  // _MSC_VER
}

bool FileExistsPossiblySanitized(const std::string& path_prefix,
                                 const char* rel_path) {
  std::string path = path_prefix + rel_path;
  if (FileExists(path.c_str())) {
    return true;
  }

  // Try again with the sanitized path.
  char* const sane_rel_path = &path[path_prefix.length()];
  if (Gltf::SanitizePath(sane_rel_path)) {
    return FileExists(path.c_str());
  }
  return false;
}

#if !_MSC_VER
// POSIX doesn't have a function to canonicalize or get a full path if a file
// for that path doesn't already exist. There is a weakly_canonical() function
// to do this, but it's only available in C++17.
std::string GetCanonicalPathLinux(const char* path) {
  if (!path[0]) {
    return std::string();
  }
  std::string full_path;
  if (path[0] == '/') {
    full_path = path;
  } else {
    char *const dir = getcwd(nullptr, 0);
    full_path = dir ? dir : "";
    full_path += '/';
    full_path += path;
    free(dir);
  }

  const char* const path_end = full_path.c_str() + full_path.length();
  const char* name = full_path.c_str() + 1;  // Skip leading '/'.
  std::string dst;
  for (const char* name_end = name; ; ++name_end) {
    if (name_end != path_end && *name_end != '/') {
      continue;
    }
    const size_t name_len = name_end - name;
    if (name_len == 0) {
      // Skip redundant slashes.
    } else if (name_len == 1 && name[0] == '.') {
      // ".": Skip current directory specifier.
    } else if (name_len == 2 && name[0] == '.' && name[1] == '.') {
      // "..": Traverse back up one level.
      const size_t last_slash_pos = dst.find_last_of('/');
      if (last_slash_pos != std::string::npos) {
        dst.resize(last_slash_pos);
      }
    } else {
      // Add normal path name.
      dst += '/';
      dst.append(name, name_end);
    }
    if (name_end == path_end) {
      break;
    }
    name = name_end + 1;
  }
  return dst.empty() ? "/" : dst;
}
#endif  // !_MSC_VER

bool CopyBinaryFile(const char* src_path, const char* dst_path) {
#ifdef _MSC_VER
  return CopyFileA(src_path, dst_path, FALSE) != FALSE;
#else  // _MSC_VER
  // This doesn't preserve file attributes, but it's portable and relatively
  // simple.
  std::ifstream src_stream(src_path, std::ios::binary);
  std::ofstream dst_stream(dst_path, std::ios::binary);
  if (!src_stream.is_open() || !dst_stream.is_open()) {
    return false;
  }
  return !(dst_stream << src_stream.rdbuf()).fail();
#endif  // _MSC_VER
}
}  // namespace

GltfDiskStream::GltfDiskStream(
    GltfLogger* logger, const char* gltf_path, const char* resource_dir)
    : GltfStream(logger),
      gltf_path_(gltf_path),
      path_prefix_(resource_dir),
      glb_file_(nullptr) {
  if (!path_prefix_.empty() && path_prefix_.back() != '/' &&
      path_prefix_.back() != '\\') {
    path_prefix_ += '/';
  }
}

std::unique_ptr<std::istream> GltfDiskStream::GetGltfIStream() {
  std::unique_ptr<std::ifstream> is(new std::ifstream(gltf_path_));
  return is->is_open() ? std::unique_ptr<std::istream>(is.release()) : nullptr;
}

bool GltfDiskStream::BufferExists(const Gltf& gltf, Gltf::Id buffer_id) const {
  const Gltf::Buffer* const buffer = Gltf::GetById(gltf.buffers, buffer_id);
  if (!buffer) {
    return false;
  }
  if (buffer->uri.data_type != Gltf::Uri::kDataTypeNone) {
    return true;
  }
  if (buffer->uri.path.empty()) {
    return false;
  }
  return FileExistsPossiblySanitized(path_prefix_, buffer->uri.path.c_str());
}

bool GltfDiskStream::ImageExists(const Gltf& gltf, Gltf::Id image_id) const {
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    return false;
  }
  if (image->bufferView == Gltf::Id::kNull) {
    const Gltf::Uri::DataType data_type = image->uri.data_type;
    if (data_type != Gltf::Uri::kDataTypeNone) {
      const Gltf::Image::MimeType mime_type =
          Gltf::GetUriDataImageMimeType(data_type);
      return mime_type != Gltf::Image::kMimeUnset;
    }
    if (image->uri.path.empty()) {
      return false;
    }
    return FileExistsPossiblySanitized(path_prefix_, image->uri.path.c_str());
  } else {
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
}

bool GltfDiskStream::IsImageAtPath(const Gltf& gltf, Gltf::Id image_id,
                                   const char* dir, const char* name) const {
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    return false;
  }
  if (image->bufferView != Gltf::Id::kNull) {
    return false;
  }
  if (image->uri.data_type != Gltf::Uri::kDataTypeNone) {
    return false;
  }
  if (image->uri.path.empty()) {
    return false;
  }
  const std::string image_path = path_prefix_ + image->uri.path;
  return SanitizedPathsEqual(
      path_prefix_.c_str(), image->uri.path.c_str(), dir, name);
}

bool GltfDiskStream::ReadBuffer(
    const Gltf& gltf, Gltf::Id buffer_id, size_t start, size_t limit,
    std::vector<uint8_t>* out_data) {
  const Gltf::Buffer* const buffer = Gltf::GetById(gltf.buffers, buffer_id);
  if (!buffer) {
    Log<GLTF_ERROR_BAD_BUFFER_ID>(Gltf::IdToIndex(buffer_id));
    return false;
  }
  if (buffer->uri.data_type != Gltf::Uri::kDataTypeNone) {
    *out_data = buffer->uri.data;
    return true;
  }
  if (buffer->uri.path.empty()) {
    Log<GLTF_ERROR_BUFFER_NO_PATH>(Gltf::IdToIndex(buffer_id));
    return false;
  }
  if (start > buffer->byteLength) {
    Log<GLTF_ERROR_IO_READ_LONG>(
        start, buffer->byteLength, buffer->uri.path.c_str());
    return false;
  }
  const size_t read_size_max = buffer->byteLength - start;
  const size_t read_size =
      limit == 0 ? read_size_max : std::min(limit, read_size_max);
  return ReadBinary(buffer->uri.path.c_str(), start, read_size, out_data);
}

bool GltfDiskStream::ReadImage(
    const Gltf& gltf, Gltf::Id image_id,
    std::vector<uint8_t>* out_data, Gltf::Image::MimeType* out_mime_type) {
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    Log<GLTF_ERROR_BAD_IMAGE_ID>(Gltf::IdToIndex(image_id));
    return false;
  }

  const Gltf::Image::MimeType mime_type =
      Gltf::FindImageMimeTypeByUri(image->uri);
  if (image->uri.data_type == Gltf::Uri::kDataTypeNone) {
    if (mime_type == Gltf::Image::kMimeUnset) {
      Log<GLTF_ERROR_IMAGE_UNKNOWN_EXTENSION>(image->uri.path.c_str());
      return false;
    }
    if (!ReadBinary(image->uri.path.c_str(), 0, -1, out_data)) {
      return false;
    }
  } else {
    if (mime_type == Gltf::Image::kMimeUnset ||
        mime_type == Gltf::Image::kMimeOther) {
      Log<GLTF_ERROR_IMAGE_UNKNOWN_TYPE>();
      return false;
    }
    *out_data = image->uri.data;
  }
  *out_mime_type = mime_type;
  return true;
}

GltfStream::ImageAttributes GltfDiskStream::ReadImageAttributes(
    const Gltf& gltf, Gltf::Id image_id) {
  GltfStream::ImageAttributes attrs;
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    return attrs;
  }
  if (image->uri.data_type == Gltf::Uri::kDataTypeNone) {
    // File is specified by path.
    attrs.path = image->uri.path;

    // Open the file, sanitizing the path if necessary.
    std::string path = path_prefix_ + image->uri.path;
    GltfDiskFileSentry file(path.c_str(), "rb");
    if (!file.fp) {
      // Try again with the sanitized path.
      char* const sane_rel_path = &path[path_prefix_.length()];
      if (Gltf::SanitizePath(sane_rel_path)) {
        file.Open(path.c_str(), "rb");
      }
      if (!file.fp) {
        return attrs;
      }
      attrs.path = sane_rel_path;
      attrs.unsanitized_path = image->uri.path;
    }

    attrs.exists = true;
    attrs.file_type = Gltf::FindImageMimeTypeByUri(image->uri);
    attrs.file_size = GetFileSize(file.fp);

    // Read remaining attributes from the header.
    attrs.real_type =
        GltfParseImage(file.fp, image->uri.path.c_str(), GetLogger(),
                       &attrs.width, &attrs.height);
  } else {
    const std::string name =
        "image" + std::to_string(Gltf::IdToIndex(image_id));
    attrs.exists = true;
    attrs.file_type = Gltf::GetUriDataImageMimeType(image->uri.data_type);
    attrs.file_size = image->uri.data.size();
    attrs.real_type = GltfParseImage(
        image->uri.data.data(), image->uri.data.size(), name.c_str(),
        GetLogger(), &attrs.width, &attrs.height);
  }
  return attrs;
}

bool GltfDiskStream::CopyImage(const Gltf& gltf, Gltf::Id image_id,
                               const char* dst_path) {
  const Gltf::Image* const image = Gltf::GetById(gltf.images, image_id);
  if (!image) {
    Log<GLTF_ERROR_BAD_IMAGE_ID>(Gltf::IdToIndex(image_id));
    return false;
  }
  if (image->uri.data_type == Gltf::Uri::kDataTypeNone) {
    if (image->uri.path.empty()) {
      Log<GLTF_ERROR_IMAGE_NO_URI>(Gltf::IdToIndex(image_id));
      return false;
    }
    if (!CopyBinary(image->uri.path.c_str(), dst_path)) {
      Log<GLTF_ERROR_IO_COPY>(
          path_prefix_.c_str(), image->uri.path.c_str(), dst_path);
      return false;
    }
  } else {
    if (!WriteBinary(dst_path,
                     image->uri.data.data(), image->uri.data.size())) {
      Log<GLTF_ERROR_IO_WRITE_FILE>(dst_path);
      return false;
    }
  }
  return true;
}

bool GltfDiskStream::IsSourcePath(const char* path) const {
  const std::string key = GetCanonicalPath(path, true);
  return src_paths_.find(key) != src_paths_.end();
}

bool GltfDiskStream::WriteBinary(const std::string& dst_path,
                                 const void* data, size_t size) {
  GltfDiskFileSentry file(dst_path.c_str(), "wb");
  if (!file.fp) {
    Log<GLTF_ERROR_IO_OPEN_WRITE>(dst_path.c_str());
    return false;
  }
  if (fwrite(data, 1, size, file.fp) != size) {
    Log<GLTF_ERROR_IO_WRITE>(size, dst_path.c_str());
    return false;
  }
  return true;
}

bool GltfDiskStream::GlbOpen(const char* path) {
  GlbClose();
  glb_file_ = fopen(path, "rb");
  return glb_file_ != nullptr;
}

bool GltfDiskStream::GlbIsOpen() const {
  return glb_file_ != nullptr;
}

size_t GltfDiskStream::GlbGetFileSize() const {
  return glb_file_ ? GetFileSize(glb_file_) : 0;
}

size_t GltfDiskStream::GlbRead(size_t size, void* out_data) {
  return fread(out_data, 1, size, glb_file_);
}

bool GltfDiskStream::GlbSeekRelative(size_t size) {
  const long size_long = static_cast<long>(size);  // NOLINT
  if (size_long < 0 || static_cast<size_t>(size_long) != size) {
    return false;
  }
  return fseek(glb_file_, size_long, SEEK_CUR) == 0;
}

void GltfDiskStream::GlbClose() {
  if (glb_file_) {
    fclose(glb_file_);
    glb_file_ = nullptr;
  }
}

std::string GltfDiskStream::GetCanonicalPath(
    const char* path, bool comparable) {
#ifdef _MSC_VER
  char full_path[_MAX_PATH];
  GetFullPathNameA(path, sizeof(full_path), full_path, nullptr);
  if (comparable) {
    Gltf::StringToLower(full_path);
  }
  return full_path;
#else  // _MSC_VER
  return GetCanonicalPathLinux(path);
#endif  // _MSC_VER
}

std::string GltfDiskStream::GetCanonicalSanitizedPath(
    const char* dir, const char* name, bool comparable) {
  std::string sane_name = Gltf::GetSanitizedPath(name);
  const std::string sane_path = Gltf::JoinPath(dir, sane_name);
  return GetCanonicalPath(sane_path.c_str(), comparable);
}

bool GltfDiskStream::SanitizedPathsEqual(const char* dir0, const char* name0,
                                         const char* dir1, const char* name1) {
  const std::string path0 = GetCanonicalSanitizedPath(dir0, name0, true);
  const std::string path1 = GetCanonicalSanitizedPath(dir1, name1, true);
  return path0 == path1;
}

bool GltfDiskStream::ReadBinary(
    const char* rel_path, size_t start, ptrdiff_t size,
    std::vector<uint8_t>* out_data) {
  std::string path = path_prefix_ + rel_path;
  GltfDiskFileSentry file(path.c_str(), "rb");
  if (!file.fp) {
    // Try again with the sanitized path.
    char* const sane_rel_path = &path[path_prefix_.length()];
    if (Gltf::SanitizePath(sane_rel_path)) {
      file.Open(path.c_str(), "rb");
    }
    if (!file.fp) {
      Log<GLTF_ERROR_IO_OPEN_READ>(path.c_str());
      return false;
    }
  }

  // Record the source path for IsSourcePath checks.
  src_paths_.insert(GetCanonicalPath(path.c_str(), true));

  if (size < 0) {
    const size_t file_size = GetFileSize(file.fp);
    if (start > file_size) {
      Log<GLTF_ERROR_IO_READ_LONG>(start, file_size, path.c_str());
      return false;
    }
    size = file_size - start;
  }
  if (start != 0 && !SeekAbsolute(file.fp, start)) {
    Log<GLTF_ERROR_IO_SEEK>(start, path.c_str());
    return false;
  }
  std::vector<uint8_t> data(size);
  const size_t read_size = fread(data.data(), 1, size, file.fp);
  if (read_size != size) {
    Log<GLTF_ERROR_IO_READ>(size, start, path.c_str());
    return false;
  }
  out_data->swap(data);
  return true;
}

bool GltfDiskStream::CopyBinary(const char* src_rel_path,
                                const char* dst_path) {
  std::string src_path = path_prefix_ + src_rel_path;
  if (CopyBinaryFile(src_path.c_str(), dst_path)) {
    src_paths_.insert(GetCanonicalPath(src_path.c_str(), true));
    return true;
  }
  // Try again with the sanitized path.
  char* const sane_src_rel_path = &src_path[path_prefix_.length()];
  if (Gltf::SanitizePath(sane_src_rel_path)) {
    src_paths_.insert(GetCanonicalPath(src_path.c_str(), true));
    return CopyBinaryFile(src_path.c_str(), dst_path);
  }
  return false;
}
