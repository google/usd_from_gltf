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

#ifndef UFG_COMMON_SCHEDULER_H_
#define UFG_COMMON_SCHEDULER_H_

#include <condition_variable>  // NOLINT: Unapproved C++11 header.
#include <functional>
#include <mutex>  // NOLINT: Unapproved C++11 header.
#include <queue>
#include <thread>  // NOLINT: Unapproved C++11 header.
#include <vector>
#include "common/common.h"
#include "common/platform.h"

namespace ufg {
// Simple multithreaded job scheduler, used to run conversion tasks in parallel.
class Scheduler {
 public:
  Scheduler();

  // Start worker threads.
  // * If worker_count is 0, the scheduler runs jobs immediately in the calling
  //   thread.
  void Start(size_t worker_count);

  // Stop worker threads. This will wait for all queued jobs to complete.
  void Stop();

  // Schedule a function to run on a worker thread.
  // * The function should have a void() signature.
  template <typename Func>
  void Schedule(Func func) {
    if (workers_.empty()) {
      func();
    } else {
      AddJob(JobFunction(func));
    }
  }

  // Wait for all scheduled jobs to complete.
  void WaitForAllComplete();

 private:
  using JobFunction = std::function<void()>;
  struct Job {
    JobFunction func;
    Job() {}
    explicit Job(JobFunction&& func) : func(func) {}
  };
  bool stopping_;
  std::condition_variable add_or_stop_event_;
  std::condition_variable job_done_event_;
  std::mutex mutex_;
  std::vector<std::thread> workers_;
  std::queue<Job> job_queue_;
  std::queue<std::exception_ptr> exceptions_;

  static void WorkerThread(Scheduler* scheduler, size_t index);
  void AddJob(JobFunction&& func);
};
}  // namespace ufg
#endif  // UFG_COMMON_SCHEDULER_H_
