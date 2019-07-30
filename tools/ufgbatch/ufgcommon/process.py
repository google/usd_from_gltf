#!/usr/bin/python
#
# Copyright 2019 Google LLC
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Utility for processing multiple conversions."""

from __future__ import print_function

import multiprocessing
import os
import subprocess
import time

from . import util
from .util import join_path
from .util import status

SUMMARY_HEADER = """
------------------------------------
-- Task Summary"""

STATS_HEADER = """
------------------------------------
-- Message Statistics"""

TASK_SECTION_FORMAT = """
------------------------------------
-- %s"""


def get_process_count(recommended_count, process_max, num_tasks):
  process_count = multiprocessing.cpu_count()
  if recommended_count > 0:
    process_count = recommended_count
  process_max = min(process_max,
                    num_tasks) if (process_max > 0) else num_tasks
  return max(min(process_count, process_max), 1)


def run_conversion(csv_tasks, tasks, exe, exe_args, ext, in_path, out_path,
                   csv_names, process_count, log_path, result_path,
                   tag_tracker):
  """Runs the processes to convert files to the target format."""
  # Convert usda and/or usdz files.
  start_time = time.time()
  (complete_tasks, failed_tasks, logs,
   results) = run_tasks(tasks, exe, exe_args, ext, in_path, out_path,
                        len(csv_names), process_count)
  task_end_time = time.time()

  # Gather message tags.
  csv_tag_trackers = [None] * len(csv_names)
  for csv_index, csv_name in enumerate(csv_names):
    csv_tag_tracker = util.TagTracker.from_output(logs[csv_index])
    tag_tracker.add_from_tracker(csv_tag_tracker)
    csv_tag_trackers[csv_index] = csv_tag_tracker

  # Print summary.
  status('')
  status(SUMMARY_HEADER, util.LOG_COLOR_CYAN)
  task_delta_time = task_end_time - start_time
  tag_summary = tag_tracker.get_summary_suffix(True)
  status('Converted %s files in %.2fs. %s failed.%s' %
         (len(complete_tasks), task_delta_time, len(failed_tasks), tag_summary))

  # Write per-CSV output.
  for csv_index, csv_name in enumerate(csv_names):
    csv_tag_tracker = csv_tag_trackers[csv_index]
    csv_tag_stats = csv_tag_tracker.get_per_tag_stats(False)
    csv_task_count = len(csv_tasks[csv_index])
    csv_out_suffix = ('%s\n%s%s\nConverted %s models.%s\n') % (
        STATS_HEADER, csv_tag_stats, SUMMARY_HEADER, csv_task_count,
        csv_tag_tracker.get_summary_suffix(False))

    status('')
    line_header = util.colorize('%s.csv: ' % csv_name, util.LOG_COLOR_CYAN)
    status(
        '%sConverted %s models.%s' %
        (line_header, csv_task_count, csv_tag_tracker.get_summary_suffix(True)))

    if result_path:
      result_name = csv_name + '_result.txt'
      result_file_path = join_path(result_path, result_name)
      with open(result_file_path, 'w') as result_file:
        result_file.write(results[csv_index] + csv_out_suffix)
    else:
      status(results[csv_index] + csv_out_suffix)

    if log_path:
      log_name = csv_name + '_log.txt'
      log_file_path = os.path.abspath(join_path(log_path, log_name))
      status('  Writing log to: %s' % (log_path))
      with open(log_file_path, 'w') as log_file:
        log_file.write(logs[csv_index] + csv_out_suffix)
    else:
      status(logs[csv_index] + csv_out_suffix)

  status('')
  status(STATS_HEADER, util.LOG_COLOR_CYAN)
  tag_stats = tag_tracker.get_per_tag_stats(True)
  status(tag_stats)
  return (complete_tasks, failed_tasks, task_delta_time)


def get_process_command(cmd_args):
  """Get command-line text for task process."""
  # Join arguments into command string, escaping any with spaces.
  cmd = ''
  for arg_index, arg in enumerate(cmd_args):
    if arg_index > 0:
      cmd += ' '
    if ' ' in arg:
      cmd += '"' + arg + '"'
    else:
      cmd += arg
  return cmd


def wait_and_update_task(process, task, cmd_args):
  """Wait for task process to complete and update status."""
  if not process:
    return
  (cmd_stdout, cmd_stderr) = process.communicate()
  code = process.returncode
  if code == 0:
    task.success = True
  task.command = get_process_command(cmd_args)
  task.output = get_process_output(cmd_stdout, cmd_stderr)
  if (not task.success) and ('error' not in task.output.lower()):
    # Failure without error lines likely indicates the process crashed.
    task.output += '\nERROR: Exit code %s (0x%s).' % (code, util.uhex32(code))
    if (code & 0xffffffff) == 0xc0000005:  # Windows-specific error code.
      task.output += ' [Access Violation]'

  # Echo command and output.
  task_log = task.command
  task_log += util.colorize_process_output(task.format_output(None, None))
  print(task_log)


def wait_for_available_process(processes):
  """Wait for a process slot to become available and return its index."""
  process_avail = -1
  while True:
    for process_index, (process, task, cmd_args) in enumerate(processes):
      if (not process) or (process.poll() is not None):
        wait_and_update_task(process, task, cmd_args)
        processes[process_index] = (None, None, None)
        process_avail = process_index
        break
    if process_avail == -1:
      poll_interval_sec = 0.01
      time.sleep(poll_interval_sec)
    else:
      break
  return process_avail


def wait_for_all_processes(processes):
  """Wait for all executing processes to complete."""
  for (process, task, cmd_args) in processes:
    wait_and_update_task(process, task, cmd_args)


def run_tasks(tasks, exe, exe_args, ext, in_path, out_path, csv_count,
              process_count):
  """Run all usd_from_gltf tasks.

  Args:
    tasks: The set of conversion tasks (array of Task).
    exe: usd_from_gltf executable path.
    exe_args: Additional arguments passed to the executable.
    ext: Output file name extension (.usda, .usdz, or .usd-)
    in_path: glTF input root directory.
    out_path: Output USD root directory.
    csv_count: Number of CSV inputs.
    process_count: Max number of concurrent processes.

  Returns:
    complete_tasks: Tasks that completed successfully.
    failed_tasks: Failed tasks.
    logs: Per-CSV log file with command-lines and output.
    results: Per-CSV text file with task status and output, for diffs.
  """
  processes = [(None, None, None)] * process_count  # (process, task, cmd_args)

  # Create all directories up-front to work around spurious IO errors that will
  # occur if multiple processes try to create a directory simultaneously.
  if process_count > 1:
    for task in tasks:
      task_dir_path = join_path(out_path, task.dst)
      util.make_directories(task_dir_path)

  # Spawn process for each task, up to process_count in parallel.
  section = None
  for task in tasks:
    process_avail = wait_for_available_process(processes)
    if task.section != section:
      section = task.section
      status(TASK_SECTION_FORMAT % section, util.LOG_COLOR_CYAN)
    task_in_path = join_path(in_path, task.src)
    task_out_path = join_path(out_path, task.dst, task.name + ext)
    cmd_args = [exe, task_in_path, task_out_path]
    if exe_args:
      cmd_args += exe_args
    if task.args:
      cmd_args += task.args
    # Disable usage text on argument errors to reduce spam.
    args = cmd_args + ['--nousage']
    process = subprocess.Popen(
        args, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    processes[process_avail] = (process, task, cmd_args)

  # Wait for remaining processes to complete.
  wait_for_all_processes(processes)

  # Return the set of completed tasks.
  complete_tasks = []
  failed_tasks = []
  logs = [''] * csv_count
  results = [''] * csv_count
  section = None
  for task in tasks:
    if task.success:
      complete_tasks.append(task)
    else:
      failed_tasks.append(task)
    csv_index = task.csv_index
    if task.section != section:
      section = task.section
      logs[csv_index] += (TASK_SECTION_FORMAT % section) + '\n'
      results[csv_index] += (TASK_SECTION_FORMAT % section) + '\n'
    logs[csv_index] += task.get_log() + '\n'
    results[csv_index] += task.get_result(in_path, out_path) + '\n'
  return (complete_tasks, failed_tasks, logs, results)


def get_brief_task_listing(tasks):
  """Get a brief one-line listing of task names (truncated if necessary)."""
  count = min(len(tasks), 2)
  listing = ''
  for i in range(0, count):
    if listing:
      listing += ', '
    listing += tasks[i].name
  if len(tasks) > count:
    listing += ', ...(and %s more)' % (len(tasks) - count)
  return listing


def get_process_output(cmd_stdout, cmd_stderr):
  """Get task process log output."""
  text = ''
  if cmd_stdout:
    text += cmd_stdout.decode(errors='ignore').replace('\r', '')
  if cmd_stderr:
    text += cmd_stderr.decode(errors='ignore').replace('\r', '')
  return text.rstrip('\n\r')


def get_summary(failed_tasks, mismatch_total, missing_total, extra_total):
  """Get one-line summary of build&diff results."""
  is_diff = mismatch_total or missing_total or extra_total
  if not failed_tasks and not is_diff:
    return 'Conversion successful. No differences detected.'
  summary = ''
  if failed_tasks:
    summary += 'Failed converting %s model(s) (%s).' % (
        len(failed_tasks), get_brief_task_listing(failed_tasks))
  if is_diff:
    if summary:
      summary += ' '
    summary += ('Detected golden file differences. %s different, %s missing, %s'
                ' extra.') % (mismatch_total, missing_total, extra_total)
  return summary
