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

#include <stdio.h>
#include "args.h"  // NOLINT: Silence relative path warning.
#include "common/logging.h"
#include "convert/package.h"

int main(int argc, char* argv[]) {
  GltfPrintLogger logger;

  Args args;
  if (!ParseArgs(argc, argv, &args, &logger)) {
    return -1;
  }
  if (args.jobs.empty()) {
    return 0;
  }

  if (!ufg::RegisterPlugins(args.settings.plugin_path, &logger)) {
    return -1;
  }

  bool success = true;
  {
    ufg::ProfileSentry profile_sentry("Convert", args.settings.print_timing);
    for (const Args::Job& job : args.jobs) {
      if (args.jobs.size() > 1) {
        printf("%s\n", job.src.c_str());
        logger.SetLinePrefix("  ");
      }
      if (!ufg::ConvertGltfToUsd(
              job.src.c_str(), job.dst.c_str(), args.settings, &logger)) {
        success = false;
      }
    }
  }
  return success ? 0 : -1;
}
