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

#include "image_parsing.h"  // NOLINT: Silence relative path warning.

#include "internal_util.h"  // NOLINT: Silence relative path warning.

namespace {
const size_t kHeaderSizeMax = 128;

#ifdef _WIN32
constexpr bool kLittleEndian = true;
#else  // _WIN32
constexpr bool kLittleEndian = __ORDER_LITTLE_ENDIAN__;
#endif  // _WIN32

constexpr uint16_t EndianSwap(uint16_t v) {
  return (v << 8) | (v >> 8);
}

constexpr uint32_t EndianSwap(uint32_t v) {
  return (v << 24) | ((v << 8) & 0xff0000u) | ((v >> 8) & 0xff00u) | (v >> 24);
}

template <typename T>
constexpr T FromBigEndian(T v) {
  return kLittleEndian ? EndianSwap(v) : v;
}

template <typename T>
bool NextCopy(const void** it, const void* end, T* out) {
  const T* const p = static_cast<const T*>(*it);
  const void* const next = p + 1;
  if (next > end) {
    return false;
  }
  *out = *p;
  *it = next;
  return true;
}

bool NextSkip(const void** it, const void* end, size_t skip_len) {
  const uint8_t* const p = static_cast<const uint8_t*>(*it);
  const void* const next = p + skip_len;
  if (next > end) {
    return false;
  }
  *it = next;
  return true;
}

// ---- JPG ----
// The OS X compilation failed with this as a static constexpr in JpgParser.
constexpr size_t kRefillBlockSize = 4 * 1024;

class JpgParser {
  // JPG marker codes.
  // See: https://en.wikipedia.org/wiki/JPEG
  static constexpr int kMarkerNone   = -1;    // No marker.
  static constexpr int kMarkerPad    = 0xff;  // Padding byte.
  static constexpr int kMarkerSOI    = 0xd8;  // Start Of Image.
  static constexpr int kMarkerSOFMin = 0xc0;  // Start Of Frame, minimum value.
  static constexpr int kMarkerSOFMax = 0xc2;  // Start Of Frame, maximum value.

  static constexpr int kSOFLenMin = 11;  // Minimum size of the SOF segment.

 public:
  static bool Match(const void* data, size_t data_size) {
    if (data_size < 2) {
      return false;
    }
    const uint8_t* const header = static_cast<const uint8_t*>(data);
    return header[0] == kMarkerPad && header[1] == kMarkerSOI;
  }

  explicit JpgParser(
      GltfLogger* logger, GltfParseReadFP read, void* user_context)
      : logger_(logger), read_(read), user_context_(user_context) {}

  bool Parse(const void* data, size_t data_size, const char* name,
             uint32_t* out_width, uint32_t* out_height) {
    data_.resize(data_size);
    memcpy(data_.data(), data, data_size);
    pos_ = data_.data();
    end_ = data_.data() + data_.size();
    eof_ = !read_;
    return ParseInternal(name, out_width, out_height);
  }

 private:
  GltfLogger* logger_;
  GltfParseReadFP read_;
  void* user_context_;
  std::vector<uint8_t> data_;
  const void *pos_;
  const void *end_;
  bool eof_;

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) const {
    logger_->Add(GltfGetMessage<kWhat>("", args...));
  }

  void Refill(size_t reserve) {
    if (eof_) {
      return;
    }
    const size_t offset = static_cast<const uint8_t*>(pos_) - data_.data();
    const size_t remain = data_.size() - offset;
    if (remain >= reserve) {
      return;
    }
    size_t refill_size = std::max(reserve - remain, kRefillBlockSize);
    std::vector<uint8_t> refill_data;
    if (!read_(user_context_, data_.size(), refill_size, &refill_data)) {
      eof_ = true;
    }
    data_.insert(data_.end(), refill_data.begin(), refill_data.end());
    pos_ = data_.data() + offset;
    end_ = data_.data() + data_.size();
  }

  bool Eof() {
    Refill(1);
    return pos_ >= end_;
  }

  int NextU8(int default_value) {
    Refill(sizeof(uint8_t));
    uint8_t value;
    return NextCopy(&pos_, end_, &value) ? static_cast<int>(value)
                                         : default_value;
  }

