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

"""Run batch usd_from_gltf conversion.

This generates USD files from glTF according to a CSV listing.

Usage: ufgbatch.py <csv>
  * csv: Path to CSV file containing conversion tasks, of the form:
    name, my/source/path.gltf, my/destination/usd/directory

Typical usages:
  ufgbatch.py all.csv
  ufgbatch.py all.csv -t usda
  ufgbatch.py all.csv -t usdz
"""

from __future__ import print_function

import argparse
import os
import sys

# Add script directory to path so the interpreter can locate ufgcommon.
sys.path.append(os.path.dirname(__file__))

from ufgcommon import util  # pylint: disable=g-import-not-at-top
from ufgcommon.process import get_process_count
from ufgcommon.process import run_conversion
from ufgcommon.task import get_csv_tasks
from ufgcommon.util import clean_output_directory
from ufgcommon.util import status


def main():
  util.enable_ansi_colors()
  (args, unknown_args) = parse_args()
  out_path = util.norm_abspath(args.out_dir)
  if args.clean:
    clean_output_directory(out_path)
    return 0

  # Parse CSVs for tasks.
  (tasks, csv_names, csv_tasks) = get_csv_tasks(args.csv)
  if not tasks:
    return 1

  usd_type = args.type

  # Choose the number of concurrent conversion processes.
  process_count = get_process_count(args.processes, args.process_max,
                                    len(tasks))

  type_text = usd_type if usd_type else 'usda+usdz'
  status('Converting %s models to %s with %s processes.' %
         (len(tasks), type_text, process_count))

  failed_tasks = []

  in_path = util.norm_abspath(args.in_dir)

  # Output .usda, .usdz, or .usd- for both.
  ext = ('.' + usd_type) if usd_type else '.usd-'

  exe_args = unknown_args + util.split_args(args.args)
  tag_tracker = util.TagTracker()
  (_, failed_tasks, _) = run_conversion(csv_tasks, tasks, args.exe, exe_args,
                                        ext, in_path, out_path, csv_names,
                                        process_count, '', '', tag_tracker)
  if failed_tasks:
    return 1
  else:
    return 0


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
        '-t',
        '--type',
        type=str,
        default=None,
        help='Output file type (usda or usdz). Defaults to both.')
    parser.add_argument(
        '-c',
        '--clean',
        default=False,
        action='store_true',
        help='Clean the output by moving it to a backup directory.')
    parser.add_argument(
        '--exe',
        type=str,
        default='usd_from_gltf',
        help='usd_from_gltf executable path.')
    parser.add_argument(
        '-a',
        '--args',
        type=str,
        default='',
        help='Additional args passed to usd_from_gltf.')
    parser.add_argument(
        '--in_dir',
        '-i',
        type=str,
        default='',
        help='Input glTF directory.')
    parser.add_argument(
        '--out_dir',
        '-o',
        type=str,
        default='',
        help='Output USD directory.')
    parser.add_argument(
        '--processes',
        type=int,
        default=0,
        help='Number of conversion processes. 0 uses all available cores.')
    parser.add_argument(
        '--process_max',
        type=int,
        default=64,
        help='Maximum number of processes to use. Set to 0 for unlimited.')
    return parser.parse_known_args()
  except argparse.ArgumentError:
    exit(1)


if __name__ == '__main__':
  exit_code = main()
  sys.exit(exit_code)
