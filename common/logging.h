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

#ifndef UFG_COMMON_LOGGING_H_
#define UFG_COMMON_LOGGING_H_

#include <ctime>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include "common/common.h"
#include "common/platform.h"
#include "gltf/message.h"

#if UFG_BREAK_ON_ASSERT
#define UFG_BREAK() DebugBreak()
#define UFG_ASSERT_HELPER(x, file, line, expression)            \
do {                                                            \
  if (!(x)) {                                                   \
    if (IsDebuggerPresent()) {                                  \
      UFG_BREAK();                                              \
    } else {                                                    \
      throw ufg::AssertException(file, line, expression);       \
    }                                                           \
  }                                                             \
} while (0)
#else  // UFG_BREAK_ON_ASSERT
#define UFG_BREAK() ((void)0)
#define UFG_ASSERT_HELPER(x, file, line, expression)            \
do {                                                            \
  if (!(x)) {                                                   \
    throw ufg::AssertException(file, line, expression);         \
  }                                                             \
} while (0)
#endif  // UFG_BREAK_ON_ASSERT

#define UFG_ASSERT(x) UFG_ASSERT_HELPER(x, __FILE__, __LINE__, #x)

// Asserts for glTF format errors.
#define UFG_ASSERT_FORMAT(x) UFG_ASSERT(x)

// Asserts that should only occur due to logic bugs.
#define UFG_ASSERT_LOGIC(x) UFG_ASSERT(x)

namespace ufg {

using Logger = GltfLogger;
using OnceLogger = GltfOnceLogger;
using Message = GltfMessage;
using Severity = GltfSeverity;
using WhatInfo = GltfWhatInfo;

const Severity kSeverityNone = kGltfSeverityNone;
const Severity kSeverityWarning = kGltfSeverityWarning;
const Severity kSeverityError = kGltfSeverityError;
const Severity kSeverityCount = kGltfSeverityCount;

enum What : uint8_t {
#define UFG_MSG(severity, id, format) \
  UFG_##severity##_##id,
#include "messages.inl"  // NOLINT: Multiple inclusion.
  UFG_WHAT_COUNT
};

extern const WhatInfo kWhatInfos[UFG_WHAT_COUNT];

template <What kWhat> struct LoggerT;

#define UFG_MSG0(severity, id, format)                             \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get() {                                         \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "");                 \
    }                                                              \
  };
#define UFG_MSG1(severity, id, format,                             \
                 T0, v0)                                           \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get(T0 v0) {                                    \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "",                  \
          v0);                                                     \
    }                                                              \
  };
#define UFG_MSG2(severity, id, format,                             \
                 T0, v0, T1, v1)                                   \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get(T0 v0, T1 v1) {                             \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "",                  \
          v0, v1);                                                 \
    }                                                              \
  };
#define UFG_MSG3(severity, id, format,                             \
                 T0, v0, T1, v1, T2, v2)                           \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get(T0 v0, T1 v1, T2 v2) {                      \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "",                  \
          v0, v1, v2);                                             \
    }                                                              \
  };
#define UFG_MSG4(severity, id, format,                             \
                 T0, v0, T1, v1, T2, v2, T3, v3)                   \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get(T0 v0, T1 v1, T2 v2, T3 v3) {               \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "",                  \
          v0, v1, v2, v3);                                         \
    }                                                              \
  };
#define UFG_MSG5(severity, id, format,                             \
                 T0, v0, T1, v1, T2, v2, T3, v3, T4, v4)           \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get(T0 v0, T1 v1, T2 v2, T3 v3, T4 v4) {        \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "",                  \
          v0, v1, v2, v3, v4);                                     \
    }                                                              \
  };
#define UFG_MSG6(severity, id, format,                             \
                 T0, v0, T1, v1, T2, v2, T3, v3, T4, v4, T5, v5)   \
  template <> struct LoggerT<UFG_##severity##_##id> {              \
    static Message Get(T0 v0, T1 v1, T2 v2, T3 v3, T4 v4, T5 v5) { \
      return Message::ConstructFormatted(                          \
          &kWhatInfos[UFG_##severity##_##id], "",                  \
          v0, v1, v2, v3, v4, v5);                                 \
    }                                                              \
  };
#include "messages.inl"  // NOLINT: Multiple inclusion.

template <What kWhat, typename ...Ts>
inline void Log(Logger* logger, const char* path, Ts... args) {
  Message message = LoggerT<kWhat>::Get(args...);
  message.path = path && path[0] ? path : logger->GetName();
  logger->Add(message);
}

template <What kWhat, typename... Ts>
inline void LogOnce(
    OnceLogger* once_logger, const char* footer, const char* name, Ts... args) {
  const char* const log_name =
      name && name[0] ? name : once_logger->GetLogger()->GetName().c_str();
  once_logger->Add(footer, log_name, LoggerT<kWhat>::Get(args...));
}

class AssertException : public std::runtime_error {
 public:
  AssertException(const char* file, int line, const char* expression);
  const char* GetFile() const { return file_; }
  int GetLine() const { return line_; }
  const char* GetExpression() const { return expression_; }
 private:
  const char* file_;
  int line_;
  const char* expression_;
};

template <typename T>
T CheckHelper(T x, const char* file, int line, const char* expression) {
  UFG_ASSERT_HELPER(x, file, line, expression);
  return x;
}

// Like UFG_ASSERT, except the expression is always evaluated and it returns the
// result of the expression.
#define UFG_VERIFY(x) ufg::CheckHelper(x, __FILE__, __LINE__, #x)

// Simple utility to profile timing in a local function scope and output to the
// log.
class ProfileSentry {
 public:
  explicit ProfileSentry(const char* label, bool enable = true);
  ~ProfileSentry();
 private:
  const char* label_;
  std::clock_t time_begin_;
};

}  // namespace ufg
#endif  // UFG_COMMON_LOGGING_H_
