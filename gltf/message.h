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

#ifndef GLTF_MESSAGE_H_
#define GLTF_MESSAGE_H_

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

enum GltfSeverity : uint8_t {
  kGltfSeverityNone,
  kGltfSeverityWarning,
  kGltfSeverityError,
  kGltfSeverityCount
};

enum GltfWhat : uint8_t {
#define GLTF_MSG(severity, id, format) GLTF_##severity##_##id,
#include "messages.inl"  // NOLINT: Multiple inclusion.
  GLTF_WHAT_COUNT
};

struct GltfWhatInfo {
  GltfSeverity severity;
  const char* name;
  const char* format;
};
extern const GltfWhatInfo kGltfWhatInfos[GLTF_WHAT_COUNT];

struct GltfMessage {
  const GltfWhatInfo* what_info;
  std::string path;
  std::string text;

  std::string ToString(bool add_severity_prefix = true,
                       bool add_what_suffix = true) const;

  void Assign(const GltfWhatInfo* what_info,
              const char* path, const char* text) {
    this->what_info = what_info;
    this->path = path;
    this->text = text;
  }

  void Assign(const GltfWhatInfo* what_info,
              const std::string& path, const std::string& text) {
    this->what_info = what_info;
    this->path = path;
    this->text = text;
  }

  GltfSeverity GetSeverity() const { return what_info->severity; }

  void AssignFormattedV(
      const GltfWhatInfo* what_info, const char* path, va_list args);
  void AssignFormatted(
      const GltfWhatInfo* what_info, const char* path, ...);

  static GltfMessage ConstructFormattedV(
      const GltfWhatInfo* what_info, const char* path, va_list args) {
    GltfMessage message;
    message.AssignFormattedV(what_info, path, args);
    return message;
  }

  static GltfMessage ConstructFormatted(
      const GltfWhatInfo* what_info, const char* path, ...) {
    GltfMessage message;
    va_list args;
    va_start(args, path);
    message.AssignFormattedV(what_info, path, args);
    va_end(args);
    return message;
  }

  static void LogV(
      std::vector<GltfMessage>* messages, const GltfWhatInfo* what_info,
      const char* path, va_list args);
  static void Log(
      std::vector<GltfMessage>* messages, const GltfWhatInfo* what_info,
      const char* path, ...);

  static size_t CountErrors(const std::vector<GltfMessage>& messages);
};

template <GltfWhat kWhat> struct GltfLoggerT;

#define GLTF_MSG0(severity, id, format)                         \
  template <>                                                   \
  struct GltfLoggerT<GLTF_##severity##_##id> {                  \
    static GltfMessage Get() {                                  \
      return GltfMessage::ConstructFormatted(                   \
          &kGltfWhatInfos[GLTF_##severity##_##id], "");         \
    }                                                           \
  };
#define GLTF_MSG1(severity, id, format,                         \
                  T0, v0)                                       \
  template <> struct GltfLoggerT<GLTF_##severity##_##id> {      \
    static GltfMessage Get(T0 v0) {                             \
      return GltfMessage::ConstructFormatted(                   \
          &kGltfWhatInfos[GLTF_##severity##_##id], "",          \
          v0);                                                  \
    }                                                           \
  };
#define GLTF_MSG2(severity, id, format,                         \
                  T0, v0, T1, v1)                               \
  template <> struct GltfLoggerT<GLTF_##severity##_##id> {      \
    static GltfMessage Get(T0 v0, T1 v1) {                      \
      return GltfMessage::ConstructFormatted(                   \
          &kGltfWhatInfos[GLTF_##severity##_##id], "",          \
          v0, v1);                                              \
    }                                                           \
  };
#define GLTF_MSG3(severity, id, format,                         \
                  T0, v0, T1, v1, T2, v2)                       \
  template <> struct GltfLoggerT<GLTF_##severity##_##id> {      \
    static GltfMessage Get(T0 v0, T1 v1, T2 v2) {               \
      return GltfMessage::ConstructFormatted(                   \
          &kGltfWhatInfos[GLTF_##severity##_##id], "",          \
          v0, v1, v2);                                          \
    }                                                           \
  };
#define GLTF_MSG4(severity, id, format,                         \
                  T0, v0, T1, v1, T2, v2, T3, v3)               \
  template <> struct GltfLoggerT<GLTF_##severity##_##id> {      \
    static GltfMessage Get(T0 v0, T1 v1, T2 v2, T3 v3) {        \
      return GltfMessage::ConstructFormatted(                   \
          &kGltfWhatInfos[GLTF_##severity##_##id], "",          \
          v0, v1, v2, v3);                                      \
    }                                                           \
  };
#define GLTF_MSG5(severity, id, format,                         \
                  T0, v0, T1, v1, T2, v2, T3, v3, T4, v4)       \
  template <> struct GltfLoggerT<GLTF_##severity##_##id> {      \
    static GltfMessage Get(T0 v0, T1 v1, T2 v2, T3 v3, T4 v4) { \
      return GltfMessage::ConstructFormatted(                   \
          &kGltfWhatInfos[GLTF_##severity##_##id], "",          \
          v0, v1, v2, v3, v4);                                  \
    }                                                           \
  };
#include "messages.inl"  // NOLINT: Multiple inclusion.

template <GltfWhat kWhat, typename ...Ts>
inline GltfMessage GltfGetMessage(const char* path, Ts... args) {
  GltfMessage message = GltfLoggerT<kWhat>::Get(args...);
  message.path = path ? path : "";
  return message;
}

