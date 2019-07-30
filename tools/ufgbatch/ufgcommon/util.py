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

"""Common utilities used for batch model processing."""

from __future__ import print_function

import os
import platform
import re
import shlex
import shutil
import time

LOG_COLOR_DEFAULT = None
LOG_COLOR_RED = '1;31'
LOG_COLOR_GREEN = '1;32'
LOG_COLOR_YELLOW = '1;33'
LOG_COLOR_MAGENTA = '1;35'
LOG_COLOR_CYAN = '1;36'
LOG_COLOR_DARK_CYAN = '36'

SEVERITY_UNKNOWN = -1
SEVERITY_INFO = 0
SEVERITY_WARN = 1
SEVERITY_ERROR = 2
SEVERITY_COUNT = 3

SEVERITY_COLORS = [LOG_COLOR_DEFAULT, LOG_COLOR_YELLOW, LOG_COLOR_RED]
SEVERITY_STATS_HEADERS = ['Info:', 'Warnings:', 'ERRORS:']

TAG_REGEX = re.compile(r'\[[A-Z0-9]+_(INFO|WARN|ERROR)_[A-Z0-9_]+\]\s*$')


def colorize(text, color):
  """Add escape sequences to colorize text."""
  if color == LOG_COLOR_DEFAULT:
    return text
  else:
    # Colorize with ANSI escape codes: '\033[<style>;<fg-color>;<bg-color>m'.
    # See: https://en.wikipedia.org/wiki/ANSI_escape_code
    return '\033[%sm%s\033[0m' % (color, text)


def enable_ansi_colors():
  """Enable ANSI text colorization."""
  # On Windows this is done via SetConsoleMode, but rather than go through the
  # hassle of importing the function from a system DLL, just call the 'color'
  # console command to do it for us.
  if platform.system() == 'Windows':
    os.system('color')


def log(msg, color=LOG_COLOR_DEFAULT):
  """Log a colorized message."""
  print(colorize(msg, color))


def status(msg, color=LOG_COLOR_DEFAULT):
  """Log a status message."""
  log(msg, color)


def warn(msg):
  """Log an warning."""
  log(msg, LOG_COLOR_YELLOW)


def error(msg):
  """Log an error."""
  log(msg, LOG_COLOR_RED)


def split_args(arg_array):
  """Split array of compound command-line arguments into individual ones."""
  args = []
  for arg in arg_array:
    args += shlex.split(arg)
  return args


def norm_slashes(path):
  """Normalize path to use forward-slashes."""
  return path.replace('\\', '/')


def norm_abspath(path):
  """Get a normalized absolute path, using forward-slashes."""
  return norm_slashes(os.path.normpath(os.path.abspath(path)))


def norm_relpath(full_path, base_path):
  """Get a full_path relative to base_path, using forward-slashes."""
  return norm_slashes(os.path.relpath(full_path, base_path))


def join_path(path0, *pathv):
  """Join paths with forward-slashes."""
  path = path0
  for p in pathv:
    if not path.endswith('/'):
      path += '/'
    path += p
  return path


def uhex32(value):
  """Convert integer to 32-bit unsigned hex text."""
  # Convert to 8 hex characters, padded with leading zeros if necessary.
  return '%s' % ('00000000%X' % (value & 0xffffffff))[-8:]


def make_directories(path):
  """Like makedirs, but won't fail if the directory already exists."""
  if not os.path.exists(path):
    os.makedirs(path)


def copy_file(src_path, dst_path):
  try:
    shutil.copyfile(src_path, dst_path)
  except IOError:
    error('Failed copying file: "%s" --> "%s"' % (src_path, dst_path))


def copy_files_with_extensions(src_dir, dst_dir, exts):
  """Recursively copy all files with the given extension."""
  for dir_path, _, files in os.walk(src_dir):
    rel_dir = norm_relpath(dir_path, src_dir)
    for file_name in files:
      (_, file_ext) = os.path.splitext(file_name)
      if file_ext.lower() in exts:
        src_path = join_path(src_dir, rel_dir, file_name)
        dst_path = join_path(dst_dir, rel_dir, file_name)
        copy_file(src_path, dst_path)


def delete_files_with_extension(path, exts):
  """Recursively delete all files with the given extension."""
  del_count = 0
  for dir_path, subdirs, file_list in os.walk(path, topdown=False):
    for file_name in file_list:
      (_, file_ext) = os.path.splitext(file_name)
      if file_ext.lower() in exts:
        file_path = join_path(dir_path, file_name)
        os.remove(file_path)
        del_count += 1
    # Also remove any empty directories.
    for subdir_name in subdirs:
      subdir_path = join_path(dir_path, subdir_name)
      if not os.listdir(subdir_path):
        os.rmdir(subdir_path)
  return del_count


