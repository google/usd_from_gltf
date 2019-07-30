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

#include "load.h"  // NOLINT: Silence relative path warning.

#include <stdarg.h>
#include "internal_util.h"  // NOLINT: Silence relative path warning.
#include "json.hpp"

namespace {
using Json = nlohmann::json;

using Id = Gltf::Id;
using Uri = Gltf::Uri;
using Asset = Gltf::Asset;
using Accessor = Gltf::Accessor;
using Animation = Gltf::Animation;
using Buffer = Gltf::Buffer;
using BufferView = Gltf::BufferView;
using Camera = Gltf::Camera;
using Image = Gltf::Image;
using Material = Gltf::Material;
using Mesh = Gltf::Mesh;
using Node = Gltf::Node;
using Sampler = Gltf::Sampler;
using Scene = Gltf::Scene;
using Skin = Gltf::Skin;
using Texture = Gltf::Texture;

ptrdiff_t FindString(size_t count, const char* const* values,
                     const std::string& key) {
  for (size_t i = 0; i != count; ++i) {
    const char* const value = values[i];
    if (value && key == value) {
      return i;
    }
  }
  return -1;
}

template <typename Value, typename Key>
ptrdiff_t FindInt(size_t count, const Value* values, Key key) {
  for (size_t i = 0; i != count; ++i) {
    if (key == values[i]) {
      return i;
    }
  }
  return -1;
}

std::string StringsToCsv(size_t count, const char* const* values) {
  std::string csv;
  for (size_t i = 0; i != count; ++i) {
    const char* const str = values[i];
    if (str) {
      if (!csv.empty()) {
        csv += ", ";
      }
      csv += str;
    }
  }
  return csv;
}

template <typename T>
std::string IntsToCsv(size_t count, const T* values, size_t null_index) {
  std::string csv;
  char buffer[16];
  for (size_t i = 0; i != count; ++i) {
    if (i == null_index) {
      continue;
    }
    if (!csv.empty()) {
      csv += ", ";
    }
    snprintf(buffer, sizeof(buffer), "%d", static_cast<int>(values[i]));
    csv += buffer;
  }
  return csv;
}

uint32_t DecimalToUnsigned(const char* text, size_t len) {
  uint32_t value = 0;
  for (size_t i = 0; i != len; ++i) {
    const uint32_t digit = text[i] - '0';
    if (digit > 9) {
      return ~0u;
    }
    value = value * 10 + digit;
  }
  return value;
}

const char* FindChar(const char* begin, const char* end, int key) {
  for (const char* s = begin; s != end; ++s) {
    if (*s == key) {
      return s;
    }
  }
  return end;
}

// Table mapping base-64 characters to values.
constexpr uint32_t kBase64Bits = 6;
constexpr char kBase64Chars[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static_assert(CONST_STRLEN(kBase64Chars) == 64, "");
struct Base64Table {
  static constexpr uint8_t kInvalidBit = 0x80;
  uint8_t values[256];
  Base64Table() {
    std::fill(std::begin(values), std::end(values), uint8_t(kInvalidBit));
    for (size_t i = 0; i != CONST_STRLEN(kBase64Chars); ++i) {
      const uint8_t c = kBase64Chars[i];
      values[c] = static_cast<uint8_t>(i);
    }
  }
};
const Base64Table kBase64Table;

bool DecodeBase64(const char* begin, const char* end,
                  std::vector<uint8_t>* out_data) {
  const size_t text_len = end - begin;
  const size_t size_max = (text_len * 3 + 3) / 4;  // 3/4, rounded up.
  std::vector<uint8_t> data(size_max);
  uint8_t* dst = data.data();

  const char* it = begin;

  // Convert each 4 characters to 3 bytes.
  const size_t aligned_text_len = text_len & ~3u;
  const char* const aligned_end = begin + aligned_text_len;
  for (; it != aligned_end; it += 4, dst += 3) {
    const uint8_t v0 = kBase64Table.values[static_cast<uint8_t>(it[0])];
    const uint8_t v1 = kBase64Table.values[static_cast<uint8_t>(it[1])];
    const uint8_t v2 = kBase64Table.values[static_cast<uint8_t>(it[2])];
    const uint8_t v3 = kBase64Table.values[static_cast<uint8_t>(it[3])];
    if ((v0 | v1 | v2 | v3) & Base64Table::kInvalidBit) {
      break;
    }
    const uint32_t bits = (v0 << 18) + (v1 << 12) + (v2 << 6) + v3;
    dst[0] = static_cast<uint8_t>((bits >> 16) & 0xff);
    dst[1] = static_cast<uint8_t>((bits >>  8) & 0xff);
    dst[2] = static_cast<uint8_t>((bits >>  0) & 0xff);
  }

  // Handle remaining unaligned characters.
  uint32_t bits = 0;
  int shift = -8;
  for (; it != end; ++it) {
    const uint8_t c = *it;

    // Skip trailing character padding '=', which exists to align encoded
    // characters to a multiple of 4. We stop at the first padding character to
    // ensure the decoded size matches the encoded size.
    //
    // For example, a 1-byte source buffer (A:data, BC:zero padding) will be
    // encoded to 4 characters:
    //   AAAAAA, AABBBB, BBBBCC, CCCCCC
    // The first 2 characters will contain data, and the second 2 will be the
    // '=' padding character (e.g. "xy==").
    //
    // In this case, this function loops 3 times then stops:
    //   1) Accumulate 6 bits, AAAAAA.
    //   2) Accumulate 6 bits, AABBBB. Add 1 byte with 4 bits remaining.
    //   3) Detect '=' padding, and stop. Total bytes added: 1.
    // See: https://en.wikipedia.org/wiki/Base64
    if (c == '=') {
      break;
    }

    const uint8_t value = kBase64Table.values[c];
    if (value == Base64Table::kInvalidBit) {
      return false;
    }
    bits = (bits << kBase64Bits) + value;
    shift += kBase64Bits;
    if (shift >= 0) {
      *dst++ = static_cast<uint8_t>((bits >> shift) & 0xff);
      shift -= 8;
    }
  }

  const size_t size = dst - data.data();
  data.resize(size);
  out_data->swap(data);
  return true;
}

bool IsDataUri(const std::string& text) {
  return Gltf::StringBeginsWithCI(text.c_str(), text.length(), "data:");
}

bool ParseDataUri(
    const std::string& text,
    std::vector<uint8_t>* out_data, Gltf::Uri::DataType* out_data_type) {
  // Parse URI of the form: "data:TYPE;ENCODING,CONTENT", where:
  // - TYPE:     The data type (one of the Uri::kDataType* values, with default
  //             kDataTypeUnknown).
  // - ENCODING: Data encoding - currently only "base64" is supported.
  // - CONTENT:  Base-64 encoded content.
  if (!IsDataUri(text)) {
    return false;
  }
  const char* const text_begin = text.c_str();
  const char* const text_end = text_begin + text.length();

  // Find type and encoding strings.
  const char* const type = text.c_str() + CONST_STRLEN("data:");
  const char* const encoding_end = FindChar(type, text_end, ',');
  if (encoding_end == text_end) {
    return false;
  }
  const char* const type_end = FindChar(type, encoding_end, ';');
  if (type_end == encoding_end) {
    return false;
  }
  const char* const encoding = type_end + 1;
  const char* const content = encoding_end + 1;
  const size_t type_len = type_end - type;
  const size_t encoding_len = encoding_end - encoding;
  if (type_len == 0 || encoding_len == 0) {
    return false;
  }

  // Parse encoding and data type.
  if (!Gltf::StringEqualCI(
          encoding, encoding_len, "base64", CONST_STRLEN("base64"))) {
    return false;
  }
  const Gltf::Uri::DataType data_type = Gltf::FindUriDataType(type, type_len);

  // Decode base64 content.
  if (!DecodeBase64(content, text_end, out_data)) {
    return false;
  }

  *out_data_type = data_type;
  return true;
}

bool GetSemantic(const char* key, size_t key_len,
                 Mesh::Attribute* out_attribute) {
  const char* const key_end = key + key_len;
  for (size_t semantic = 0; semantic != Mesh::kSemanticCount; ++semantic) {
    const Gltf::SemanticInfo& info = Gltf::kSemanticInfos[semantic];
    if (!Gltf::StringBeginsWithCI(key, key_len, info.prefix, info.prefix_len)) {
      continue;
    }
    const char* const suffix = key + info.prefix_len;
    uint8_t number = 0;
    if (info.has_numeric_suffix) {
      const uint32_t value = DecimalToUnsigned(suffix, key_end - suffix);
      if (value >= std::numeric_limits<uint8_t>::max()) {
        continue;
      }
      number = static_cast<uint8_t>(value);
    } else {
      if (suffix != key_end) {
        continue;
      }
    }
    out_attribute->semantic = static_cast<Mesh::Semantic>(semantic);
    out_attribute->number = number;
    return true;
  }
  return false;
}

bool IsWhitespace(int c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

const char* TrimLeadingWhitespace(const char* begin, const char* end) {
  for (; begin != end; ++begin) {
    if (!IsWhitespace(*begin)) {
      break;
    }
  }
  return begin;
}

const char* TrimTrailingWhitespace(const char* begin, const char* end) {
  for (; end != begin; --end) {
    if (!IsWhitespace(end[-1])) {
      break;
    }
  }
  return end;
}

std::string TrimWhitespace(const std::string& text) {
  const char* const text_begin = text.c_str();
  const char* const text_end = text_begin + text.length();
  const char* const trim_begin = TrimLeadingWhitespace(text_begin, text_end);
  const char* const trim_end = TrimTrailingWhitespace(trim_begin, text_end);
  return std::string(trim_begin, trim_end);
}

std::string GetValueBrief(const Json& json) {
  char buffer[1024];
  switch (json.type()) {
    case Json::value_t::object:
      return "{}";
    case Json::value_t::array:
      snprintf(buffer, sizeof(buffer), "[%zu]", json.size());
      return buffer;
    case Json::value_t::string:
      return TrimWhitespace(json.get<std::string>());
    case Json::value_t::boolean:
      return json.get<bool>() ? "true" : "false";
    case Json::value_t::number_integer:
      snprintf(buffer, sizeof(buffer), "%d", json.get<int>());
      return buffer;
    case Json::value_t::number_unsigned:
      snprintf(buffer, sizeof(buffer), "%u", json.get<unsigned int>());
      return buffer;
    case Json::value_t::number_float:
      snprintf(buffer, sizeof(buffer), "%f", json.get<float>());
      return buffer;
    case Json::value_t::null:
    case Json::value_t::discarded:
    default:
      return std::string();
  }
}

void PruneUnknownExtensions(std::vector<Gltf::ExtensionId>* extensions) {
  const Gltf::ExtensionId* src = extensions->data();
  const Gltf::ExtensionId* const src_end = src + extensions->size();
  Gltf::ExtensionId* dst = extensions->data();
  for (; src != src_end; ++src) {
    if (*src != Gltf::kExtensionUnknown) {
      *dst++ = *src;
    }
  }
  extensions->resize(dst - extensions->data());
}

class GltfLoader {
 public:
  void LoadGltf(const Json& json, const GltfLoadSettings& settings,
                Gltf* out, GltfLogger* logger) {
    settings_ = settings;
    logger_ = logger;

    LoadField(json, "asset", kGltfSeverityError, &out->asset);
    if (!out->asset.IsSupportedVersion()) {
      Log<GLTF_ERROR_BAD_VERSION>(out->asset.version.c_str());
      return;
    }
    LoadField(json, "scene", kGltfSeverityNone, &out->scene);
    LoadFieldArray(json, "extensionsUsed", kGltfSeverityNone,
                   &out->extensionsUsed, kGltfSeverityNone);
    PruneUnknownExtensions(&out->extensionsUsed);
    LoadFieldArray(json, "extensionsRequired", kGltfSeverityNone,
                   &out->extensionsRequired, kGltfSeverityError);
    PruneUnknownExtensions(&out->extensionsRequired);
    LoadFieldArray(json, "accessors", kGltfSeverityNone, &out->accessors);
    LoadFieldArray(json, "animations", kGltfSeverityNone, &out->animations);
    LoadFieldArray(json, "buffers", kGltfSeverityNone, &out->buffers);
    LoadFieldArray(json, "bufferViews", kGltfSeverityNone, &out->bufferViews);
    LoadFieldArray(json, "cameras", kGltfSeverityNone, &out->cameras);
    LoadFieldArray(json, "images", kGltfSeverityNone, &out->images);
    LoadFieldArray(json, "materials", kGltfSeverityNone, &out->materials);
    LoadFieldArray(json, "meshes", kGltfSeverityNone, &out->meshes);
    LoadFieldArray(json, "nodes", kGltfSeverityNone, &out->nodes);
    LoadFieldArray(json, "samplers", kGltfSeverityNone, &out->samplers);
    LoadFieldArray(json, "scenes", kGltfSeverityNone, &out->scenes);
    LoadFieldArray(json, "skins", kGltfSeverityNone, &out->skins);
    LoadFieldArray(json, "textures", kGltfSeverityNone, &out->textures);
  }

 private:
  GltfLoadSettings settings_;
  GltfLogger* logger_;
  GltfPathStack path_stack_;

  template <GltfWhat kWhat, typename ...Ts>
  void Log(Ts... args) {
    const std::string path = path_stack_.GetPath();
    GltfLog<kWhat>(logger_, path.c_str(), args...);
  }

  const Json* EnterField(const Json& parent_json, const char* name,
                         GltfSeverity severity_if_missing) {
    const auto found = parent_json.find(name);
    if (found == parent_json.end()) {
      if (severity_if_missing == kGltfSeverityWarning) {
        Log<GLTF_WARN_MISSING_FIELD>(name);
      } else if (severity_if_missing == kGltfSeverityError) {
        Log<GLTF_ERROR_MISSING_FIELD>(name);
      }
      return nullptr;
    }
    path_stack_.Enter(name);
    return &*found;
  }

  struct FieldSentry {
    GltfLoader* loader;
    const Json* json;
    FieldSentry(GltfLoader* loader, const Json& parent_json, const char* name,
                GltfSeverity severity_if_missing)
        : loader(loader),
          json(loader->EnterField(parent_json, name, severity_if_missing)) {}
    ~FieldSentry() {
      if (json) {
        loader->path_stack_.Exit();
      }
    }
  };

  struct ElementSentry {
    GltfLoader* loader;
    ElementSentry(GltfLoader* loader, uint32_t element_index) : loader(loader) {
      loader->path_stack_.Enter(element_index);
    }
    ~ElementSentry() { loader->path_stack_.Exit(); }
  };

#define GLTF_LOADER_FIELD_SENTRY(                     \
    out_json, parent_json, name, severity_if_missing) \
  const FieldSentry out_json##_sentry(                \
    this, parent_json, name, severity_if_missing);    \
  if (!out_json##_sentry.json) {                      \
    return false;                                     \
  }                                                   \
  const Json& out_json = *out_json##_sentry.json

#define GLTF_LOADER_ELEMENT_SENTRY(out_json, element_index) \
  const ElementSentry out_json##_sentry(this, element_index)

  template <typename T>
  static bool IsMissingValue(const T& value) {
    return false;
  }
  static bool IsMissingValue(Id id) {
    return id == Id::kNull;
  }

  template <typename T>
  bool LoadField(const Json& parent_json, const char* name,
                 GltfSeverity severity_if_missing, T* out_value) {
    {
      GLTF_LOADER_FIELD_SENTRY(value_json, parent_json, name,
                               severity_if_missing);
      Load(value_json, out_value);
    }
    if (severity_if_missing != kGltfSeverityNone &&
        IsMissingValue(*out_value)) {
      if (severity_if_missing == kGltfSeverityWarning) {
        Log<GLTF_WARN_MISSING_FIELD>(name);
      } else if (severity_if_missing == kGltfSeverityError) {
        Log<GLTF_ERROR_MISSING_FIELD>(name);
      }
    }
    return true;
  }

  template <typename Element, typename ...Ts>
  void LoadArray(const Json& elements_json,
                 std::vector<Element>* out_elements, Ts... args) {
    if (!elements_json.is_array()) {
      Log<GLTF_ERROR_EXPECTED_ARRAY>();
      return;
    }
    const size_t count = elements_json.size();
    if (count == 0) {
      Log<GLTF_ERROR_EMPTY_ARRAY>();
      return;
    }
    std::vector<Element> elements(count);
    uint32_t index = 0;
    for (const Json& element_json : elements_json) {
      {
        GLTF_LOADER_ELEMENT_SENTRY(element_json, index);
        Load(element_json, &elements[index], args...);
      }
      if (IsMissingValue(elements[index])) {
        Log<GLTF_ERROR_MISSING_ARRAY_ELEMENT>(index);
      }
      ++index;
    }
    out_elements->swap(elements);
  }

  template <typename Element>
  void LoadArray(const Json& elements_json, size_t element_count,
                 Element* out_elements) {
    if (!elements_json.is_array()) {
      Log<GLTF_ERROR_EXPECTED_ARRAY>();
      return;
    }
    const size_t count = elements_json.size();
    if (count != element_count) {
      Log<GLTF_ERROR_BAD_ARRAY_LENGTH>(count, element_count);
      return;
    }
    uint32_t index = 0;
    for (const Json& element_json : elements_json) {
      {
        GLTF_LOADER_ELEMENT_SENTRY(element_json, index);
        Load(element_json, &out_elements[index]);
      }
      if (IsMissingValue(out_elements[index])) {
        Log<GLTF_ERROR_MISSING_ARRAY_ELEMENT>(index);
      }
      ++index;
    }
  }

  template <typename Element, size_t kCount>
  void LoadArray(const Json& elements_json, Element (&out_elements)[kCount]) {
    LoadArray(elements_json, kCount, out_elements);
  }

  template <typename Element, typename ...Ts>
  bool LoadFieldArray(const Json& parent_json, const char* name,
                      GltfSeverity severity_if_missing,
                      std::vector<Element>* out_elements, Ts... args) {
    GLTF_LOADER_FIELD_SENTRY(elements_json, parent_json, name,
                             severity_if_missing);
    LoadArray(elements_json, out_elements, args...);
    return true;
  }

  template <typename Element>
  bool LoadFieldArray(const Json& parent_json, const char* name,
                      GltfSeverity severity_if_missing, size_t element_count,
                      Element* out_elements) {
    GLTF_LOADER_FIELD_SENTRY(elements_json, parent_json, name,
                             severity_if_missing);
    LoadArray(elements_json, element_count, out_elements);
    return true;
  }

  template <typename Element, size_t kCount>
  bool LoadFieldArray(const Json& parent_json, const char* name,
                      GltfSeverity severity_if_missing,
                      Element (&out_elements)[kCount]) {
    return LoadFieldArray(parent_json, name, severity_if_missing, kCount,
                          out_elements);
  }

  template <typename Enum>
  bool LoadEnumByString(
      const Json& json, GltfSeverity severity_if_unknown, Enum* out) {
    if (!json.is_string()) {
      Log<GLTF_ERROR_EXPECTED_ENUM_STRING>();
      return false;
    }
    const std::string& value = json.get<std::string>();
    if (value.empty()) {
      Log<GLTF_ERROR_EMPTY_ENUM>();
      return false;
    }
    size_t value_count;
    const char* const* const values = Gltf::GetEnumNames(Enum(), &value_count);
    const ptrdiff_t found = FindString(value_count, values, value);
    if (found < 0) {
      if (severity_if_unknown == kGltfSeverityWarning) {
        Log<GLTF_WARN_BAD_ENUM_STRING>(
            value.c_str(), StringsToCsv(value_count, values).c_str());
      } else if (severity_if_unknown == kGltfSeverityError) {
        Log<GLTF_ERROR_BAD_ENUM_STRING>(
            value.c_str(), StringsToCsv(value_count, values).c_str());
      }
      return false;
    }
    *out = static_cast<Enum>(found);
    return true;
  }

  template <typename Value, typename Enum>
  bool LoadEnumByInt(const Json& json, const Value* values, size_t value_count,
                     Enum null_enum, Enum* out) {
    if (!json.is_number_integer()) {
      Log<GLTF_ERROR_EXPECTED_ENUM_INT>();
      return false;
    }
    const Value value = static_cast<Value>(json.get<int32_t>());
    const ptrdiff_t found = FindInt(value_count, values, value);
    if (found < 0 || static_cast<Enum>(found) == null_enum) {
      const std::string expected = IntsToCsv(value_count, values, null_enum);
      Log<GLTF_ERROR_BAD_ENUM_INT>(static_cast<int>(value), expected.c_str());
      return false;
    }
    *out = static_cast<Enum>(found);
    return true;
  }

  void Load(const Json& json, std::string* out) {
    if (!json.is_string()) {
      Log<GLTF_ERROR_EXPECTED_STRING>();
      return;
    }
    *out = json.get<std::string>();
  }

  void Load(const Json& json, bool* out) {
    if (!json.is_boolean()) {
      Log<GLTF_ERROR_EXPECTED_BOOL>();
      return;
    }
    *out = json.get<bool>();
  }

  void Load(const Json& json, int32_t* out) {
    if (!json.is_number()) {
      Log<GLTF_ERROR_EXPECTED_INT>();
      return;
    }
    *out = json.get<int32_t>();
  }

  template <typename T>
  void LoadUnsigned(const Json& json, T* out) {
    const uint32_t max = std::numeric_limits<T>::max();

    // Unsigned values can technically fit in signed and float types, so
    // silently convert them if the result is not lossy.
    uint32_t value;
    switch (json.type()) {
    case Json::value_t::number_integer: {
      const int32_t signed_value = json.get<int32_t>();
      if (signed_value < 0) {
        Log<GLTF_ERROR_INT_OUT_OF_RANGE>(signed_value, max);
        return;
      }
      value = signed_value;
      break;
    }
    case Json::value_t::number_unsigned: {
      value = json.get<uint32_t>();
      break;
    }
    case Json::value_t::number_float: {
      const float float_value = json.get<float>();
      value = static_cast<uint32_t>(float_value);
      if (static_cast<float>(value) != float_value) {
        Log<GLTF_ERROR_EXPECTED_UINT_IS_FLOAT>(float_value);
        return;
      }
      break;
    }
    default:
      Log<GLTF_ERROR_EXPECTED_UINT>();
      return;
    }

    // Verify value fits in output type.
    if (value > max) {
      Log<GLTF_ERROR_UINT_OUT_OF_RANGE>(value, max);
      return;
    }
    *out = static_cast<T>(value);
  }

  void Load(const Json& json, uint8_t* out) {
    LoadUnsigned(json, out);
  }
  void Load(const Json& json, uint16_t* out) {
    LoadUnsigned(json, out);
  }
  void Load(const Json& json, uint32_t* out) {
    LoadUnsigned(json, out);
  }

  void Load(const Json& json, float* out) {
    if (!json.is_number()) {
      Log<GLTF_ERROR_EXPECTED_FLOAT>();
      return;
    }
    const float value = json.get<float>();
    if (std::isnan(value)) {
      Log<GLTF_ERROR_FLOAT_NAN>();
    }
    *out = value;
  }

  void Load(const Json& json, Id* out) {
    if (!json.is_number_integer()) {
      Log<GLTF_ERROR_EXPECTED_ID>();
      return;
    }
    const int32_t id = json.get<int32_t>();
    if (id >= static_cast<int32_t>(Id::kNull)) {
      Log<GLTF_ERROR_ID_OUT_OF_RANGE>(id, static_cast<int>(Id::kNull) - 1);
      return;
    }
    *out = id < 0 ? Id::kNull : static_cast<Id>(id);
  }

  void Load(const Json& json, Gltf::ExtensionId* out, GltfSeverity severity) {
    *out = Gltf::kExtensionUnknown;
    LoadEnumByString(json, severity, out);
  }

  void Load(const Json& json, Accessor::ComponentType* out) {
    LoadEnumByInt(json, Gltf::kAccessorComponentTypeValues,
                  arraysize(Gltf::kAccessorComponentTypeValues),
                  Gltf::Accessor::kComponentUnset, out);
  }

  void Load(const Json& json, Accessor::Type* out) {
    LoadEnumByString(json, kGltfSeverityError, out);
  }

  void Load(const Json& json, Accessor::ComponentType component_type,
            Accessor::Type type, Accessor::Value* out) {
    const size_t count = Gltf::GetComponentCount(type);
    switch (Gltf::GetComponentFormat(component_type)) {
      case Gltf::kComponentFormatSignedInt:
        LoadArray(json, count, out->i);
        break;
      case Gltf::kComponentFormatUnsignedInt:
        LoadArray(json, count, out->u);
        break;
      case Gltf::kComponentFormatFloat:
        LoadArray(json, count, out->f);
        break;
      default:
        break;
    }
  }

  bool LoadField(const Json& parent_json, const char* name,
                 GltfSeverity severity_if_missing,
                 Accessor::ComponentType component_type, Accessor::Type type,
                 Accessor::Value* out_value) {
    GLTF_LOADER_FIELD_SENTRY(value_json, parent_json, name,
                             severity_if_missing);
    Load(value_json, component_type, type, out_value);
    return true;
  }

  bool HaveExtension(const Json& json, Gltf::ExtensionId extension_id) const {
    const auto extensions_it = json.find("extensions");
    if (extensions_it == json.end()) {
      return false;
    }
    const Json& extensions_json = *extensions_it;
    const char* const extension_name = Gltf::GetEnumName(extension_id);
    const auto extension_it = extensions_json.find(extension_name);
    return extension_it != extensions_json.end();
  }

  template <typename T>
  bool LoadExtension(const Json& json, Gltf::ExtensionId extension_id, T* out) {
    const auto extensions_it = json.find("extensions");
    if (extensions_it == json.end()) {
      return false;
    }
    const Json& extensions_json = *extensions_it;
    const char* const extension_name = Gltf::GetEnumName(extension_id);
    const auto extension_it = extensions_json.find(extension_name);
    if (extension_it == extensions_json.end()) {
      return false;
    }
    const Json& extension_json = *extension_it;
    Load(extension_json, out);
    return true;
  }

  template <typename T>
  void LoadExtension(const Json& json, Gltf::ExtensionId extension_id,
                     Gltf::Optional<T>* out) {
    T value;
    if (LoadExtension(json, extension_id, &value)) {
      *out = std::move(value);
    }
  }

  void WarnUnusedExtensions(const Json& json, size_t used_count,
                            const Gltf::ExtensionId* used) {
    const auto set_found = json.find("extensions");
    if (set_found == json.end()) {
      return;
    }
    const Json& set_json = *set_found;
    if (set_json.is_object()) {
      size_t extension_count;
      const char* const* const extension_names =
          Gltf::GetEnumNames(Gltf::ExtensionId(), &extension_count);
      for (auto it = set_json.begin(), end = set_json.end(); it != end; ++it) {
        const std::string& key = it.key();
        const ptrdiff_t found_id =
            FindString(extension_count, extension_names, key);
        if (found_id < 0) {
          if (!Gltf::StringBeginsWithAny(key.c_str(), key.length(),
                                         settings_.nowarn_extension_prefixes)) {
            Log<GLTF_WARN_EXTENSION_UNSUPPORTED>(key.c_str());
          }
        } else {
          const ptrdiff_t found_used = FindInt(used_count, used, found_id);
          if (found_used < 0) {
            Log<GLTF_WARN_EXTENSION_UNUSED>(key.c_str());
          }
        }
      }
    } else {
      Log<GLTF_WARN_EXTENSIONS_FIELD_UNUSED>();
    }
  }

  void WarnUnusedExtras(const Json& json) {
    const auto set_found = json.find("extras");
    if (set_found == json.end()) {
      return;
    }
    const Json& set_json = *set_found;
    if (set_json.is_object()) {
      for (auto it = set_json.begin(), end = set_json.end(); it != end; ++it) {
        const std::string& key = it.key();
        const Json& value = it.value();
        const std::string brief = GetValueBrief(value);
        const std::string suffix = brief.empty() ? "" : ": " + brief;
        Log<GLTF_INFO_EXTRA_UNUSED>(key.c_str(), suffix.c_str());
      }
    }
  }

  void WarnUnusedExtensionsAndExtras(
      const Json& json, size_t extensions_used_count,
      const Gltf::ExtensionId* extensions_used) {
    WarnUnusedExtensions(json, extensions_used_count, extensions_used);
    WarnUnusedExtras(json);
  }

  void WarnUnusedExtensionsAndExtras(const Json& json) {
    WarnUnusedExtensionsAndExtras(json, 0, nullptr);
  }

  template <size_t kExtensionsUsedCount>
  void WarnUnusedExtensionsAndExtras(
      const Json& json,
      const Gltf::ExtensionId (&extensions_used)[kExtensionsUsedCount]) {
    WarnUnusedExtensionsAndExtras(json, kExtensionsUsedCount, extensions_used);
  }

  void Load(const Json& json, Uri* out) {
    if (!json.is_string()) {
      Log<GLTF_ERROR_EXPECTED_URI>();
      return;
    }
    const std::string& text = json.get<std::string>();
    if (IsDataUri(text)) {
      if (!ParseDataUri(text, &out->data, &out->data_type)) {
        Log<GLTF_ERROR_BAD_URI_DATA_FORMAT>();
      }
    } else {
      out->path = text;
    }
  }

  void Load(const Json& json, Asset* out) {
    LoadField(json, "copyright", kGltfSeverityNone, &out->copyright);
    LoadField(json, "generator", kGltfSeverityNone, &out->generator);
    LoadField(json, "version", kGltfSeverityError, &out->version);
    LoadField(json, "minVersion", kGltfSeverityNone, &out->minVersion);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Accessor::Sparse::Indices* out) {
    LoadField(json, "bufferView", kGltfSeverityError, &out->bufferView);
    LoadField(json, "componentType", kGltfSeverityError, &out->componentType);
    if (out->componentType != Accessor::kComponentUnsignedByte &&
        out->componentType != Accessor::kComponentUnsignedShort &&
        out->componentType != Accessor::kComponentUnsignedInt) {
      const uint32_t type_value =
          out->componentType < Accessor::kComponentCount
              ? Gltf::kAccessorComponentTypeValues[out->componentType]
              : out->componentType;
      Log<GLTF_ERROR_BAD_SPARSE_INDICES_FORMAT>(
          Gltf::GetEnumName(out->componentType), type_value);
    }
    LoadField(json, "byteOffset", kGltfSeverityNone, &out->byteOffset);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Accessor::Sparse::Values* out) {
    LoadField(json, "bufferView", kGltfSeverityError, &out->bufferView);
    LoadField(json, "byteOffset", kGltfSeverityNone, &out->byteOffset);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Accessor::Sparse* out) {
    LoadField(json, "count", kGltfSeverityError, &out->count);
    LoadField(json, "indices", kGltfSeverityError, &out->indices);
    LoadField(json, "values", kGltfSeverityError, &out->values);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Accessor* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "bufferView", kGltfSeverityNone, &out->bufferView);
    LoadField(json, "normalized", kGltfSeverityNone, &out->normalized);
    LoadField(json, "byteOffset", kGltfSeverityNone, &out->byteOffset);
    LoadField(json, "componentType", kGltfSeverityError, &out->componentType);
    LoadField(json, "type", kGltfSeverityError, &out->type);
    LoadField(json, "count", kGltfSeverityError, &out->count);
    LoadField(json, "min", kGltfSeverityNone, out->componentType, out->type,
              &out->min);
    LoadField(json, "max", kGltfSeverityNone, out->componentType, out->type,
              &out->max);
    LoadField(json, "sparse", kGltfSeverityNone, &out->sparse);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Animation::Channel::Target::Path* out) {
    LoadEnumByString(json, kGltfSeverityError, out);
  }