// Print messages to stdout/stderr, and return the number of errors.
size_t GltfPrintMessages(
    const GltfMessage* messages, size_t message_count,
    const char* line_prefix = "", FILE* output_file = nullptr);

inline size_t GltfPrintMessages(
    const std::vector<GltfMessage>& messages,
    const char* line_prefix = "", FILE* output_file = nullptr) {
  return GltfPrintMessages(
      messages.data(), messages.size(), line_prefix, output_file);
}

// Utility used during glTF tree traversal to track field paths.
class GltfPathStack {
 public:
  void Clear() {
    context_stack_.clear();
  }
  void Enter(const char* field_name) {
    context_stack_.push_back({field_name, 0});
  }
  void Enter(size_t element_index) {
    context_stack_.push_back({ nullptr, element_index });
  }
  void Exit() {
    context_stack_.pop_back();
  }
  std::string GetPath() const;

  // Calls Enter/Exit within a function scope.
  struct Sentry {
    GltfPathStack* stack;
    Sentry(GltfPathStack* stack, const char* name) : stack(stack) {
      stack->Enter(name);
    }
    Sentry(GltfPathStack* stack, size_t element_index) : stack(stack) {
      stack->Enter(element_index);
    }
    ~Sentry() {
      stack->Exit();
    }
  };

 private:
  struct Context {
    const char* field_name;
    size_t element_index;
  };
  std::vector<Context> context_stack_;
};

// Abstract interface used to log messages.
class GltfLogger {
 public:
  virtual ~GltfLogger() {}
  virtual void Add(const GltfMessage& message) = 0;
  virtual size_t GetErrorCount() const = 0;

  // Push/pop the name of the current object being logged, used to provide
  // additional context in implementation messages.
  void PushName(const std::string& name) { names_.push_back(name); }
  void PopName() { names_.pop_back(); }

  // Get the current object name, if any.
  const std::string& GetName() const {
    return names_.empty() ? empty_name_ : names_.back();
  }

  // Clear object name stack.
  void ClearNames() { names_.clear(); }

  // Push/Pop object name within a local function scope.
  struct NameSentry {
    GltfLogger* logger;
    NameSentry(GltfLogger* logger, const std::string& name) : logger(logger) {
      logger->PushName(name);
    }
    ~NameSentry() { logger->PopName(); }
  };

 private:
  std::string empty_name_;
  std::vector<std::string> names_;
};

// Logger that prints to stdout or a file.
class GltfPrintLogger : public GltfLogger {
 public:
  explicit GltfPrintLogger(
      const char* line_prefix = "", FILE* output_file = nullptr)
      : line_prefix_(line_prefix), output_file_(output_file), error_count_(0) {}

  void SetLinePrefix(const std::string& line_prefix) {
    line_prefix_ = line_prefix;
  }

  void SetOutputFile(FILE* output_file) {
    output_file_ = output_file;
  }

  void Add(const GltfMessage& message) override {
    GltfPrintMessages(&message, 1, line_prefix_.c_str(), output_file_);
    if (message.GetSeverity() == kGltfSeverityError) {
      ++error_count_;
    }
  }

  size_t GetErrorCount() const override {
    return error_count_;
  }

 private:
  std::string line_prefix_;
  FILE* output_file_;
  size_t error_count_;
};

// Logger that stores messages in a vector.
class GltfVectorLogger : public GltfLogger {
 public:
  void Add(const GltfMessage& message) override {
    messages_.push_back(message);
  }

  size_t GetErrorCount() const override {
    return GltfMessage::CountErrors(messages_);
  }

  void Clear() {
    messages_.clear();
  }

  const std::vector<GltfMessage>& GetMessages() const {
    return messages_;
  }

  void Print(const char* line_prefix = "", FILE* output_file = nullptr) {
    GltfPrintMessages(messages_, line_prefix, output_file);
  }

  void PrintAndClear(
      const char* line_prefix = "", FILE* output_file = nullptr) {
    Print(line_prefix, output_file);
    Clear();
  }

 private:
  std::vector<GltfMessage> messages_;
};

// Utility used to merge similar messages.
class GltfOnceLogger {
 public:
  GltfOnceLogger() : logger_(nullptr) {}

  GltfLogger* GetLogger() const { return logger_; }
  void Reset(GltfLogger* logger);
  void Add(const char* footer, const char* name, const GltfMessage& message);
  void Flush();

 private:
  struct Value {
    std::string footer;
    std::set<std::string> names;
  };
  struct MessageHasher {
    std::size_t operator()(const GltfMessage& k) const {
      return std::hash<std::string>()(k.text);
    }
  };
  struct MessageEqual {
    std::size_t operator()(const GltfMessage& a, const GltfMessage& b) const {
      return a.what_info == b.what_info && a.text == b.text;
    }
  };
  using Map =
      std::unordered_map<GltfMessage, Value, MessageHasher, MessageEqual>;
  using Entry = Map::value_type;
  GltfLogger* logger_;
  Map map_;
  std::vector<const Entry*> entries_;
};

template <GltfWhat kWhat, typename... Ts>
inline void GltfLog(GltfLogger* logger, const char* path, Ts... args) {
  logger->Add(GltfGetMessage<kWhat>(path, args...));
}

template <GltfWhat kWhat, typename... Ts>
inline void GltfLogOnce(
    GltfOnceLogger* once_logger, const char* footer, const char* name,
    Ts... args) {
  once_logger->Add(footer, name, GltfGetMessage<kWhat>("", args...));
}

#endif  // GLTF_MESSAGE_H_
