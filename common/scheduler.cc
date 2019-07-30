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

#include "common/scheduler.h"

#include "common/logging.h"

namespace ufg {
namespace {
constexpr size_t kWorkerMax = 64;
}  // namespace

Scheduler::Scheduler() : stopping_(false) {
}

void Scheduler::Start(size_t worker_count) {
  worker_count = std::min(worker_count, kWorkerMax);

  UFG_ASSERT_LOGIC(!stopping_);
  UFG_ASSERT_LOGIC(workers_.empty());
  UFG_ASSERT_LOGIC(job_queue_.empty());
  stopping_ = false;
  workers_.reserve(worker_count);
  for (size_t worker_index = 0; worker_index != worker_count; ++worker_index) {
    workers_.emplace_back(WorkerThread, this, worker_index);
  }
}

void Scheduler::Stop() {
  {
    std::unique_lock<std::mutex> lock(mutex_);
    stopping_ = true;
    add_or_stop_event_.notify_all();
  }
  for (std::thread& worker : workers_) {
    worker.join();
  }
  if (!exceptions_.empty()) {
    std::rethrow_exception(exceptions_.front());
  }
  workers_.clear();
  UFG_ASSERT_LOGIC(job_queue_.empty());
}

void Scheduler::WaitForAllComplete() {
  std::queue<std::exception_ptr> exceptions;
  for (;;) {
    std::unique_lock<std::mutex> lock(mutex_);
    if (job_queue_.empty()) {
      exceptions.swap(exceptions_);
      break;
    }
    job_done_event_.wait(lock);
  }
  if (!exceptions.empty()) {
    std::rethrow_exception(exceptions.front());
  }
}

void Scheduler::WorkerThread(Scheduler* scheduler, size_t index) {
  while (!scheduler->stopping_) {
    bool have_job = false;
    JobFunction func;
    {
      std::unique_lock<std::mutex> lock(scheduler->mutex_);
      if (!scheduler->job_queue_.empty()) {
        func.swap(scheduler->job_queue_.front().func);
        scheduler->job_queue_.pop();
        have_job = true;
      } else {
        // Wait for a job to become available, or the signal to stop.
        // * Don't wait if we're stopping so the event isn't prematurely cleared
        //   (which could cause a deadlock).
        if (!scheduler->stopping_) {
          scheduler->add_or_stop_event_.wait(lock);
          if (scheduler->stopping_) {
            // Waiting on the event clears it, so we need to re-signal it to
            // ensure any remaining workers wake up to stop.
            scheduler->add_or_stop_event_.notify_all();
          }
        }
      }
    }
    if (have_job) {
      try {
        func();
      } catch (...) {
        std::unique_lock<std::mutex> lock(scheduler->mutex_);
        scheduler->exceptions_.push(std::current_exception());
      }
      scheduler->job_done_event_.notify_all();
    }
  }
}

void Scheduler::AddJob(JobFunction&& func) {
  std::unique_lock<std::mutex> lock(mutex_);
  job_queue_.push(Job(std::move(func)));
  add_or_stop_event_.notify_all();
}
}  // namespace ufg
