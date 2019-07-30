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

"""Task definitions for batch processing models from CSV lists."""

from __future__ import print_function

import csv
import os
import shlex

from . import util


class Task(object):
  """Contains info for each conversion task."""

  def __init__(self, name, src, dst, args, csv_index, section):
    self.name = name
    self.src = src
    self.dst = dst
    self.args = args
    self.csv_index = csv_index
    self.section = section
    # Task completion state.
    self.success = False
    self.command = None
    self.output = None

  def format_output(self, in_path, out_path):
    """Format task output for logging."""
    if not self.output:
      return ''
    output = self.output

    # Replace full paths to prevent differences based on build directories.
    if in_path:
      output = output.replace(in_path, '${IN}')
    if out_path:
      output = output.replace(out_path, '${OUT}')

    # Indent.
    output = ''.join(('    ' + line) for line in output.splitlines(True))
    return '\n' + output

  def get_log(self):
    """Get task log text (command plus output)."""
    return self.command + self.format_output(None, None)

  def get_result(self, in_path, out_path):
    """Get result text (status plus output), suitable for diff'ing."""
    prefix = 'Success' if self.success else 'FAILURE'
    result = prefix + ' [' + self.name + '] ' + self.src + ' --> ' + self.dst
    return result + self.format_output(in_path, out_path)


def parse_tasks(fp, csv_index):
  """Parse tasks from a CSV file."""
  tasks = []
  section = None
  section_args = []
  with fp as csv_file:
    csv_reader = csv.reader(csv_file, delimiter=',', skipinitialspace=True)
    for row in csv_reader:
      if not row:
        # Just skip empty rows - we use them for organization.
        continue
      if row[0].startswith('#'):
        # Skip leading line comments.
        continue
      if row[0].startswith('@'):
        # Record section header, with optional section-specific arguments.
        if len(row) != 1 and len(row) != 2:
          util.error(
              '{0}: Incorrect number of section columns ({1}, expected 1 or 2) on line {2}.'
              .format(fp.name, len(row), csv_reader.line_num))
          exit(1)
        section = row[0][1:]
        section_args = []
        if len(row) >= 2:
          section_args = shlex.split(row[1])
        continue
      if len(row) != 3 and len(row) != 4:
        util.error(
            '{0}: Incorrect number of columns ({1}, expected 3 or 4) on line {2}.'
            .format(fp.name, len(row), csv_reader.line_num))
        exit(1)
      task_args = section_args
      if len(row) >= 4:
        task_args = task_args + shlex.split(row[3])
      tasks.append(Task(row[0], row[1], row[2], task_args, csv_index, section))
  return tasks


def check_unique_task_names(tasks):
  """Returns true if task names are unique."""
  names = {}
  have_dupes = False
  for task in tasks:
    if task.name in names:
      names[task.name] += 1
      have_dupes = True
    else:
      names[task.name] = 1
  if have_dupes:
    dupes = ''
    for (name, count) in names.items():
      if count > 1:
        if dupes:
          dupes += ', '
        dupes += name
    util.error('ERROR: Duplicate task(s): ' + dupes)
  return not have_dupes


def get_csv_tasks(csv_files):
  """Parse tasks from CSV files and ensure tasks are uniquely named."""
  csv_count = len(csv_files)
  tasks = []
  csv_names = [None] * csv_count
  csv_tasks = [[]] * csv_count
  for csv_index, csv_file in enumerate(csv_files):
    csv_tasks[csv_index] = parse_tasks(csv_file, csv_index)
    tasks += csv_tasks[csv_index]
    (_, csv_file_name) = os.path.split(csv_file.name)
    (csv_names[csv_index], _) = os.path.splitext(csv_file_name)
  if not check_unique_task_names(tasks):
    return (None, None, None)
  return (tasks, csv_names, csv_tasks)
