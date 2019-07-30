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

#include "process/image_png.h"

#include "png.h"  // NOLINT: Silence relative path warning.
#include "process/math.h"

namespace ufg {
namespace {
constexpr int kPngBitDepth = 8;

static void DecodeErrorCallback(png_struct* png, const char* message) {
  Logger* const logger = static_cast<Logger*>(png_get_error_ptr(png));
  Log<UFG_ERROR_PNG_DECODE>(logger, "", message);
  png_longjmp(png, 1);
}

static void EncodeErrorCallback(png_struct* png, const char* message) {
  Logger* const logger = static_cast<Logger*>(png_get_error_ptr(png));
  Log<UFG_ERROR_PNG_ENCODE>(logger, "", message);
  png_longjmp(png, 1);
}

static void DecodeWarnCallback(png_struct* png, const char* message) {
  // This warning occurs a lot and is innocuous, so ignore it to reduce spam.
  if (strcmp(message, "iCCP: known incorrect sRGB profile") == 0) {
    return;
  }
  Logger* const logger = static_cast<Logger*>(png_get_error_ptr(png));
  Log<UFG_WARN_PNG_DECODE>(logger, "", message);
}

static void EncodeWarnCallback(png_struct* png, const char* message) {
  Logger* const logger = static_cast<Logger*>(png_get_error_ptr(png));
  Log<UFG_WARN_PNG_ENCODE>(logger, "", message);
}

int PngChannelCountToColorType(uint8_t channel_count) {
  switch (channel_count) {
  case 1:
    return PNG_COLOR_TYPE_GRAY;
  case 3:
    return PNG_COLOR_TYPE_RGB;
  case 4:
    return PNG_COLOR_TYPE_RGB_ALPHA;
  default:
    UFG_ASSERT_LOGIC(false);
    return PNG_COLOR_TYPE_GRAY;
  }
}

class PngReader {
 public:
  PngReader() : logger_(nullptr), png_(nullptr), info_(nullptr) {}
  ~PngReader() { Reset(); }

  bool Read(
      const void* src, size_t src_size,
      uint32_t* out_width, uint32_t* out_height, uint8_t* out_channel_count,
      std::vector<Image::Component>* out_buffer, Logger* logger) {
    Reset();
    logger_ = logger;

    // Create the decoder structures. The class destructor ensures these are
    // freed on error.
    png_ = png_create_read_struct(
        PNG_LIBPNG_VER_STRING, logger, DecodeErrorCallback, DecodeWarnCallback);
    info_ = png_ ? png_create_info_struct(png_) : nullptr;
    if (!png_ || !info_) {
      Log<UFG_ERROR_PNG_READ_INIT>(logger, "");
      return false;
    }
    if (setjmp(png_jmpbuf(png_))) {
      return false;
    }

    // Use callback to read from memory.
    read_pos_ = static_cast<const uint8_t*>(src);
    read_end_ = read_pos_ + src_size;
    png_set_read_fn(png_, this, ReadCallback);

    // Force output to 8-bit RGB/RGBA.
    png_set_expand(png_);
    png_set_scale_16(png_);
    png_set_gray_to_rgb(png_);

    // Read file info.
    png_read_info(png_, info_);
    png_read_update_info(png_, info_);
    const int width = png_get_image_width(png_, info_);
    const int height = png_get_image_height(png_, info_);
    const int bit_depth = png_get_bit_depth(png_, info_);
    const uint8_t channel_count = png_get_channels(png_, info_);
    UFG_ASSERT_LOGIC(bit_depth <= kPngBitDepth);
    UFG_ASSERT_LOGIC(channel_count == 3 || channel_count == 4);

    // Decode image pixels.
    const size_t row_stride = width * channel_count;
    const size_t src_row_stride = png_get_rowbytes(png_, info_);
    UFG_ASSERT_LOGIC(src_row_stride == row_stride);
    const size_t component_total = height * row_stride;
    std::vector<Image::Component> buffer(component_total);
    std::vector<png_bytep> rows(height);
    for (size_t y = 0; y != height; ++y) {
      rows[y] = &buffer[row_stride * y];
    }
    png_read_image(png_, rows.data());

    *out_width = width;
    *out_height = height;
    *out_channel_count = channel_count;
    out_buffer->swap(buffer);
    return true;
  }

