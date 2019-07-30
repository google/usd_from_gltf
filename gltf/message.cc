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

#include "message.h"  // NOLINT: Silence relative path warning.

#include <algorithm>
#include "internal_util.h"  // NOLINT: Silence relative path warning.

constexpr GltfSeverity WHAT_SEVERITY_INFO = kGltfSeverityNone;
constexpr GltfSeverity WHAT_SEVERITY_WARN = kGltfSeverityWarning;
constexpr GltfSeverity WHAT_SEVERITY_ERROR = kGltfSeverityError;

const GltfWhatInfo kGltfWhatInfos[GLTF_WHAT_COUNT] = {
#define GLTF_MSG(severity, id, format) \
  {WHAT_SEVERITY_##severity, "GLTF_" #severity "_" #id, format},
#include "messages.inl"  // NOLINT: Multiple inclusion.
};

std::string GltfMessage::ToString(
    bool add_severity_prefix, bool add_what_suffix) const {
  std::string str;
  if (add_severity_prefix) {
    static const char* const kGltfSeverityText[] = {
        "",           // kGltfSeverityNone
        "Warning: ",  // kGltfSeverityWarning
        "ERROR: ",    // kGltfSeverityError
    };
    static_assert(arraysize(kGltfSeverityText) == kGltfSeverityCount, "");
    str += kGltfSeverityText[what_info->severity];
  }
  if (!path.empty()) {
    str += path;
    str += ": ";
  }
  str += text;
  if (add_what_suffix) {
    str += " [";
    str += what_info->name;
    str += "]";
  }
  return str;
}

void GltfMessage::AssignFormattedV(
    const GltfWhatInfo* what_info, const char* path, va_list args) {
  // Args cannot be reused in GCC, so copy it for the second call to vsnprintf.
  va_list args_copy;
#ifdef __GNUC__
  va_copy(args_copy, args);
#else  // __GNUC__
  args_copy = args;
#endif  // __GNUC__

  this->what_info = what_info;
  this->path = path ? path : "";
  const size_t len = std::vsnprintf(nullptr, 0, what_info->format, args);
  text.resize(len);
  std::vsnprintf(&text[0], len + 1, what_info->format, args_copy);

#ifdef __GNUC__
  va_end(args_copy);
#endif  // __GNUC__
}

void GltfMessage::AssignFormatted(
    const GltfWhatInfo* what_info, const char* path, ...) {
  va_list args;
  va_start(args, path);
  AssignFormattedV(what_info, path, args);
  va_end(args);
}

void GltfMessage::LogV(
    std::vector<GltfMessage>* messages, const GltfWhatInfo* what_info,
    const char* path, va_list args) {
  messages->push_back(GltfMessage());
  messages->back().AssignFormattedV(what_info, path, args);
}

void GltfMessage::Log(
    std::vector<GltfMessage>* messages, const GltfWhatInfo* what_info,
    const char* path, ...) {
  va_list args;
  va_start(args, path);
  LogV(messages, what_info, path, args);
  va_end(args);
}

size_t GltfMessage::CountErrors(const std::vector<GltfMessage>& messages) {
  size_t error_count = 0;
  for (const GltfMessage& message : messages) {
    if (message.GetSeverity() == kGltfSeverityError) {
      ++error_count;
    }
  }
  return error_count;
}

size_t GltfPrintMessages(
    const GltfMessage* messages, size_t message_count,
    const char* line_prefix, FILE* output_file) {
  size_t warning_count = 0;
  size_t error_count = 0;
  for (size_t i = 0; i != message_count; ++i) {
    const GltfMessage& message = messages[i];
    const GltfSeverity severity = message.GetSeverity();
    const bool is_warning = severity == kGltfSeverityWarning;
    const bool is_error = severity == kGltfSeverityError;
    if (is_warning) {
      ++warning_count;
    }
    if (is_error) {
      ++error_count;
    }
    FILE* const target =
        output_file ? output_file : (is_warning || is_error ? stderr : stdout);
    fprintf(target, "%s%s\n", line_prefix, message.ToString().c_str());
  }
  return error_count;
}

std::string GltfPathStack::GetPath() const {
  std::string path;
  for (const Context& context : context_stack_) {
    if (context.field_name) {
      if (!path.empty()) {
        path += '.';
      }
      path += context.field_name;
    } else {
      path += '[';
      path += std::to_string(context.element_index);
      path += ']';
    }
  }
  return path;
}

void GltfOnceLogger::Reset(GltfLogger* logger) {
  logger_ = logger;
  map_.clear();
  entries_.clear();
}

void GltfOnceLogger::Add(
    const char* footer, const char* name, const GltfMessage& message) {
  const auto insert_result = map_.insert(std::make_pair(message, Value()));

  // Add new entries to the ordered set.
  if (insert_result.second) {
    entries_.push_back(&*insert_result.first);
  }

  // Record optional name.
  Value& value = insert_result.first->second;
  value.footer = footer;
  if (name && name[0]) {
    value.names.insert(name);
  }
}

void GltfOnceLogger::Flush() {
  static constexpr size_t kOnceNameMax = 3;

  for (const Entry* const entry : entries_) {
    const GltfMessage& key = entry->first;
    const Value& value = entry->second;

    const size_t name_count = value.names.size();
    if (name_count == 0) {
      logger_->Add(key);
      continue;
    }

    GltfMessage message = key;
    message.text += value.footer;

    // Append the list of names on a single line, truncated to show only a few
    // entries. When truncated, we display one less than the maximum so the
    // number of elements is always kOnceNameMax, counting the ellipsis.
    const size_t name_shown = name_count <= kOnceNameMax
                                  ? name_count
                                  : std::min(name_count, kOnceNameMax - 1);
    size_t name_i = 0;
    for (const std::string& name : value.names) {
      if (name_i == name_shown) {
        break;
      }
      if (name_i != 0) {
        message.text += ", ";
      }
      message.text += name;
      ++name_i;
    }
    const size_t name_hidden = name_count - name_shown;

    if (name_hidden > 0) {
      message.text += ", ...(plus ";
      message.text += std::to_string(name_hidden);
      message.text += " more)";
    }

    logger_->Add(message);
  }

  Reset(logger_);
}
