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

"""Run batch validation and diff results.

This runs the validator on lists of models specified by CSV files, and logs
differences against a known-good 'golden' directory.

Usage: ufgvalidate.py <csv>
  * csv: Path to CSV file containing conversion tasks, of the form:
    name, my/source/path.gltf, my/destination/usd/directory
"""
# pylint: disable=g-bad-todo

from __future__ import print_function

import argparse
import os
import platform
import subprocess
import sys
import time

# Add script directory to path so the interpreter can locate ufgcommon.
sys.path.append(os.path.dirname(__file__))

from ufgcommon import diff  # pylint: disable=g-import-not-at-top
from ufgcommon import util
from ufgcommon.task import get_csv_tasks
from ufgcommon.util import colorize
from ufgcommon.util import status

# Maximum number of files to validate per call to gltf_validator. On Windows,
# this needs to be fairly low to ensure we don't overrun the 8191 character
# limit, but it's also useful in general to limit commands to a reasonable
# length.
MAX_FILES_PER_COMMAND = 32

SUMMARY_HEADER = """
------------------------------------
-- Task Summary"""

STATS_HEADER = """
------------------------------------
-- Message Statistics"""

TASK_SECTION_FORMAT = """
------------------------------------
-- %s: %s"""


def main():
  util.enable_ansi_colors()
  (args, _) = parse_args()
  out_path = util.norm_abspath(args.out_dir)

  # Parse CSVs for tasks.
  (tasks, csv_names, csv_tasks) = get_csv_tasks(args.csv)
  if not tasks:
    return 1

  status('Validating %s models.' % len(tasks))

  in_path = util.norm_abspath(args.in_dir)
  golden_path = util.norm_abspath(args.golden_dir)
  test_path = util.join_path(out_path, 'test')

  # Convert usda and/or usdz files.
  start_time = time.time()
  exe = args.exe
  logs = run_tasks(tasks, exe, in_path, csv_names)
  task_end_time = time.time()

  csv_count = len(csv_names)
  tag_tracker = util.TagTracker()
  csv_tag_trackers = [None] * csv_count
  for csv_index, log_text in enumerate(logs):
    csv_tag_tracker = util.TagTracker.from_output(log_text)
    tag_tracker.add_from_tracker(csv_tag_tracker)
    csv_tag_trackers[csv_index] = csv_tag_tracker

  status(STATS_HEADER, util.LOG_COLOR_CYAN)
  tag_stats = tag_tracker.get_per_tag_stats(True)
  status(tag_stats)

  status(SUMMARY_HEADER, util.LOG_COLOR_CYAN)
  task_delta_time = task_end_time - start_time
  tag_summary = tag_tracker.get_summary_suffix(True)
  status('Validated %s models in %.2fs.%s' %
         (len(tasks), task_delta_time, tag_summary))

  util.make_directories(out_path)
  util.make_directories(test_path)

  mismatch_total = 0
  missing_total = 0
  extra_total = 0

  # Write per-CSV output.
  for csv_index, csv_name in enumerate(csv_names):
    status('')
    log_text = logs[csv_index]
    csv_task_count = len(csv_tasks[csv_index])
    (mismatch_count, missing_count, extra_count) = generate_csv_results(
        in_path, out_path, golden_path, test_path, csv_name, log_text,
        csv_task_count, csv_tag_trackers[csv_index], args.nodiff,
        args.diff_command)
    mismatch_total += mismatch_count
    missing_total += missing_count
    extra_total += extra_count

  error_total = tag_tracker.totals[util.SEVERITY_ERROR]
  if args.summary_out:
    with open(args.summary_out, 'w') as summary_file:
      summary_file.write(
          get_summary(error_total, mismatch_total, missing_total, extra_total))

  # Return exit code for use by unit tests.
  err = 0
  if error_total:
    err |= 1
  if mismatch_total or missing_total or extra_total:
    err |= 2
  return err


def parse_args():
  """Parse command-line arguments."""
  try:
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument(
        'csv',
        nargs='+',
        type=argparse.FileType('r'),
        help='Task CSV file path.')
    parser.add_argument(
        '--exe',
        type=str,
        default='gltf_validator',
        help='gltf_validator executable path.')
    parser.add_argument(
        '--in_dir',
        '-i',
        type=str,
        default='gltf',
        help='Input glTF directory.')
    parser.add_argument(
        '--out_dir',
        '-o',
        type=str,
        default='validation/out',
        help='Output USD directory.')
    parser.add_argument(
        '--golden_dir',
        '-g',
        type=str,
        default='validation/golden',
        help='Golden directory for diffs.')
    parser.add_argument(
        '--nodiff',
        default=False,
        action='store_true',
        help='Disable diff step.')
    parser.add_argument(
        '--diff_command',
        type=str,
        default=None,
        help='Formatted command to perform line diffs.')
    parser.add_argument(
        '--summary_out',
        type=str,
        default=None,
        help='Path to file that receives one-line build summary.')
    return parser.parse_known_args()
  except argparse.ArgumentError:
    exit(1)


def get_process_command(cmd_args):
  """Get command-line text for task process."""
  # Join arguments into command string, escaping any with spaces.
  continuance = '^' if (platform.system() == 'Windows') else '\\'
  separator = ' ' + continuance + '\n  '
  cmd = ''
  for arg_index, arg in enumerate(cmd_args):
    if arg_index > 0:
      cmd += separator
    if ' ' in arg:
      cmd += '"' + arg + '"'
    else:
      cmd += arg
  return cmd