  int NextU16(int default_value) {
    Refill(sizeof(uint16_t));
    uint16_t value;
    return NextCopy(&pos_, end_, &value)
               ? static_cast<int>(FromBigEndian(value))
               : default_value;
  }

  int NextMarker() {
    // One or more bytes of padding (0xff) followed by the marker code.
    int marker = NextU8(kMarkerNone);
    if (marker != kMarkerPad) {
      return kMarkerNone;
    }
    // Skip padding.
    do {
      marker = NextU8(kMarkerNone);
    } while (marker == kMarkerPad);
    return marker;
  }

  bool SkipToSOF(const char* name) {
    for (;;) {
      const int marker = NextMarker();
      if (marker >= kMarkerSOFMin && marker <= kMarkerSOFMax) {
        return true;
      }

      // Ignore padding.
      if (marker == kMarkerNone) {
        if (Eof()) {
          return false;
        }
        continue;
      }

      // Skip this segment.
      const int size = NextU16(-1);
      if (size < sizeof(uint16_t)) {
        return false;
      }
      const size_t skip = size - sizeof(uint16_t);
      Refill(skip);
      if (!NextSkip(&pos_, end_, skip)) {
        return false;
      }
    }
    return true;
  }

  bool ParseInternal(
      const char* name, uint32_t* out_width, uint32_t* out_height) {
    // Check SOI (Start Of Image) header.
    int marker = NextMarker();
    if (marker != kMarkerSOI) {
      Log<GLTF_ERROR_JPG_SOI_MISSING>(name);
      return false;
    }

    // Skip segments until we find SOF (Start Of Frame).
    if (!SkipToSOF(name)) {
      Log<GLTF_ERROR_JPG_SOF_MISSING>(name);
      return false;
    }

    // Read SOF fields.
    const int sof_len = NextU16(-1);
    const int bit_count = NextU8(-1);
    const int height = NextU16(-1);
    const int width = NextU16(-1);
    const int component_count = NextU8(-1);
    if (Eof()) {
      Log<GLTF_ERROR_JPG_TRUNCATED>(PointerDistance(data_.data(), end_), name);
      return false;
    }
    if (sof_len < kSOFLenMin) {
      Log<GLTF_ERROR_JPG_SOF_SHORT>(sof_len, kSOFLenMin, name);
      return false;
    }
    if (bit_count != 8) {
      Log<GLTF_ERROR_JPG_BAD_BIT_COUNT>(bit_count, name);
      return false;
    }
    if (width <= 0 || height <= 0) {
      Log<GLTF_ERROR_IMAGE_ZERO_SIZE>(width, height, name);
      return false;
    }
    if (component_count != 1 && component_count != 3 && component_count != 4) {
      Log<GLTF_ERROR_JPG_BAD_COMPONENT_COUNT>(component_count, name);
      return false;
    }

    *out_width = width;
    *out_height = height;
    return true;
  }
};

// ---- PNG ----

constexpr uint8_t kPngHeader[] = {137, 'P', 'N', 'G', 13, 10, 26, 10};

class PngParser {
 private:
  struct Header {
    uint8_t magic[sizeof(kPngHeader)];
  };

  struct Chunk {
    uint32_t len;
    uint32_t type;

    void Decode() {
      len = FromBigEndian(len);
      type = FromBigEndian(type);
    }
  };

  struct Ihdr {
    static constexpr size_t kLen = 13;
    static constexpr uint32_t kDimMax = 1 << 24;

    uint32_t width;
    uint32_t height;
    uint8_t depth;
    uint8_t color;
    uint8_t comp;
    uint8_t filter;
    // There's 13 bytes in the IHDR chunk making it unaligned. This complicates
    // iteration, so just ignore it since it's not critical to validation.
    // uint8_t interlace;

    void Decode() {
      width = FromBigEndian(width);
      height = FromBigEndian(height);
    }
  };

 public:
  static bool Match(const void* data, size_t data_size) {
    return data_size >= sizeof(kPngHeader) &&
           memcmp(data, kPngHeader, sizeof(kPngHeader)) == 0;
  }