 private:
  Logger* logger_;
  png_struct* png_;
  png_info* info_;
  const uint8_t* read_pos_;
  const uint8_t* read_end_;

  static void ReadCallback(
      png_struct* png, png_byte* out_bytes, png_size_t byte_count) {
    PngReader* const decoder = static_cast<PngReader*>(png_get_io_ptr(png));
    const size_t remain = decoder->read_end_ - decoder->read_pos_;
    if (byte_count > remain) {
      Log<UFG_ERROR_PNG_READ_SHORT>(decoder->logger_, "", byte_count, remain);
      png_longjmp(png, 1);
    }
    memcpy(out_bytes, decoder->read_pos_, byte_count);
    decoder->read_pos_ += byte_count;
  }

  void Reset() {
    if (png_) {
      png_destroy_read_struct(&png_, &info_, nullptr);
      png_ = nullptr;
      info_ = nullptr;
    }
  }
};

class PngWriter {
 public:
  PngWriter() : fp_(nullptr), png_(nullptr), info_(nullptr) {}
  ~PngWriter() { Reset(); }

  bool Write(
      const char* path, uint32_t width, uint32_t height, uint8_t channel_count,
      const Image::Component* data, int level, Logger* logger) {
    level = Clamp(level, 0, 9);

    Reset();

    fp_ = fopen(path, "wb");
    if (!fp_) {
      Log<UFG_ERROR_IO_WRITE_IMAGE>(logger, "", path);
      return false;
    }

    png_ = png_create_write_struct(
        PNG_LIBPNG_VER_STRING, logger, EncodeErrorCallback, EncodeWarnCallback);
    info_ = png_ ? png_create_info_struct(png_) : nullptr;
    if (!png_ || !info_) {
      Log<UFG_ERROR_PNG_WRITE_INIT>(logger, "");
      return false;
    }

    if (setjmp(png_jmpbuf(png_))) {
      return false;
    }
    png_init_io(png_, fp_);

    // Write header.
    const int color_type = PngChannelCountToColorType(channel_count);
    png_set_IHDR(png_, info_, width, height, kPngBitDepth, color_type,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_set_compression_level(png_, level);
    png_write_info(png_, info_);

    // Write image data, first awkwardly converting to array of arrays.
    const size_t row_stride = width * channel_count;
    std::vector<png_bytep> rows(height);
    for (size_t y = 0; y != height; ++y) {
      rows[y] = const_cast<png_bytep>(&data[row_stride * y]);
    }
    png_write_image(png_, const_cast<png_bytep*>(rows.data()));

    png_write_end(png_, nullptr);
    return true;
  }

 private:
  FILE* fp_;
  png_struct* png_;
  png_info* info_;

  void Reset() {
    if (fp_) {
      fclose(fp_);
      fp_ = nullptr;
    }
    if (png_) {
      png_destroy_write_struct(&png_, &info_);
      png_ = nullptr;
      info_ = nullptr;
    }
  }
};
}  // namespace

bool HasPngHeader(const void* src, size_t src_size) {
  constexpr size_t kHeaderSize = 8;
  return src_size >= kHeaderSize &&
         png_check_sig(static_cast<const png_byte*>(src), kHeaderSize);
}

bool PngRead(
    const void* src, size_t src_size,
    uint32_t* out_width, uint32_t* out_height, uint8_t* out_channel_count,
    std::vector<Image::Component>* out_buffer, Logger* logger) {
  PngReader reader;
  return reader.Read(
      src, src_size, out_width, out_height, out_channel_count, out_buffer,
      logger);
}

bool PngWrite(
    const char* path, uint32_t width, uint32_t height, uint8_t channel_count,
    const Image::Component* data, int level, Logger* logger) {
  PngWriter writer;
  return writer.Write(path, width, height, channel_count, data, level, logger);
}
}  // namespace ufg
