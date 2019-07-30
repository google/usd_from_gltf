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

"""Utilities to deploy conversion results and generate preview HTML."""
# pylint: disable=g-bad-todo

from __future__ import print_function

import os

from . import util
from .util import join_path
from .util import status

# Python 2&3-compatible HTML escape function.
# See: https://python-future.org/compatible_idioms.html
try:
  from html import escape  # pylint: disable=g-import-not-at-top, g-importing-member
except ImportError:
  from cgi import escape  # pylint: disable=g-import-not-at-top, g-importing-member


HTML_PREFIX_FORMAT = """
<!DOCTYPE html>
<html lang="en">
  <head>
    <title>USDZ Files</title>
    <meta charset="utf-8">
    <meta http-equiv="X-UA-Compatible" content="IE=edge">
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <link href="https://fonts.googleapis.com/icon?family=Material+Icons" rel="stylesheet">
%s
    <style>
      .ufg-section {
        background-color: #788;
        color: white;
        cursor: pointer;
        padding: 15px;
        width: 100%%;
        border: 2px solid #fff;
        text-align: left;
        outline: none;
        font-size: 18px;
      }
      .ufg-section:active, .ufg-section:hover {
        background-color: #566;
      }
      .ufg-section-content {
        padding: 0 15px;
        display: none;
        overflow: hidden;
        background-color: #f1f1f1;
      }
    </style>

    <style>
      .ufg-group {
        background-color: #999;
        color: white;
        cursor: pointer;
        padding: 10px;
        width: 100%%;
        border: 2px solid #fff;
        text-align: left;
        outline: none;
        font-size: 15px;
      }
      .ufg-group:active, .ufg-group:hover {
        background-color: #777;
      }
      .ufg-group-content {
        padding: 0 15px;
        display: none;
        overflow: hidden;
        background-color: #e1e1e1;
      }
    </style>

    <style>
      .ufg-model {
        background-color: #fff;
        color: white;
        cursor: pointer;
        padding: 2px;
        width: 40px;
        border: 2px solid #000;
        border-radius: 100px;
        text-align: center;
        float: right;
        outline: none;
        font-size: 0;
      }
      .ufg-model:active, .ufg-model:hover {
        background-color: #ddd;
      }
      .ufg-model-content {
        padding: 0 0;
        display: none;
        background-color: #e1e1e1;
      }
    </style>

    <style>
      .ufg-msg {
        background-color: #555;
        cursor: pointer;
        outline: none;
        text-decoration: underline;
      }
      .ufg-msg:active, .ufg-msg:hover {
        background-color: #666;
      }
      .ufg-msg-content {
        padding: 0 0;
        display: none;
        background-color: #888;
        font-size: 14px;
        text-shadow: 1px 1px #000;
      }
    </style>
  </head>
  <body>
"""

HTML_SUFFIX = """
    <!-- Collapsible button script. -->
    <script>
      function findChildByTagName(element, key) {
        var ucKey = key.toUpperCase();
        var children = element.childNodes;
        for (var i = 0; i < children.length; ++i) {
          var child = children[i];
          if (child.nodeType === 1 && child.tagName.toUpperCase() === ucKey) {
            return child;
          }
        }
        return null;
      }

      var buttons = document.querySelectorAll('.ufg-section, .ufg-group, .ufg-model, .ufg-msg')
      var i;
      for (i = 0; i < buttons.length; i++) {
        buttons[i].addEventListener("click", function() {
          this.classList.toggle("active");
          var content = this.nextElementSibling;
          if (content.style.display === "block") {
            content.style.display = "none";
          } else {
            content.style.display = "block";
            viewer = findChildByTagName(content, "model-viewer");
            if (viewer) {
              viewer.dismissPoster();
            }
          }
          window.dispatchEvent(new CustomEvent('resize'));
        });
      }
    </script>
  </body>
</html>
"""

HTML_SECTION_HEADER_FORMAT = """
    <button class="ufg-section">%s</button>
    <div class="ufg-section-content">
"""

HTML_SECTION_FOOTER_FORMAT = """    </div><br/>
"""

HTML_GROUP_HEADER_FORMAT = """
      <button class="ufg-group">%s [%s]</button>
      <div class="ufg-group-content">
"""

HTML_GROUP_FOOTER_FORMAT = """      </div>
"""

HTML_USDZ_FORMAT = """        <a href="{0}.usdz" rel="ar"><img src="usdz_icon.png" width="30" height="30">{1}</a>"""

