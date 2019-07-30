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

#ifndef UFG_COMMON_COMMON_UTIL_H_
#define UFG_COMMON_COMMON_UTIL_H_

#include <string.h>
#include <string>
#include "common/common.h"

namespace ufg {
// Return std::vector data, or null if the vector is empty.
template <typename Vector>
inline typename Vector::value_type* GetDataOrNull(Vector& v) {
  return v.empty() ? nullptr : v.data();
}
template <typename Vector>
inline const typename Vector::value_type* GetDataOrNull(const Vector& v) {
  return v.empty() ? nullptr : v.data();
}

struct FileReference {
  std::string disk_path;
  std::string usd_path;
  friend bool operator<(const FileReference& a, const FileReference& b) {
    return a.usd_path < b.usd_path;
  }
  friend bool operator==(const FileReference& a, const FileReference& b) {
    return a.usd_path == b.usd_path;
  }
  friend bool operator!=(const FileReference& a, const FileReference& b) {
    return a.usd_path != b.usd_path;
  }
};

inline std::string AppendNumber(const char* prefix, size_t prefix_len,
                                size_t index) {
  const size_t len_max = prefix_len + 24;
  std::string text(len_max, 0);
  const size_t len = snprintf(&text[0], len_max, "%s%zu", prefix, index);
  text.resize(len);
  return text;
}

inline std::string AppendNumber(const char* prefix, size_t index) {
  return AppendNumber(prefix, strlen(prefix), index);
}

inline std::string AppendNumber(const std::string& prefix, size_t index) {
  return AppendNumber(prefix.c_str(), prefix.length(), index);
}

inline std::string AddFileNameSuffix(const std::string& path,
                                     const char* suffix) {
  const size_t last_dot_pos = path.rfind('.');
  if (last_dot_pos == std::string::npos) {
    return path + suffix;
  } else {
    const char* const ext = path.c_str() + last_dot_pos;
    return path.substr(0, last_dot_pos) + suffix + ext;
  }
}

inline const char* GetFileName(const std::string& path) {
  const size_t last_slash_pos = path.find_last_of("\\/");
  return last_slash_pos == std::string::npos
             ? path.c_str()
             : path.c_str() + last_slash_pos + 1;
}

inline std::string GetFileDirectory(const std::string& path) {
  const size_t last_slash_pos = path.find_last_of("\\/");
  return last_slash_pos == std::string::npos
             ? std::string()
             : std::string(path.c_str(), last_slash_pos);
}
}  // namespace ufg

#endif  // UFG_COMMON_COMMON_UTIL_H_
