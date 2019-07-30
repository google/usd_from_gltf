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

#ifndef UFG_COMMON_PLATFORM_H_
#define UFG_COMMON_PLATFORM_H_

#include <string>

// Used for DebugBreak.
#ifdef _MSC_VER
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifdef ERROR
#undef ERROR
#endif  // ERROR
#endif  // _MSC_VER

namespace ufg {
std::string GetCwd();
void SetCwd(const char* dir);
}  // namespace ufg

#endif  // UFG_COMMON_PLATFORM_H_
