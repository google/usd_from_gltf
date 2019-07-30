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

#ifndef GLTF_INTERNAL_UTIL_H_
#define GLTF_INTERNAL_UTIL_H_

#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include "message.h"  // NOLINT: Silence relative path warning.

template <class T, size_t LEN>
char (&ArraySizeHelper(T (&)[LEN]))[LEN];
#define arraysize(array) (sizeof(ArraySizeHelper(array)))

#define CONST_STRLEN(text) (arraysize(text) - 1)

template <typename T>
inline T* PointerOffset(T* p, size_t offset) {
  return reinterpret_cast<T*>(reinterpret_cast<size_t>(p) + offset);
}

template <typename Begin, typename End>
inline ptrdiff_t PointerDistance(Begin* begin, End* end) {
  return reinterpret_cast<size_t>(end) - reinterpret_cast<size_t>(begin);
}

size_t GetFileSize(FILE* fp);
bool SeekAbsolute(FILE* fp, size_t offset);

#endif  // GLTF_INTERNAL_UTIL_H_