  explicit PngParser(GltfLogger* logger) : logger_(logger) {}

  bool Parse(const void* data, size_t data_size, const char* name,
             uint32_t* out_width, uint32_t* out_height) const {
    const void* it = data;
    const void* const end = PointerOffset(data, data_size);

    // Skip the PNG header.
    Header png_header;
    if (!NextCopy(&it, end, &png_header)) {
      // The caller already checked the header, so this case shouldn't happen.
      return false;
    }

    // Decode first chunk header, which must be IHDR.
    Chunk chunk;
    if (!NextCopy(&it, end, &chunk)) {
      Log<GLTF_ERROR_PNG_IHDR_MISSING>(name);
      return false;
    }
    chunk.Decode();
    if (chunk.type != 'IHDR') {
      Log<GLTF_ERROR_PNG_IHDR_MISSING>(name);
      return false;
    }
    if (chunk.len != Ihdr::kLen) {
      Log<GLTF_ERROR_PNG_IHDR_SIZE>(chunk.len, Ihdr::kLen, name);
      return false;
    }

    // Decode IHDR.
    Ihdr ihdr;
    if (!NextCopy(&it, end, &ihdr)) {
      const size_t size_min = PointerDistance(data, it) + Ihdr::kLen;
      Log<GLTF_ERROR_FILE_TRUNCATED>(data_size, size_min, name);
      return false;
    }
    ihdr.Decode();

    if (ihdr.width == 0 || ihdr.height == 0) {
      Log<GLTF_ERROR_IMAGE_ZERO_SIZE>(ihdr.width, ihdr.height, name);
      return false;
    }
    if (ihdr.width > Ihdr::kDimMax || ihdr.height > Ihdr::kDimMax) {
      Log<GLTF_ERROR_PNG_TOO_LARGE>(
          ihdr.width, ihdr.height, Ihdr::kDimMax, name);
      return false;
    }
    if (ihdr.depth != 1 && ihdr.depth != 2 && ihdr.depth != 4 &&
        ihdr.depth != 8 && ihdr.depth != 16) {
      Log<GLTF_ERROR_PNG_BAD_DEPTH>(ihdr.depth, name);
      return false;
    }

    // Color type codes represent sums of the following values: 1 (palette
    // used), 2 (color used), and 4 (alpha channel used). Valid values are
    // 0, 2, 3, 4, and 6.
    // See: http://www.libpng.org/pub/png/spec/1.2/PNG-Chunks.html
    if (ihdr.color > 6 || (ihdr.color == 3 && ihdr.depth == 16) ||
        (ihdr.color != 3 && (ihdr.color & 1))) {
      Log<GLTF_ERROR_PNG_BAD_COLOR>(ihdr.color, name);
      return false;
    }

    if (ihdr.comp != 0) {
      Log<GLTF_ERROR_PNG_BAD_COMP>(ihdr.comp, name);
      return false;
    }
    if (ihdr.filter != 0) {
      Log<GLTF_ERROR_PNG_BAD_FILTER>(ihdr.filter, name);
      return false;
    }

    *out_width = ihdr.width;
    *out_height = ihdr.height;
    return true;
  }

 private:
  GltfLogger* logger_;

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) const {
    logger_->Add(GltfGetMessage<kWhat>("", args...));
  }
};

// ---- BMP ----

// BMP has a 2-byte "BM" header, and a DIB struct size at offset 14 that
// indicates the version.
// See: https://en.wikipedia.org/wiki/BMP_file_format
constexpr uint8_t kBmpHeader[] = {'B', 'M'};
constexpr uint8_t kBmpDibSizes[] = {
  12,   // BITMAPCOREHEADER
  40,   // BITMAPINFOHEADER
  56,   // BITMAPV3INFOHEADER
  108,  // BITMAPV4HEADER
  124,  // BITMAPV5HEADER
};

class BmpParser {
 private:
  static_assert(kLittleEndian, "Only implemented for little-endian.");
  static constexpr uint32_t kCommonHeaderSize = 14;
  static constexpr uint32_t kDibSize16 = 12;

