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

#include "internal_util.h"  // NOLINT: Silence relative path warning.

size_t GetFileSize(FILE* fp) {
  const long pos = ftell(fp);  // NOLINT: ftell returns long.
  fseek(fp, 0, SEEK_END);
  const size_t file_size = ftell(fp);
  fseek(fp, pos, SEEK_SET);
  return file_size;
}

bool SeekAbsolute(FILE* fp, size_t offset) {
  // TODO: 64-bit version?
  const long offset_long = static_cast<long>(offset);  // NOLINT
  if (static_cast<size_t>(offset_long) != offset) {
    return false;
  }
  return fseek(fp, offset_long, SEEK_SET) == 0;
}