HTML_ERROR_FORMAT = """        <i class="material-icons" style="color:red">error</i>{0}"""

# Note, the documentation recommends additional polyfills for compatibility:
# https://github.com/GoogleWebComponents/model-viewer/blob/master/POLYFILLS.md
# We only really need the resize behavior though, so rather than use the Resize
# Observer, the collapsible button script explicitly sends a window resize
# event.
HTML_MODEL_VIEWER_SCRIPT = """
    <!-- Load model-viewer script. -->
    <script type="module" src="https://unpkg.com/@google/model-viewer/dist/model-viewer.js"></script>
    <script nomodule src="https://unpkg.com/@google/model-viewer/dist/model-viewer-legacy.js"></script>
"""

# TODO: Generate thumbnails from the glTF instead of a placeholder.
# * This could be done by capturing <model-viewer> screenshots with Puppeteer:
#   See: https://github.com/GoogleChrome/puppeteer
#   Example:
#   https://github.com/GoogleWebComponents/model-viewer/blob/master/src/test/fidelity/artifact-creator.ts#L130-L206
HTML_MODEL_VIEWER_TAG_FORMAT = """
        <button class="ufg-model"><i class="material-icons" width="22" height="22" style="color:black">3d_rotation</i></button>
        <div class="ufg-model-content"><model-viewer src="gltf/{0}" background-color="#999999" autoplay camera-controls reveal="interaction"></model-viewer></div>"""


def copy_to_deploy_dir(tasks, in_path, test_path, deploy_path, deploy_gltf):
  """Copy test USDZ files to the local deploy directory."""
  gltf_dep_exts = ['.bin', '.bmp', '.gif', '.png', '.jpg', '.jpeg']
  deploy_exts = ['.usdz', '.gltf', '.glb'] + gltf_dep_exts
  if os.path.exists(deploy_path):
    # Delete all USDZ files. Note we could delete the entire directory, but this
    # is safer.
    del_count = util.delete_files_with_extension(deploy_path, deploy_exts)
    status('Deleted %s old deployment files.' % del_count)
  else:
    util.make_directories(deploy_path)
  status('Deploying %s files.' % len(tasks))
  for task in tasks:
    usdz_name = task.name + '.usdz'
    test_usdz_path = join_path(test_path, task.dst, usdz_name)
    deploy_usdz_path = join_path(deploy_path, usdz_name)
    if os.path.exists(test_usdz_path):
      util.copy_file(test_usdz_path, deploy_usdz_path)

    # Optionally also deploy source glTF files.
    if deploy_gltf:
      task_in_path = join_path(in_path, task.src)
      deploy_gltf_path = join_path(deploy_path, 'gltf', task.src)
      (deploy_gltf_dir, gltf_name) = os.path.split(deploy_gltf_path)
      util.make_directories(deploy_gltf_dir)
      util.copy_file(task_in_path, deploy_gltf_path)
      (_, in_ext) = os.path.splitext(gltf_name)
      if in_ext != '.glb':
        # Copy all image and .bin files under the same directory.
        # Note, this will copy more than necessary in cases where multiple GLTFs
        # are in the same directory, but this is a lot easier than parsing the
        # json for dependencies.
        (src_gltf_dir, _) = os.path.split(task_in_path)
        util.copy_files_with_extensions(src_gltf_dir, deploy_gltf_dir,
                                        gltf_dep_exts)


def organize_tasks_for_html(tasks):
  """Organize tasks into a tree for HTML display.

  The tasks are organized first by section, then broken up into ranges grouped
  by first letter.

  Args:
    tasks: Tasks to organize.

  Returns:
    Array of sections tuples (section, groups).
  """

  group_size_min = 10

  # Split tasks into sections.
  section = tasks[0].section
  section_tasks = []
  tasks_per_section = [(section, section_tasks)]
  for task in tasks:
    if task.section != section:
      section = task.section
      section_tasks = []
      tasks_per_section.append((section, section_tasks))
    section_tasks.append(task)

  # Break up sections into alphabetic ranges.
  section_count = len(tasks_per_section)
  section_sets = [None]*section_count
  for section_index in range(0, section_count):
    (section, section_tasks) = tasks_per_section[section_index]
    if len(section_tasks) <= group_size_min:
      # Small section - put all tasks under a single unnamed group.
      section_sets[section_index] = (section, [(None, section_tasks)])
    else:
      # Sort alphabetically by the first character of each task name.
      section_tasks.sort(key=lambda task: task.name[0].upper())

      # Partition tasks into alphabetic ranges, each of size >= group_size_min.
      groups = []
      # [c0, c1]: The current character range.
      c0 = section_tasks[0].name[0].upper()
      c1 = c0
      group_tasks = []
      for task in section_tasks:
        c = task.name[0].upper()
        if c != c1 and len(group_tasks) >= group_size_min:
          # Group is large enough. Add it to the set of groups for the section.
          group_name = c0 if (c0 == c1) else (c0 + '-' + c1)
          groups.append((group_name, group_tasks))
          c0 = c
          group_tasks = []
        c1 = c
        group_tasks.append(task)
      if group_tasks:
        group_name = c0 if (c0 == c1) else (c0 + '-' + c1)
        groups.append((group_name, group_tasks))

      section_sets[section_index] = (section, groups)

  return section_sets