  void Load(const Json& json, Animation::Channel::Target* out) {
    LoadField(json, "node", kGltfSeverityNone, &out->node);
    LoadField(json, "path", kGltfSeverityError, &out->path);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Animation::Channel* out) {
    LoadField(json, "sampler", kGltfSeverityError, &out->sampler);
    LoadField(json, "target", kGltfSeverityError, &out->target);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Animation::Sampler::Interpolation* out) {
    LoadEnumByString(json, kGltfSeverityError, out);
  }

  void Load(const Json& json, Animation::Sampler* out) {
    LoadField(json, "input", kGltfSeverityError, &out->input);
    LoadField(json, "interpolation", kGltfSeverityNone, &out->interpolation);
    LoadField(json, "output", kGltfSeverityError, &out->output);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Animation* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadFieldArray(json, "channels", kGltfSeverityError, &out->channels);
    LoadFieldArray(json, "samplers", kGltfSeverityError, &out->samplers);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Buffer* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "uri", kGltfSeverityNone, &out->uri);
    LoadField(json, "byteLength", kGltfSeverityError, &out->byteLength);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, BufferView::Target* out) {
    LoadEnumByInt(
        json,
        Gltf::kBufferViewTargetValues, arraysize(Gltf::kBufferViewTargetValues),
        Gltf::BufferView::kTargetUnset, out);
  }

  void Load(const Json& json, BufferView* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "buffer", kGltfSeverityError, &out->buffer);
    LoadField(json, "byteOffset", kGltfSeverityNone, &out->byteOffset);
    LoadField(json, "byteLength", kGltfSeverityError, &out->byteLength);
    LoadField(json, "byteStride", kGltfSeverityNone, &out->byteStride);
    LoadField(json, "target", kGltfSeverityNone, &out->target);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Camera::Type* out) {
    LoadEnumByString(json, kGltfSeverityError, out);
  }

  void Load(const Json& json, Camera::Orthographic* out) {
    LoadField(json, "xmag", kGltfSeverityError, &out->xmag);
    LoadField(json, "ymag", kGltfSeverityError, &out->ymag);
    LoadField(json, "zfar", kGltfSeverityError, &out->zfar);
    LoadField(json, "znear", kGltfSeverityError, &out->znear);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Camera::Perspective* out) {
    LoadField(json, "aspectRatio", kGltfSeverityNone, &out->aspectRatio);
    LoadField(json, "yfov", kGltfSeverityError, &out->yfov);
    LoadField(json, "zfar", kGltfSeverityNone, &out->zfar);
    LoadField(json, "znear", kGltfSeverityError, &out->znear);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Camera* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "type", kGltfSeverityError, &out->type);
    switch (out->type) {
    case Camera::kTypeOrthographic:
      out->orthographic = Camera::Orthographic::kDefault;
      LoadField(json, "orthographic", kGltfSeverityError, &out->orthographic);
      break;
    case Camera::kTypePerspective:
      out->perspective = Camera::Perspective::kDefault;
      LoadField(json, "perspective", kGltfSeverityError, &out->perspective);
      break;
    case Camera::kTypeCount:
    default:
      // Cannot occur. Silence the compiler warning.
      break;
    }
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Image::MimeType* out) {
    LoadEnumByString(json, kGltfSeverityError, out);
  }

  void Load(const Json& json, Image* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "uri", kGltfSeverityNone, &out->uri);
    const bool is_uri = out->uri.IsSet();
    LoadField(json, "mimeType", is_uri ? kGltfSeverityNone : kGltfSeverityError,
              &out->mimeType);
    if (!is_uri) {
      LoadField(json, "bufferView", kGltfSeverityError, &out->bufferView);
    }
    WarnUnusedExtensionsAndExtras(json);
  }

  struct TextureTransformWithTexCoord {
    Gltf::Material::Texture::Transform transform;
    // [Optional] Overrides the textureInfo texCoord value if supplied, and if
    // this extension is supported.
    uint8_t texCoord;
  };
  void Load(const Json& json, TextureTransformWithTexCoord* out) {
    LoadFieldArray(json, "offset", kGltfSeverityNone, out->transform.offset);
    LoadField(json, "rotation", kGltfSeverityNone, &out->transform.rotation);
    LoadFieldArray(json, "scale", kGltfSeverityNone, out->transform.scale);
    LoadField(json, "texCoord", kGltfSeverityNone, &out->texCoord);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Material::Texture* out) {
    LoadField(json, "index", kGltfSeverityError, &out->index);
    LoadField(json, "texCoord", kGltfSeverityNone, &out->texCoord);
    TextureTransformWithTexCoord transform = { out->transform, out->texCoord };
    if (LoadExtension(json, Gltf::kExtensionTextureTransform, &transform)) {
      out->transform = transform.transform;
      out->texCoord = transform.texCoord;
    }
    WarnUnusedExtensionsAndExtras(json, { Gltf::kExtensionTextureTransform });
  }

  void Load(const Json& json, Material::NormalTexture* out) {
    Load(json, static_cast<Material::Texture*>(out));
    LoadField(json, "scale", kGltfSeverityNone, &out->scale);
  }

  void Load(const Json& json, Material::OcclusionTexture* out) {
    Load(json, static_cast<Material::Texture*>(out));
    LoadField(json, "strength", kGltfSeverityNone, &out->strength);
  }

  void Load(const Json& json, Material::Pbr* out) {
    LoadFieldArray(json, "baseColorFactor", kGltfSeverityNone,
        out->baseColorFactor);
    LoadField(json, "baseColorTexture", kGltfSeverityNone,
        &out->baseColorTexture);
    LoadField(json, "metallicFactor", kGltfSeverityNone,
        &out->metallicFactor);
    LoadField(json, "roughnessFactor", kGltfSeverityNone,
        &out->roughnessFactor);
    LoadField(json, "metallicRoughnessTexture", kGltfSeverityNone,
        &out->metallicRoughnessTexture);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Material::AlphaMode* out) {
    LoadEnumByString(json, kGltfSeverityError, out);
  }

  void Load(const Json& json, Material::Pbr::SpecGloss* out) {
    LoadFieldArray(json, "diffuseFactor", kGltfSeverityNone,
        out->diffuseFactor);
    LoadField(json, "diffuseTexture", kGltfSeverityNone,
        &out->diffuseTexture);
    LoadFieldArray(json, "specularFactor", kGltfSeverityNone,
        out->specularFactor);
    LoadField(json, "glossinessFactor", kGltfSeverityNone,
        &out->glossinessFactor);
    LoadField(json, "specularGlossinessTexture", kGltfSeverityNone,
        &out->specularGlossinessTexture);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Material* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "pbrMetallicRoughness", kGltfSeverityNone, &out->pbr);
    LoadExtension(json, Gltf::kExtensionSpecGloss, &out->pbr.specGloss);
    LoadField(json, "normalTexture", kGltfSeverityNone, &out->normalTexture);
    LoadField(json, "occlusionTexture", kGltfSeverityNone,
        &out->occlusionTexture);
    LoadField(json, "emissiveTexture", kGltfSeverityNone,
        &out->emissiveTexture);
    LoadFieldArray(json, "emissiveFactor", kGltfSeverityNone,
        out->emissiveFactor);
    LoadField(json, "alphaMode", kGltfSeverityNone, &out->alphaMode);
    LoadField(json, "alphaCutoff", kGltfSeverityNone, &out->alphaCutoff);
    LoadField(json, "doubleSided", kGltfSeverityNone, &out->doubleSided);
    out->unlit = HaveExtension(json, Gltf::kExtensionUnlit);
    WarnUnusedExtensionsAndExtras(
        json, {Gltf::kExtensionSpecGloss, Gltf::kExtensionUnlit});
  }

  void Load(const Json& json, Mesh::AttributeSet* out) {
    for (auto it = json.begin(), end = json.end(); it != end; ++it) {
      Mesh::Attribute attribute;
      const std::string& key = it.key();
      if (!GetSemantic(key.c_str(), key.length(), &attribute)) {
        Log<GLTF_ERROR_BAD_SEMANTIC>(key.c_str());
        return;
      }
      const Json& accessor_json = it.value();
      Load(accessor_json, &attribute.accessor);
      if (attribute.accessor == Id::kNull) {
        Log<GLTF_ERROR_MISSING_ACCESSOR>();
        return;
      }
      // TODO: We cannot detect duplicate attributes because the json
      // parser silently discards duplicate map entries.
      const auto insert_result = out->insert(attribute);
      if (!insert_result.second) {
        Log<GLTF_ERROR_DUPLICATE_ATTR>(key.c_str());
        return;
      }
    }
  }

  void Load(const Json& json, Mesh::Primitive::Mode* out) {
    LoadEnumByInt(json, Gltf::kMeshPrimitiveModeValues,
                  arraysize(Gltf::kMeshPrimitiveModeValues),
                  Gltf::Mesh::Primitive::kModeCount, out);
  }

  void Load(const Json& json, Mesh::Primitive::Draco* out) {
    LoadField(json, "bufferView", kGltfSeverityError, &out->bufferView);
    LoadField(json, "attributes", kGltfSeverityError, &out->attributes);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Mesh::Primitive* out) {
    LoadField(json, "attributes", kGltfSeverityError, &out->attributes);
    LoadField(json, "indices", kGltfSeverityNone, &out->indices);
    LoadField(json, "material", kGltfSeverityNone, &out->material);
    LoadField(json, "mode", kGltfSeverityNone, &out->mode);

    // TODO: Can Draco extensions be nested under morph targets? I
    // suspect they can to support compressed morph targets, but I don't have an
    // example using this.
    LoadFieldArray(json, "targets", kGltfSeverityNone, &out->targets);

    LoadExtension(json, Gltf::kExtensionDraco, &out->draco);
    WarnUnusedExtensionsAndExtras(json, { Gltf::kExtensionDraco });
  }

  void Load(const Json& json, Mesh* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadFieldArray(json, "primitives", kGltfSeverityError, &out->primitives);
    LoadFieldArray(json, "weights", kGltfSeverityNone, &out->weights);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Node* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "camera", kGltfSeverityNone, &out->camera);
    LoadField(json, "mesh", kGltfSeverityNone, &out->mesh);
    LoadField(json, "skin", kGltfSeverityNone, &out->skin);
    if (out->skin != Gltf::Id::kNull && out->mesh == Gltf::Id::kNull) {
      Log<GLTF_ERROR_SKIN_WITHOUT_MESH>(Gltf::IdToIndex(out->skin));
    }
    if (LoadFieldArray(json, "matrix", kGltfSeverityNone, out->matrix)) {
      out->is_matrix = true;
    } else {
      out->is_matrix = false;
      LoadFieldArray(json, "scale", kGltfSeverityNone, out->scale);
      LoadFieldArray(json, "rotation", kGltfSeverityNone, out->rotation);
      LoadFieldArray(json, "translation", kGltfSeverityNone, out->translation);
    }
    LoadFieldArray(json, "children", kGltfSeverityNone, &out->children);
    LoadFieldArray(json, "weights", kGltfSeverityNone, &out->weights);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Sampler::MagFilter* out) {
    LoadEnumByInt(json, Gltf::kSamplerMagFilterValues,
                  arraysize(Gltf::kSamplerMagFilterValues),
                  Gltf::Sampler::kMagFilterUnset, out);
  }

  void Load(const Json& json, Sampler::MinFilter* out) {
    LoadEnumByInt(json, Gltf::kSamplerMinFilterValues,
                  arraysize(Gltf::kSamplerMinFilterValues),
                  Gltf::Sampler::kMinFilterUnset, out);
  }

  void Load(const Json& json, Sampler::WrapMode* out) {
    LoadEnumByInt(json, Gltf::kSamplerWrapModeValues,
                  arraysize(Gltf::kSamplerWrapModeValues),
                  Gltf::Sampler::kWrapUnset, out);
  }

  void Load(const Json& json, Sampler* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "magFilter", kGltfSeverityNone, &out->magFilter);
    LoadField(json, "minFilter", kGltfSeverityNone, &out->minFilter);
    LoadField(json, "wrapS", kGltfSeverityNone, &out->wrapS);
    LoadField(json, "wrapT", kGltfSeverityNone, &out->wrapT);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Scene* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadFieldArray(json, "nodes", kGltfSeverityNone, &out->nodes);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Skin* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "inverseBindMatrices", kGltfSeverityNone,
        &out->inverseBindMatrices);
    LoadField(json, "skeleton", kGltfSeverityNone, &out->skeleton);
    LoadFieldArray(json, "joints", kGltfSeverityError, &out->joints);
    WarnUnusedExtensionsAndExtras(json);
  }

  void Load(const Json& json, Texture* out) {
    LoadField(json, "name", kGltfSeverityNone, &out->name);
    LoadField(json, "sampler", kGltfSeverityNone, &out->sampler);
    LoadField(json, "source", kGltfSeverityNone, &out->source);
    WarnUnusedExtensionsAndExtras(json);
  }
};

}  // namespace

bool GltfLoad(
    std::istream& is, const char* name, const GltfLoadSettings& settings,
    Gltf* out_gltf, GltfLogger* logger) {
  const size_t old_error_count = logger->GetErrorCount();

  // Parse json file.
  Json json;
  try {
    is >> json;
  } catch (const std::exception& e) {
    GltfLog<GLTF_ERROR_JSON_PARSE>(logger, "", e.what());
    return false;
  }

  // Parse gltf from json.
  GltfLoader loader;
  Gltf gltf;
  loader.LoadGltf(json, settings, &gltf, logger);
  out_gltf->Swap(&gltf);

  // Fail on any errors.
  const size_t error_count = logger->GetErrorCount();
  return error_count == old_error_count;
}