def get_process_output(cmd_stdout, cmd_stderr):
  """Get task process log output."""
  text = ''
  if cmd_stdout:
    text += cmd_stdout.decode().replace('\r', '')
  if cmd_stderr:
    text += cmd_stderr.decode().replace('\r', '')
  return text.rstrip('\n\r')


def format_process_output(output, in_path):
  """Format process output for logging."""
  # Replace full paths to prevent differences based on build directories.
  if in_path:
    output = output.replace(in_path, '${IN}')
  return output


def run_command(cmd_args):
  """Run validator command-line."""
  # Spawn the command.
  command = get_process_command(cmd_args)
  status(command, util.LOG_COLOR_DARK_CYAN)

  # TODO: This doesn't interleave stdout and stderr in the correct
  # order, and there appears to be no reasonable way to do that in Python. As a
  # work-around, we're forcing the exe to write all messages to stdout.
  code = 0
  try:
    output = subprocess.check_output(
        cmd_args, stderr=subprocess.STDOUT, universal_newlines=True)
  except subprocess.CalledProcessError as e:
    output = e.output
    code = e.returncode

  output = output.replace('\r', '')

  if code != 0:
    # Failure without error lines likely indicates the process crashed.
    if output:
      output += '\n'
    output += 'ERROR: Exit code %s (0x%s).' % (code, util.uhex32(code))
    if (code & 0xffffffff) == 0xc0000005:  # Windows-specific error code.
      output += ' [Access Violation]'

  print(util.colorize_process_output(output))
  return output


def run_tasks(tasks, exe, in_path, csv_names):
  """Run validator command-line on all tasks."""
  csv_count = len(csv_names)
  logs = [''] * csv_count

  # Group tasks by unique CSV index and section.
  groups = []
  csv_index = -1
  section = None
  for task in tasks:
    if task.csv_index != csv_index or task.section != section:
      csv_index = task.csv_index
      section = task.section
      groups.append((csv_index, section, []))
    groups[-1][2].append(task)

  for (csv_index, section, group_tasks) in groups:
    # Run validation command on all tasks in the group, splitting up the command
    # to limit the number of parameters for each run.
    status(TASK_SECTION_FORMAT % (csv_names[csv_index], section),
           util.LOG_COLOR_CYAN)
    cmd_args = [exe]
    for task in group_tasks:
      if len(cmd_args) >= MAX_FILES_PER_COMMAND:
        logs[csv_index] += run_command(cmd_args)
        cmd_args = [exe]
      cmd_args.append(util.join_path(in_path, task.src))
    if len(cmd_args) > 1:
      logs[csv_index] += run_command(cmd_args)

  return logs


def generate_csv_results(in_path, out_path, golden_path, test_path, csv_name,
                         log_text, task_count, tag_tracker, nodiff,
                         diff_command):
  """Write per-CSV result text and diff."""
  tag_stats = tag_tracker.get_per_tag_stats(False)
  out_text = ('%s%s\n%s%s\nValidated %s models.%s\n') % (
      log_text, STATS_HEADER, tag_stats, SUMMARY_HEADER, task_count,
      tag_tracker.get_summary_suffix(False))

  # Write the result file to the test output directory so it's included in
  # diffs.
  result_name = csv_name + '_result.txt'
  result_path = util.join_path(test_path, result_name)
  with open(result_path, 'w') as result_file:
    result = out_text.replace(in_path, '${IN}')
    result_file.write(result)

  # Write the log file to the base output directory so it's not diff'ed (we
  # don't want it diff'ed because it contains full paths and can vary based
  # on input/output directories).
  log_name = csv_name + '_log.txt'
  log_path = os.path.abspath(util.join_path(out_path, log_name))
  line_header = colorize('%s.csv: ' % csv_name, util.LOG_COLOR_CYAN)
  status('%sValidated %s models.%s' %
         (line_header, task_count, tag_tracker.get_summary_suffix(True)))
  status('  Writing log to: %s' % (log_path))
  with open(log_path, 'w') as log_file:
    log_file.write(out_text)

  # Diff vs golden files.
  mismatch_count = 0
  missing_count = 0
  extra_count = 0
  if not nodiff:
    status('%sDiffing vs golden.' % (line_header))
    diffs_name = csv_name + '_diffs.txt'
    (_, mismatch_count, missing_count,
     extra_count) = diff.diff_with_golden(golden_path, test_path, out_path, [],
                                          result_name, diff_command, diffs_name)

  return (mismatch_count, missing_count, extra_count)


def get_summary(error_total, mismatch_total, missing_total, extra_total):
  """Get one-line summary of validation results."""
  is_diff = mismatch_total or missing_total or extra_total
  if not error_total and not is_diff:
    return 'Validation successful. No differences detected.'
  summary = ''
  if error_total:
    summary += '%s errors validating model(s).' % (error_total)
  if is_diff:
    if summary:
      summary += ' '
    summary += ('Detected golden file differences. %s different, %s missing, %s'
                ' extra.') % (mismatch_total, missing_total, extra_total)
  return summary


if __name__ == '__main__':
  exit_code = main()
  sys.exit(exit_code)