  struct Header {
    uint8_t magic[sizeof(kBmpHeader)];
  };

  struct Info {
    uint32_t file_size;
    uint16_t reserved0;
    uint16_t reserved1;
    uint32_t pixel_offset;
    uint32_t dib_size;
    union {
      struct { uint16_t width, height; } dim16;  // (dib_size == kDibSize16)
      struct { uint32_t width, height; } dim32;  // (dib_size != kDibSize16)
    };
  };

 public:
  static bool Match(const void* data, size_t data_size) {
    if (data_size < sizeof(Header) + sizeof(Info)) {
      return false;
    }
    const Header* const header = static_cast<const Header*>(data);
    if (memcmp(header->magic, kBmpHeader, sizeof(kBmpHeader)) != 0) {
      return false;
    }
    const Info* const info = reinterpret_cast<const Info*>(header + 1);
    for (const uint8_t dib_size : kBmpDibSizes) {
      if (info->dib_size == dib_size) {
        return true;
      }
    }
    return false;
  }

  explicit BmpParser(GltfLogger* logger) : logger_(logger) {}

  bool Parse(const void* data, size_t data_size, const char* name,
             uint32_t* out_width, uint32_t* out_height) const {
    const Header* const header = static_cast<const Header*>(data);
    const Info* const info = reinterpret_cast<const Info*>(header + 1);

    const size_t pixel_offset_min = kCommonHeaderSize + info->dib_size;
    if (info->file_size < pixel_offset_min ||
        info->pixel_offset < pixel_offset_min) {
      Log<GLTF_ERROR_BMP_BAD_HEADER_SIZES>(
          info->file_size, info->pixel_offset, pixel_offset_min, name);
      return false;
    }

    uint32_t width, height;
    if (info->dib_size == kDibSize16) {
      width = info->dim16.width;
      height = info->dim16.height;
    } else {
      width = info->dim32.width;
      height = info->dim32.height;
    }
    if (width == 0 || height == 0) {
      Log<GLTF_ERROR_IMAGE_ZERO_SIZE>(width, height, name);
      return false;
    }

    *out_width = width;
    *out_height = height;
    return true;
  }

 private:
  GltfLogger* logger_;

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) const {
    logger_->Add(GltfGetMessage<kWhat>("", args...));
  }
};

// ---- GIF ----

// GIF has a 6-byte "GIF87a" or "GIF89a" header.
// See: https://en.wikipedia.org/wiki/GIF
constexpr uint8_t kGifHeader87[] = {'G', 'I', 'F', '8' , '7' , 'a'};
constexpr uint8_t kGifHeader89[] = {'G', 'I', 'F', '8' , '9' , 'a'};

class GifParser {
 private:
  static_assert(kLittleEndian, "Only implemented for little-endian.");
  struct Info {
    uint16_t width;
    uint16_t height;
  };

 public:
  static bool Match(const void* data, size_t data_size) {
    if (data_size < sizeof(kGifHeader87) + sizeof(Info)) {
      return false;
    }
    return memcmp(data, kGifHeader87, sizeof(kGifHeader87)) == 0 ||
           memcmp(data, kGifHeader89, sizeof(kGifHeader89)) == 0;
  }

  explicit GifParser(GltfLogger* logger) : logger_(logger) {}

  bool Parse(const void* data, size_t data_size, const char* name,
             uint32_t* out_width, uint32_t* out_height) const {
    const Info* const info = reinterpret_cast<const Info*>(
        PointerOffset(data, sizeof(kGifHeader87)));
    const uint32_t width = info->width;
    const uint32_t height = info->height;
    if (width == 0 || height == 0) {
      Log<GLTF_ERROR_IMAGE_ZERO_SIZE>(width, height, name);
      return false;
    }
    *out_width = width;
    *out_height = height;
    return true;
  }

 private:
  GltfLogger* logger_;

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) const {
    logger_->Add(GltfGetMessage<kWhat>("", args...));
  }
};

