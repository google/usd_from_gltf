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

#ifndef USD_FROM_GLTF_ARGS_H_
#define USD_FROM_GLTF_ARGS_H_

#include "common/config.h"
#include "common/logging.h"

struct Args {
  struct Job {
    std::string src;
    std::string dst;
  };
  ufg::ConvertSettings settings;
  std::vector<Job> jobs;
};

bool ParseArgs(
    int argc, const char* const* argv, Args* out_args, ufg::Logger* logger);

#endif  // USD_FROM_GLTF_ARGS_H_
