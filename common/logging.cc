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

#include "common/logging.h"

namespace ufg {
namespace {
constexpr size_t kFormatTextMax = 8 * 1024;
constexpr size_t kOnceNameMax = 3;

constexpr Severity WHAT_SEVERITY_INFO = kSeverityNone;  // NOLINT: unused
constexpr Severity WHAT_SEVERITY_WARN = kSeverityWarning;
constexpr Severity WHAT_SEVERITY_ERROR = kSeverityError;

std::string GetAssertText(const char* file, int line, const char* expression) {
  char text[kFormatTextMax];
  snprintf(text, sizeof(text), "%s(%d) : ASSERT(%s)", file, line, expression);
  return text;
}
}  // namespace

const WhatInfo kWhatInfos[UFG_WHAT_COUNT] = {
#define UFG_MSG(severity, id, format) \
  {WHAT_SEVERITY_##severity, "UFG_" #severity "_" #id, format},
#include "messages.inl"  // NOLINT: Multiple inclusion.
};

AssertException::AssertException(const char* file, int line,
                                 const char* expression)
    : std::runtime_error(GetAssertText(file, line, expression)),
      file_(file),
      line_(line),
      expression_(expression) {}

ProfileSentry::ProfileSentry(const char* label, bool enable)
    : label_(enable ? label : nullptr) {
  if (label_) {
    time_begin_ = std::clock();
  }
}

ProfileSentry::~ProfileSentry() {
  if (label_) {
    const std::clock_t time_end = std::clock();
    const float elapsed =
        static_cast<float>(time_end - time_begin_) / CLOCKS_PER_SEC;
    printf("%s: Completed in %.3f seconds.\n", label_, elapsed);
  }
}
}  // namespace ufg
