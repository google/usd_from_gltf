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

"""Utility functions for differencing script output."""
# pylint: disable=g-bad-todo

from __future__ import print_function

import filecmp
import os
import platform
import subprocess
import zipfile

from . import util
from PIL import Image
from PIL import ImageChops
from .util import join_path
from .util import norm_relpath
from .util import status


def get_exe_path(name_path):
  exe_path = name_path
  if platform.system() == 'Windows':
    exe_path = name_path + '.exe'
  return exe_path if os.access(exe_path, os.X_OK) else None


def find_exe(name):
  """Find executable in path."""
  paths = os.environ['PATH'].split(os.pathsep)
  for path in paths:
    exe_path = get_exe_path(os.path.join(path, name))
    if exe_path:
      return exe_path
  return None


def get_default_diff_command():
  """Get the default command used to diff text files."""
  exe = find_exe('diff')
  if exe:
    return '"%s" --ignore-all-space "{0}" "{1}"' % exe
  exe = find_exe('git')
  if exe:
    return ('"%s" --no-pager diff --ignore-all-space --no-color "{0}" "{1}"' %
            exe)
  exe = find_exe('git-diff')
  if exe:
    return '"%s" --ignore-all-space --no-color "{0}" "{1}"' % exe
  return None


def histogram_channels(histogram):
  """Yields histogram's color channels in slices of size 256."""
  for i in range(0, len(histogram), 256):
    yield histogram[i:i + 256]


def max_histogram_bucket(histogram):
  """Returns max index of all non-zero buckets."""
  return max([i for i, diff in enumerate(histogram) if diff != 0])


def compare_images(golden, test, threshold=3):
  """Returns true if the images are equal within a per pixel threshold.

  Args:
    golden: the golden image file
    test: the test image file
    threshold: in range [0, 255]

  Returns:
    true if all pixel differences are less than or equal to threshold, else
    returns false
  """
  with Image.open(golden) as g_image, Image.open(test) as t_image:
    histogram = ImageChops.difference(g_image, t_image).histogram()
    # Finds the max index of non-zero buckets and compares with the threshold.
    #   - Bucket N contains count of all pixels that differ by N.
    max_buckets = [
        max_histogram_bucket(channel)
        for channel in histogram_channels(histogram)
    ]
    return max(max_buckets) <= threshold


def handle_crc_difference(golden_zip, golden_info, test_zip, test_info):
  """Returns results of more relaxed comparisons for the files."""
  try:
    with golden_zip.open(golden_info) as golden, test_zip.open(
        test_info) as test:
      return compare_images(golden, test)
  except IOError:
    # Non-images always return false.
    return False


def compare_usdz_content(golden_path, test_path):
  """Returns true if usdz content matches."""
  with zipfile.ZipFile(golden_path) as golden_zip:
    with zipfile.ZipFile(test_path) as test_zip:
      golden_infos = golden_zip.infolist()
      test_infos = test_zip.infolist()
      if len(golden_infos) != len(test_infos):
        return False
      for i, test_info in enumerate(test_infos):
        golden_info = golden_infos[i]
        if test_info.filename != golden_info.filename:
          return False
        if test_info.file_size != golden_info.file_size:
          return False
        if test_info.CRC != golden_info.CRC:
          return handle_crc_difference(golden_zip, golden_info, test_zip,
                                       test_info)
      return True
  return False


def get_diffs(gold_base, test_base, gold_root):
  """Diff test output with the golden directory.

  Args:
    gold_base: Golden (known-good) directory path.
    test_base: Test output directory path.
    gold_root: Returned paths are relative to this path.

  Returns:
    matches:    Matching files.
    mismatches: Different files.
    missings:   Files in golden root, missing in test_base.
    extras:     Files not in golden root, extra in test_base.
  """
  # Walk the golden tree testing for differences with the test tree.
  all_matches = []
  all_mismatches = []
  all_missings = []
  for gold_dir, _, file_list in os.walk(gold_base):
    if not file_list:
      continue
    test_dir = join_path(test_base, norm_relpath(gold_dir, gold_base))
    (matches, mismatches, errors) = filecmp.cmpfiles(
        gold_dir, test_dir, file_list, shallow=False)
    rel_dir = norm_relpath(gold_dir, gold_root)
    for name in matches:
      all_matches.append(join_path(rel_dir, name))
    for name in mismatches:
      rel_file_path = join_path(rel_dir, name)
      gold_path = join_path(gold_dir, name)
      test_path = join_path(test_dir, name)
      (_, ext) = os.path.splitext(name)
      # TODO: USDC (and there by extension USDZ) output may not be
      # deterministic, so we should use the usddiff tool in the USD library to
      # perform deep compares when there's a binary mismatch.
      if ext != '.usdz' or not compare_usdz_content(gold_path, test_path):
        all_mismatches.append(rel_file_path)
    for name in errors:
      all_missings.append(join_path(rel_dir, name))

  # Walk the test tree to find new files not in the golden tree.
  all_extras = []
  for test_dir, _, file_list in os.walk(test_base):
    if not file_list:
      continue
    gold_dir = join_path(gold_base, norm_relpath(test_dir, test_base))
    (_, _, errors) = filecmp.cmpfiles(gold_dir, test_dir, file_list)
    rel_dir = norm_relpath(gold_dir, gold_root)
    for name in errors:
      all_extras.append(join_path(rel_dir, name))

  return (all_matches, all_mismatches, all_missings, all_extras)


