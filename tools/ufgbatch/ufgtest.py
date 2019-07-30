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

"""Run batch usd_from_gltf tests, diff, and deploy.

This generates USD files from glTF according to a CSV listing and logs
differences against a known-good 'golden' directory. It then generates an HTML
file and copies USDZ files to a deployment directory browseable on iOS.

Usage: ufgtest.py <csv>
  * csv: Path to CSV file containing conversion tasks, of the form:
    name, my/source/path.gltf, my/destination/usd/directory

Typical usages:
  ufgtest.py all.csv
  ufgtest.py all.csv -t usda
  ufgtest.py all.csv -t usdz
  ufgtest.py all.csv --redeploy --deploy_all
"""

from __future__ import print_function

import argparse
import os
import sys
import time

# Add script directory to path so the interpreter can locate ufgcommon.
sys.path.append(os.path.dirname(__file__))

from ufgcommon import deploy  # pylint: disable=g-import-not-at-top
from ufgcommon import diff
from ufgcommon import util
from ufgcommon.process import get_process_count
from ufgcommon.process import run_conversion
from ufgcommon.task import get_csv_tasks
from ufgcommon.util import clean_output_directory
from ufgcommon.util import join_path
from ufgcommon.util import status

DIFF_SUMMARY_HEADER = """
------------------------------------
-- Task+Diff Summary"""


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

  complete_tasks = []
  failed_tasks = []

  in_path = util.norm_abspath(args.in_dir)
  golden_path = util.norm_abspath(args.golden_dir)
  test_path = join_path(out_path, 'test')

  # Output .usda, .usdz, or .usd- for both.
  ext = ('.' + usd_type) if usd_type else '.usd-'

  match_total = 0
  mismatch_total = 0
  missing_total = 0
  extra_total = 0
  if args.redeploy:
    # We only deploy USDZ files.
    usd_type = 'usdz'
    complete_tasks = tasks
  else:
    exe_args = unknown_args + util.split_args(args.args)
    # Write the log file to the base output directory so it's not diff'ed (we
    # don't want it diff'ed because it contains full paths and can vary based
    # on input/output directories).
    log_path = out_path
    # Write the result file to the test output directory so it's included in
    # diffs.
    result_path = test_path
    tag_tracker = util.TagTracker()
    (complete_tasks, failed_tasks, task_delta_time) = run_conversion(
        csv_tasks, tasks, args.exe, exe_args, ext, in_path, test_path,
        csv_names, process_count, log_path, result_path, tag_tracker)

    # Do per CSV diffs.
    if not args.nodiff:
      diff_start_time = time.time()
      for csv_index, csv_name in enumerate(csv_names):
        # Diff vs golden files.
        line_header = util.colorize('%s.csv: ' % csv_name, util.LOG_COLOR_CYAN)
        status('%sDiffing vs golden.' % (line_header))
        diffs_name = csv_name + '_diffs.txt'
        result_name = csv_name + '_result.txt'
        (match_count, mismatch_count, missing_count,
         extra_count) = diff.diff_with_golden(golden_path, test_path, out_path,
                                              csv_tasks[csv_index], result_name,
                                              args.diff_command, diffs_name)
        match_total += match_count
        mismatch_total += mismatch_count
        missing_total += missing_count
        extra_total += extra_count
      diff_end_time = time.time()
      status(DIFF_SUMMARY_HEADER, util.LOG_COLOR_CYAN)
      diff_delta_time = diff_end_time - diff_start_time
      tag_summary = tag_tracker.get_summary_suffix(True)
      status('Converted %s models in %.2fs. %s failed.%s' %
             (len(complete_tasks), task_delta_time, len(failed_tasks),
              tag_summary))
      diff_summary = diff.get_summary_suffix(match_total, mismatch_total,
                                             missing_total, extra_total)
      status('Diffed in %.2fs.%s' % (diff_delta_time, diff_summary))

  # Deploy USDZ files and index.html to local directory for upload.
  if not args.nodeploy and (not usd_type or usd_type == 'usdz'):
    changed_tasks = []
    if not args.nodiff:
      changed_tasks = diff.get_changed_tasks(complete_tasks, golden_path,
                                             test_path)
    if args.deploy_all:
      deploy_tasks = tasks
      complete_tasks = tasks
    else:
      deploy_tasks = changed_tasks
    deploy_path = util.norm_abspath(args.deploy_dir)
    deploy.copy_to_deploy_dir(deploy_tasks, in_path, test_path, deploy_path,
                              args.deploy_gltf)
    html = deploy.generate_html(tasks, changed_tasks, args.deploy_gltf)
    html_path = join_path(deploy_path, 'index.html')
    with open(html_path, 'w') as html_file:
      html_file.write(html)
    deploy.copy_html_resources(deploy_path)

  if args.summary_out:
    with open(args.summary_out, 'w') as summary_file:
      summary_file.write(
          get_summary(failed_tasks, mismatch_total, missing_total, extra_total))

  # Return exit code for use by unit tests.
  err = 0
  if failed_tasks:
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
        '--golden_dir',
        '-g',
        type=str,
        default='golden',
        help='Golden directory for diffs.')
    parser.add_argument(
        '--deploy_dir',
        type=str,
        default='deploy',
        help='Local directory for deploying website content.')
    parser.add_argument(
        '--deploy_all',
        default=False,
        action='store_true',
        help='Deploy all content, rather than just changed files.')
    parser.add_argument(
        '--deploy_gltf',
        default=False,
        action='store_true',
        help='Deploy glTF source with USD, for browser previews.')
    parser.add_argument(
        '--nodeploy',
        default=False,
        action='store_true',
        help='Don\'t deploy USDZ files.')
    parser.add_argument(
        '--redeploy',
        default=False,
        action='store_true',
        help='Re-deploy all content without rebuilding.')
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


def rename_with_retry(from_path, to_path, retry_limit):
  """Rename a directory, with retry."""
  for retry in range(retry_limit):
    try:
      if retry > 0:
        retry_sleep_sec = 0.1
        time.sleep(retry_sleep_sec)
        util.warn('Retrying failed rename: "%s" --> "%s"' %
                  (from_path, to_path))
      os.rename(from_path, to_path)
      return
    except IOError:
      continue
  util.error('\nRename failed after %s retries: "%s" --> "%s"' %
             (retry_limit, from_path, to_path))


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


if __name__ == '__main__':
  exit_code = main()
  sys.exit(exit_code)