def get_html_message_block(log_text):
  """Get HTML for expandable text messages."""
  info_color = '"white"'
  warning_color = '"yellow"'
  error_color = '#FF5544'
  bullet_format = '          <li><font color=%s>%s</font></li>\n'

  if not log_text:
    return ''

  severity = 0
  bullets = []
  for log_line in log_text.splitlines():
    if not log_line:
      continue
    color_tag = info_color
    if 'ERROR:' in log_line:
      color_tag = error_color
      severity = 2
    elif 'Warning:' in log_line:
      color_tag = warning_color
      severity = max(severity, 1)
    bullets.append(bullet_format % (color_tag, escape(log_line.strip())))

  color_tag = info_color
  if severity == 2:
    color_tag = error_color
  elif severity == 1:
    color_tag = warning_color

  plurality = '' if len(bullets) == 1 else 's'
  label = '<font color=%s>[%s message%s]</font>' % (color_tag, len(bullets),
                                                    plurality)

  html_text = '\n        <button class="ufg-msg">%s</button>\n' % label
  html_text += '        <ul class="ufg-msg-content">\n'
  html_text += '\n'.join(bullets)
  html_text += '        </ul>'
  return html_text


def get_html_tags(tasks, heading, deploy_gltf):
  """Get HTML tags for each task, with links to USDZ files."""
  html = '    <h1>%s</h1>\n' % heading
  section_sets = organize_tasks_for_html(tasks)
  for (section, groups) in section_sets:
    section_label = section if section else '[default]'
    html += HTML_SECTION_HEADER_FORMAT % section_label
    for (group_name, group_tasks) in groups:
      if group_name:
        html += HTML_GROUP_HEADER_FORMAT % (group_name, len(group_tasks))
      for task in group_tasks:
        if task.success:
          html += HTML_USDZ_FORMAT.format(task.name, task.name)
        else:
          html += HTML_ERROR_FORMAT.format(task.name)
        html += get_html_message_block(task.output)
        if task.success and deploy_gltf:
          html += HTML_MODEL_VIEWER_TAG_FORMAT.format(task.src)
        html += '<br/>\n'
      if group_name:
        html += HTML_GROUP_FOOTER_FORMAT
    html += HTML_SECTION_FOOTER_FORMAT
  html += '    <br/>\n'
  return html


def generate_html(tasks, changed_tasks, deploy_gltf):
  """Generate index.html containing listings for changed and all files."""
  head_addendum = HTML_MODEL_VIEWER_SCRIPT if deploy_gltf else ''
  html = HTML_PREFIX_FORMAT % head_addendum
  if changed_tasks:
    html += get_html_tags(changed_tasks, '%s Changed' % len(changed_tasks),
                          deploy_gltf)
  html += get_html_tags(tasks, 'All Files', deploy_gltf)
  html += HTML_SUFFIX
  return html


def copy_html_resources(deploy_path):
  """Copy HTML resources to the deploy directory."""
  # Assume resources are "../html_resources" relative to this script.
  script_path = util.norm_abspath(__file__)
  script_dir = os.path.dirname(os.path.dirname(script_path))
  resource_dir = join_path(script_dir, 'html_resources')
  for dir_path, _, file_list in os.walk(resource_dir):
    rel_dir = util.norm_relpath(dir_path, resource_dir)
    for file_name in file_list:
      src_path = join_path(resource_dir, rel_dir, file_name)
      dst_path = join_path(deploy_path, rel_dir, file_name)
      util.copy_file(src_path, dst_path)