def format_bytes(b):
  """Format bytes as human-readable text."""
  kb = 1024
  mb = kb*1024
  gb = mb*1024
  if b < kb:
    return '%s b' % b
  elif b < mb:
    return '{0:.2f} kb'.format(float(b) / kb)
  elif b < gb:
    return '{0:.2f} mb'.format(float(b) / mb)
  else:
    return '{0:.2f} gb'.format(float(b) / gb)


def get_file_size_text(path):
  """Get file size as human-readable text."""
  b = os.path.getsize(path)
  return format_bytes(b)


def colorized_diff_count(fmt, count):
  return util.colorize(
      fmt % count,
      util.LOG_COLOR_MAGENTA if count > 0 else util.LOG_COLOR_DEFAULT)


def get_summary_suffix(same_count, diff_count, missing_count, extra_count):
  return ' %s same, %s, %s, %s.' % (
      same_count, colorized_diff_count('%s different', diff_count),
      colorized_diff_count('%s missing', missing_count),
      colorized_diff_count('%s extra', extra_count))


def diff_with_golden(golden_path, test_path, out_path, tasks, result_rel_path,
                     diff_command, diffs_out):
  """Diff generated test files vs golden files.

  This prints the list of different and missing files, and for usda also writes
  line diffs to a file.

  Args:
    golden_path: Golden (known-correct) directory path.
    test_path: Built test data directory path.
    out_path: Output base directory path.
    tasks: Tasks used to filter comparison.
    result_rel_path: Relative path of the *_result.txt file.
    diff_command: Command format string to diff 2 text files.
    diffs_out: Output diffs text file name.

  Returns:
    match_count:    Number of identical files.
    mismatch_count: Number of different files.
    missing_count:  Number of golden files missing in output.
    extra_count:    Number of output files not in golden set.
  """
  match_total = 0
  mismatches = []
  missings = []
  extras = []
  for task in tasks:
    task_gold_path = join_path(golden_path, task.dst)
    task_test_path = join_path(test_path, task.dst)
    (task_matches, task_mismatches, task_missings,
     task_extras) = get_diffs(task_gold_path, task_test_path, golden_path)
    match_total += len(task_matches)
    mismatches += task_mismatches
    missings += task_missings
    extras += task_extras

  # Also diff the *_result.txt file.
  result_gold_path = join_path(golden_path, result_rel_path)
  result_test_path = join_path(test_path, result_rel_path)
  if not os.path.exists(result_test_path):
    missings.append(result_rel_path)
  elif not os.path.exists(result_gold_path):
    extras.append(result_rel_path)
  elif filecmp.cmp(result_gold_path, result_test_path, shallow=False):
    match_total += 1
  else:
    mismatches.append(result_rel_path)

  # Print missing and extra files.
  for rel_file_path in missings:
    status('  Missing:   %s' % (rel_file_path))
  for rel_file_path in extras:
    status('  Extra:     %s' % (rel_file_path))

  if not diff_command:
    diff_command = get_default_diff_command()

  # Print different files, and generate line-diff text.
  all_diffs = ''
  for rel_file_path in mismatches:
    golden_file_path = join_path(golden_path, rel_file_path)
    test_file_path = join_path(test_path, rel_file_path)
    status('  Different: %s (%s -> %s)' %
           (rel_file_path, get_file_size_text(golden_file_path),
            get_file_size_text(test_file_path)))
    (_, ext) = os.path.splitext(rel_file_path)
    if diff_command and (ext == '.usda' or ext == '.txt'):
      cmd = diff_command.format(golden_file_path, test_file_path)
      try:
        diff = subprocess.check_output(
            cmd, stderr=subprocess.STDOUT, shell=True, universal_newlines=True)
      except subprocess.CalledProcessError as e:
        diff = e.output
      all_diffs += cmd + '\n' + diff.replace('\r', '\n') + '\n'
      # The 'diff' command resets the console mode for some reason.
      util.enable_ansi_colors()

  # Print summary and write line-diff text file.
  diff_total = len(mismatches) + len(missings) + len(extras)
  if not diff_total:
    status('Diff Passed: %s same' % match_total)
  else:
    status(
        util.colorize('Diff FAILED:', util.LOG_COLOR_MAGENTA) +
        get_summary_suffix(match_total, len(mismatches), len(missings),
                           len(extras)))
    if all_diffs:
      diffs_path = join_path(out_path, diffs_out)
      status('  Writing line diffs to: %s' % os.path.abspath(diffs_path))
      with open(diffs_path, 'w') as diffs_file:
        diffs_file.write(all_diffs)
  return (match_total, len(mismatches), len(missings), len(extras))


def compare_directories(golden_path, test_path):
  """Returns true if the test directory matches the golden directory."""
  (_, mismatches, missings, extras) = get_diffs(golden_path, test_path,
                                                golden_path)
  return not (mismatches or missings or extras)


def get_changed_tasks(tasks, golden_path, test_path):
  """Get the set of tasks that have different test output than the golden."""
  changed_tasks = []
  for task in tasks:
    golden_dst_path = join_path(golden_path, task.dst)
    test_dst_path = join_path(test_path, task.dst)
    if not compare_directories(golden_dst_path, test_dst_path):
      changed_tasks.append(task)
  return changed_tasks
