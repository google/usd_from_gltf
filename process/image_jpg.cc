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

#include "process/image_jpg.h"

#include "gltf/disk_util.h"
#include "gltf/stream.h"
#include "process/math.h"
#include "turbojpeg.h"  // NOLINT: Silence relative path warning.

namespace ufg {
namespace {
struct JpgDecompressor {
  tjhandle handle;
  JpgDecompressor() : handle(tjInitDecompress()) {}
  ~JpgDecompressor() { tjDestroy(handle); }
};

struct JpgCompressor {
  tjhandle handle;
  JpgCompressor() : handle(tjInitCompress()) {}
  ~JpgCompressor() { tjDestroy(handle); }
};

const char* JpgGetErrorStr(tjhandle handle) {
  // TODO: Use tjGetErrorStr2(handle), which is thread-safe but only
  // available in newer versions of the library.
  return tjGetErrorStr();
}
}  // namespace

bool HasJpgHeader(const void* src, size_t src_size) {
  // JPEG doesn't have a fixed header signature, but the first two bytes should
  // always be FF D8. Checking just these two bytes isn't completely accurate,
  // but is unique amongst the image formats we support.
  // See: https://en.wikipedia.org/wiki/JPEG_File_Interchange_Format
  if (src_size < 2) {
    return false;
  }
  const uint8_t* const header = static_cast<const uint8_t*>(src);
  return header[0] == 0xff && header[1] == 0xd8;
}

bool JpgRead(
    const void* src, size_t src_size,
    uint32_t* out_width, uint32_t* out_height, uint8_t* out_channel_count,
    std::vector<Image::Component>* out_buffer, Logger* logger) {
  JpgDecompressor decompressor;
  int width, height, subsamp, colorspace;
  const int header_result = tjDecompressHeader3(decompressor.handle,
      static_cast<const uint8_t*>(src),
      static_cast<unsigned long>(src_size),  // NOLINT
      &width, &height, &subsamp, &colorspace);
  if (header_result != 0) {
    Log<UFG_ERROR_JPG_DECODE_HEADER>(
        logger, "", JpgGetErrorStr(decompressor.handle));
    return false;
  }
  constexpr int kJpgChannelCount = 3;
  const int pitch = width * kJpgChannelCount;
  const int kFormat = TJPF_RGB;
  const int kFlags = 0;
  const size_t dst_size = pitch * height;
  out_buffer->resize(dst_size);
  const int decompress_result = tjDecompress2(decompressor.handle,
      static_cast<const uint8_t*>(src),
      static_cast<unsigned long>(src_size),  // NOLINT
      out_buffer->data(), width, pitch, height, kFormat, kFlags);
  if (decompress_result != 0) {
    Log<UFG_ERROR_JPG_DECOMPRESS>(
        logger, "", JpgGetErrorStr(decompressor.handle));
    return false;
  }
  *out_width = width;
  *out_height = height;
  *out_channel_count = kJpgChannelCount;
  return true;
}

bool JpgWrite(
    const char* path, uint32_t width, uint32_t height, uint8_t channel_count,
    const Image::Component* data, int quality, int subsamp, Logger* logger) {
  // Choose quality and chroma subsampling method.
  quality = Clamp(quality, 1, 100);
  static_assert(TJSAMP_444 == 0 && TJSAMP_422 == 1 && TJSAMP_420 == 2, "");
  subsamp = channel_count == 1 ? TJSAMP_GRAY : Clamp(subsamp, 0, 2);

  // Determine input format.
  // * JPG only supports 1- or 3-channel textures. For 4-channel we discard
  //   alpha, and 2-channel is not used by this library.
  UFG_ASSERT_LOGIC(
      channel_count == 1 || channel_count == 3 || channel_count == 4);
  const int format = channel_count == 1 ?
      TJPF_GRAY : (channel_count == 4 ? TJPF_RGBX : TJPF_RGB);

  const int pitch = width * channel_count;
  unsigned char* jpg_data = nullptr;
  unsigned long jpg_size = 0;  // NOLINT
  JpgCompressor compressor;
  bool success = tjCompress2(
      compressor.handle, data, width, pitch, height, format,
      &jpg_data, &jpg_size, subsamp, quality, TJFLAG_FASTDCT) == 0;
  if (!success) {
    Log<UFG_ERROR_JPG_COMPRESS>(logger, "", JpgGetErrorStr(compressor.handle));
  }
  if (success) {
    success = GltfDiskWriteBinary(path, jpg_data, jpg_size);
    if (!success) {
      Log<UFG_ERROR_IO_WRITE_IMAGE>(logger, "", path);
    }
  }
  tjFree(jpg_data);
  return success;
}
}  // namespace ufg