Gltf::Image::MimeType ClassifyImage(const void* data, size_t data_size) {
  if (JpgParser::Match(data, data_size)) {
    return Gltf::Image::kMimeJpeg;
  }
  if (PngParser::Match(data, data_size)) {
    return Gltf::Image::kMimePng;
  }
  if (BmpParser::Match(data, data_size)) {
    return Gltf::Image::kMimeBmp;
  }
  if (GifParser::Match(data, data_size)) {
    return Gltf::Image::kMimeGif;
  }
  return Gltf::Image::kMimeOther;
}

struct ReadFromFileContext {
  FILE* fp;
  size_t pos;
};

bool ReadFromFile(void* user_context, size_t start, size_t limit,
                  std::vector<uint8_t>* out_data) {
  ReadFromFileContext& ctx = *static_cast<ReadFromFileContext*>(user_context);
  if (limit == 0) {
    const size_t file_size = GetFileSize(ctx.fp);
    if (start > file_size) {
      return false;
    }
    limit = file_size - start;
  }
  out_data->resize(limit);
  if (start != ctx.pos) {
    if (!SeekAbsolute(ctx.fp, start)) {
      return false;
    }
    ctx.pos = start;
  }
  const size_t read_size = fread(out_data->data(), 1, limit, ctx.fp);
  ctx.pos += read_size;
  out_data->resize(read_size);
  return read_size > 0;
}
}  // namespace

Gltf::Image::MimeType GltfParseImage(
    const void* data, size_t data_size, const char* name, GltfLogger* logger,
    uint32_t* out_width, uint32_t* out_height) {
  const Gltf::Image::MimeType type = ClassifyImage(data, data_size);
  switch (type) {
    case Gltf::Image::kMimeJpeg: {
      JpgParser parser(logger, nullptr, nullptr);
      return parser.Parse(data, data_size, name, out_width, out_height)
                 ? Gltf::Image::kMimeJpeg
                 : Gltf::Image::kMimeUnset;
    }
    case Gltf::Image::kMimePng: {
      PngParser parser(logger);
      return parser.Parse(data, data_size, name, out_width, out_height)
                 ? Gltf::Image::kMimePng
                 : Gltf::Image::kMimeUnset;
    }
    case Gltf::Image::kMimeBmp: {
      BmpParser parser(logger);
      return parser.Parse(data, data_size, name, out_width, out_height)
                 ? Gltf::Image::kMimeBmp
                 : Gltf::Image::kMimeUnset;
    }
    case Gltf::Image::kMimeGif: {
      GifParser parser(logger);
      return parser.Parse(data, data_size, name, out_width, out_height)
                 ? Gltf::Image::kMimeGif
                 : Gltf::Image::kMimeUnset;
    }
    case Gltf::Image::kMimeUnset:
    case Gltf::Image::kMimeOther:
    case Gltf::Image::kMimeCount:
      break;
  }
  return type;
}

Gltf::Image::MimeType GltfParseImage(
    FILE* fp, const char* name, GltfLogger* logger,
    uint32_t* out_width, uint32_t* out_height) {
  ReadFromFileContext ctx = {fp, 0};
  return GltfParseImage(
      ReadFromFile, &ctx, name, logger, out_width, out_height);
}

Gltf::Image::MimeType GltfParseImage(
    GltfParseReadFP read, void* user_context, const char* name,
    GltfLogger* logger, uint32_t* out_width, uint32_t* out_height) {
  // Read a small fixed-size header to determine image type.
  std::vector<uint8_t> data;
  if (!read(user_context, 0, kHeaderSizeMax, &data)) {
    return Gltf::Image::kMimeUnset;
  }
  const Gltf::Image::MimeType type = ClassifyImage(data.data(), data.size());
  if (type == Gltf::Image::kMimeJpeg) {
    // JPEG image size can be at an arbitrary location in the file, so we can't
    // use a fixed-size header.
    JpgParser parser(logger, read, user_context);
    return parser.Parse(data.data(), data.size(), name, out_width, out_height)
               ? Gltf::Image::kMimeJpeg
               : Gltf::Image::kMimeUnset;
  } else {
    // Other types can parse dimensions from the header.
    return GltfParseImage(
        data.data(), data.size(), name, logger, out_width, out_height);
  }
}