def add_in_dictionary(d, key, value):
  """Add to a number in a dictionary, creating the entry if necessary."""
  if key in d:
    d[key] += value
  else:
    d[key] = value


def get_message_tag(line):
  """Extract message tag from a log output line."""
  found = TAG_REGEX.search(line)
  if found:
    # Determine severity from the tag.
    tag = found.group(0)
    code = tag[1:len(tag)-1]
    severity = found.group(1)
    if severity == 'ERROR':
      return (SEVERITY_ERROR, code)
    elif severity == 'WARN':
      return (SEVERITY_WARN, code)
    else:
      return (SEVERITY_INFO, code)
  else:
    # No tag. Determine severity from text.
    line_lower = line.lower()
    if 'error:' in line_lower:
      return (SEVERITY_ERROR, None)
    elif 'warning:' in line_lower:
      return (SEVERITY_WARN, None)
    else:
      return (SEVERITY_UNKNOWN, None)


def colorize_process_output(output):
  """Colorize errors and warnings from command-line process output."""
  colorized = ''
  for line in output.splitlines(True):
    (severity, _) = get_message_tag(line)
    color = LOG_COLOR_DEFAULT
    if severity == SEVERITY_ERROR:
      color = LOG_COLOR_RED
    elif severity == SEVERITY_WARN:
      color = LOG_COLOR_YELLOW
    colorized += colorize(line, color)
  return colorized


def rename_with_retry(from_path, to_path, retry_limit=20):
  """Rename a directory, with retry."""
  for retry in range(retry_limit):
    try:
      if retry > 0:
        retry_sleep_sec = 0.1
        time.sleep(retry_sleep_sec)
        warn('Retrying failed rename: "%s" --> "%s"' % (from_path, to_path))
      os.rename(from_path, to_path)
      return
    except IOError:
      continue
  error('\nRename failed after %s retries: "%s" --> "%s"' %
        (retry_limit, from_path, to_path))


def clean_output_directory(out_path):
  """Clean output directory by renaming it."""
  status('Cleaning "%s"' % out_path)
  if not os.path.exists(out_path):
    status('Already clean.')
    return
  bak_path_format = out_path + '_bak%d'
  i = 0
  while os.path.exists(bak_path_format % i):
    i += 1
  bak_path = bak_path_format % i
  status('Moving "%s" --> "%s"' % (out_path, bak_path))
  rename_with_retry(out_path, bak_path)


class TagTracker(object):
  """Keeps track of message totals, globally and per-tag."""

  def __init__(self):
    self.totals = [0] * SEVERITY_COUNT
    self.tag_totals = [None] * SEVERITY_COUNT
    for severity in range(SEVERITY_COUNT):
      self.tag_totals[severity] = {}

  def add_from_tracker(self, tracker):
    """Add totals from another tracker."""
    for severity in range(SEVERITY_COUNT):
      self.totals[severity] += tracker.totals[severity]
      for tag, plus_total in tracker.tag_totals[severity].items():
        add_in_dictionary(self.tag_totals[severity], tag, plus_total)

  def add_from_output(self, output):
    """Add totals from tags in log output text."""
    for line in output.splitlines():
      (severity, tag) = get_message_tag(line)
      if tag:
        add_in_dictionary(self.tag_totals[severity], tag, 1)
      if severity != SEVERITY_UNKNOWN:
        self.totals[severity] += 1

  def _get_total_suffix(self, colored, name, severity, color):
    total = self.totals[severity]
    if not total:
      return ''
    plurality = '' if total == 1 else 's'
    text = ' %s %s%s.' % (total, name, plurality)
    if colored:
      text = colorize(text, color)
    return text

  def get_summary_suffix(self, colored):
    """Get one-line error/warning total summary suffix."""
    summary = ''
    summary += self._get_total_suffix(
        colored, 'error', SEVERITY_ERROR, LOG_COLOR_RED)
    summary += self._get_total_suffix(
        colored, 'warning', SEVERITY_WARN, LOG_COLOR_YELLOW)
    return summary

  def get_per_tag_stats(self, colored):
    """Get table of per-tag stats."""
    stats = ''
    for severity in range(SEVERITY_COUNT):
      totals = self.tag_totals[severity]
      if not totals:
        continue
      color = (SEVERITY_COLORS[severity] if colored else LOG_COLOR_DEFAULT)
      header = SEVERITY_STATS_HEADERS[severity]
      stats += colorize(header, color) + '\n'
      for tag in sorted(totals):
        stats += colorize('  %s(%s)\n' % (tag, totals[tag]), color)
    return stats

  @staticmethod
  def from_output(output):
    """Construct from tags in log output text."""
    tracker = TagTracker()
    tracker.add_from_output(output)
    return tracker
