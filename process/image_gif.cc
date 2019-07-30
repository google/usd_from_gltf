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

#include "gif_lib.h"  // NOLINT: Silence relative path warning.

namespace ufg {
namespace {
struct GifSentry {
  GifFileType *gif;
  explicit GifSentry(GifFileType *gif) : gif(gif) {}
  ~GifSentry() {
    if (gif) {
      int error_code;
      DGifCloseFile(gif, &error_code);
    }
  }
};

struct GifReadContext {
  const uint8_t* pos;
  const uint8_t* end;
};

int GifReadCallback(GifFileType* gif, GifByteType* out_data, int request_size) {
  GifReadContext* const context = static_cast<GifReadContext*>(gif->UserData);
  const int remain_size = static_cast<int>(context->end - context->pos);
  const int read_size = std::min(remain_size, request_size);
  memcpy(out_data, context->pos, read_size);
  context->pos += read_size;
  return read_size;
}

bool GifSeekToFirstImage(
    GifFileType* gif, int* out_transparent_index, Logger* logger) {
  *out_transparent_index = -1;
  for (;;) {
    GifRecordType type;
    if (DGifGetRecordType(gif, &type) == GIF_ERROR) {
      Log<UFG_ERROR_GIF_RECORD_TYPE>(logger, "", gif->Error);
      return false;
    }
    switch (type) {
    case IMAGE_DESC_RECORD_TYPE:
      if (DGifGetImageDesc(gif) == GIF_ERROR) {
        Log<UFG_ERROR_GIF_IMAGE_DESC>(logger, "", gif->Error);
        return false;
      }
      return true;
    case EXTENSION_RECORD_TYPE: {
      // Get graphics control block (GCB) extension, skip the rest.
      int extension_code;
      GifByteType* extension = nullptr;
      int result = DGifGetExtension(gif, &extension_code, &extension);
      while (extension) {
        if (result == GIF_ERROR) {
          Log<UFG_ERROR_GIF_EXTENSION>(logger, "", gif->Error);
          return false;
        }
        if (extension_code == GRAPHICS_EXT_FUNC_CODE) {
          const uint32_t len = extension[0];
          const GifByteType* const data = extension + 1;
          GraphicsControlBlock gcb;
          if (DGifExtensionToGCB(len, data, &gcb) == GIF_ERROR) {
            Log<UFG_ERROR_GIF_BAD_GCB>(logger, "");
            return false;
          }
          *out_transparent_index = gcb.TransparentColor;
        }
        result = DGifGetExtensionNext(gif, &extension);
      }
      break;
    }
    case TERMINATE_RECORD_TYPE:
    default:
      Log<UFG_ERROR_GIF_NO_RECORDS>(logger, "");
      return false;
    }
  }
}

void GifGetBackgroundColor(GifFileType* gif, int transparent_index,
                           Image::Component (&out_color)[kColorChannelCount]) {
  out_color[kColorChannelR] = 0;
  out_color[kColorChannelG] = 0;
  out_color[kColorChannelB] = 0;
  out_color[kColorChannelA] = Image::kComponentMax;

  const ColorMapObject* const palette = gif->SColorMap;
  if (transparent_index != NO_TRANSPARENT_COLOR &&
      gif->SBackGroundColor == transparent_index) {
    out_color[kColorChannelA] = 0;
  } else if (palette && palette->Colors &&
             gif->SBackGroundColor < palette->ColorCount) {
    const GifColorType& color = palette->Colors[gif->SBackGroundColor];
    out_color[kColorChannelR] = color.Red;
    out_color[kColorChannelG] = color.Green;
    out_color[kColorChannelB] = color.Blue;
  } else {
    // Unexpected failure. Fall back to black.
  }
}

bool GifReadLine(GifFileType* gif, uint32_t width, uint32_t transparent_index,
                 const GifColorType* palette_colors,
                 uint32_t palette_color_count, GifPixelType* row_buffer,
                 Image::Component* out_pixels) {
  if (DGifGetLine(gif, row_buffer, width) == GIF_ERROR) {
    return false;
  }
  for (size_t i = 0; i != width; ++i) {
    Image::Component* const pixel = out_pixels + i * kColorChannelCount;
    const uint32_t color_index = row_buffer[i];
    if (color_index == transparent_index) {
      pixel[kColorChannelR] = 0;
      pixel[kColorChannelG] = 0;
      pixel[kColorChannelB] = 0;
      pixel[kColorChannelA] = 0;
    } else if (color_index < palette_color_count) {
      const GifColorType& color = palette_colors[color_index];
      pixel[kColorChannelR] = color.Red;
      pixel[kColorChannelG] = color.Green;
      pixel[kColorChannelB] = color.Blue;
      pixel[kColorChannelA] = Image::kComponentMax;
    }
  }
  return true;
}
}  // namespace

bool HasGifHeader(const void* src, size_t src_size) {
  // Expect "GIF87a" or "GIF89a".
  // See: https://en.wikipedia.org/wiki/GIF
  if (src_size < 6) {
    return false;
  }
  const uint8_t* const b = static_cast<const uint8_t*>(src);
  return b[0] == 'G' && b[1] == 'I' && b[2] == 'F' &&
    b[3] == '8' && (b[4] == '7' || b[4] == '9') && b[5] == 'a';
}

bool GifRead(
    const void* src, size_t src_size,
    uint32_t* out_width, uint32_t* out_height, uint8_t* out_channel_count,
    std::vector<Image::Component>* out_buffer, Logger* logger) {
  const uint8_t* const begin = static_cast<const uint8_t*>(src);
  GifReadContext context = { begin, begin + src_size };
  int error_code = 0;
  GifFileType *const gif = DGifOpen(&context, GifReadCallback, &error_code);
  if (!gif) {
    Log<UFG_ERROR_GIF_OPEN>(logger, "", error_code);
    return false;
  }
  GifSentry gif_sentry(gif);

  if (gif->SWidth <= 0 || gif->SHeight <= 0) {
    Log<UFG_ERROR_GIF_BAD_SIZE>(logger, "", gif->SWidth, gif->SHeight);
    return false;
  }
  const uint32_t width = gif->SWidth;
  const uint32_t height = gif->SHeight;

  // Decode the first frame of the GIF and verify it's in bounds.
  int transparent_index;
  if (!GifSeekToFirstImage(gif, &transparent_index, logger)) {
    return false;
  }
  const uint32_t dx = gif->Image.Width;
  const uint32_t dy = gif->Image.Height;
  const uint32_t x0 = gif->Image.Left;
  const uint32_t y0 = gif->Image.Top;
  const uint32_t x1 = x0 + dx;
  const uint32_t y1 = y0 + dy;
  if (x1 > width || y1 > height) {
    Log<UFG_ERROR_GIF_FRAME_BOUNDS>(logger, "", x0, y0, x1, y1, width, height);
    return false;
  }

  // Allocate image pixels.
  const size_t pixel_count = width * height;
  const size_t size = pixel_count * kColorChannelCount;
  out_buffer->resize(size);
  Image::Component* const dst_pixels = out_buffer->data();

  // Initialize the image to the background color.
  Image::Component background_color[kColorChannelCount];
  GifGetBackgroundColor(gif, transparent_index, background_color);
  for (size_t i = 0; i != pixel_count; ++i) {
    Image::Component* const dst_pixel = dst_pixels + i * kColorChannelCount;
    dst_pixel[kColorChannelR] = background_color[kColorChannelR];
    dst_pixel[kColorChannelG] = background_color[kColorChannelG];
    dst_pixel[kColorChannelB] = background_color[kColorChannelB];
    dst_pixel[kColorChannelA] = background_color[kColorChannelA];
  }

  // Copy image lines.
  const GifColorType* const palette_colors =
      gif->SColorMap ? gif->SColorMap->Colors : nullptr;
  const uint32_t palette_color_count =
      gif->SColorMap ? gif->SColorMap->ColorCount : 0;
  const uint32_t row_stride = width * kColorChannelCount;
  std::vector<GifPixelType> row_buffer(dx);
  if (gif->Image.Interlace) {
    // Interlacing just modifies row order.
    static const uint8_t kInterlacedOffsets[] = { 0, 4, 2, 1 };
    static const uint8_t kInterlacedJumps[] = { 8, 8, 4, 2 };
    Image::Component* const base_pixels = dst_pixels + x0 * kColorChannelCount;
    for (size_t pass = 0; pass != 4; ++pass) {
      const uint32_t offset = kInterlacedOffsets[pass];
      const uint32_t jump = kInterlacedJumps[pass];
      for (uint32_t y = y0 + offset; y < y1; y += jump) {
        Image::Component* const row_pixels = base_pixels + y * row_stride;
        GifReadLine(gif, dx, transparent_index, palette_colors,
                    palette_color_count, row_buffer.data(), row_pixels);
      }
    }
  } else {
    Image::Component* row_pixels =
        dst_pixels + y0 * row_stride + x0 * kColorChannelCount;
    for (uint32_t i = 0; i != dy; ++i) {
      GifReadLine(gif, dx, transparent_index, palette_colors,
                  palette_color_count, row_buffer.data(), row_pixels);
      row_pixels += row_stride;
    }
  }

  *out_width = width;
  *out_height = height;
  *out_channel_count = kColorChannelCount;
  return true;
}
}  // namespace ufg
