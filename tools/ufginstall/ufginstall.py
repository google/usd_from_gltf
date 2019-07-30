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

"""Install usd_from_gltf and dependencies.

Builds and installs usd_from_gltf and dependencies, downloading as necessary.

The USD library is a prerequisite and must be installed first.
"""

import argparse
import contextlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import time
import zipfile

# Python 2&3-compatible urlopen import.
# See: https://python-future.org/compatible_idioms.html
try:
  from urllib.request import urlopen  # pylint: disable=g-import-not-at-top, g-importing-member
except ImportError:
  from urllib2 import urlopen  # pylint: disable=g-import-not-at-top, g-importing-member


ALL_DEPS = []

LOG_COLOR_DEFAULT = 0
LOG_COLOR_RED = 31
LOG_COLOR_GREEN = 32
LOG_COLOR_YELLOW = 33
LOG_COLOR_CYAN = 36

cfg = None
task = None


class Cfg(object):
  """Install configuration globals."""

  def __init__(self, args):
    # Infer usd_from_gltf location from script location (which should be at
    # usd_from_gltf/tools/ufginstall).
    script_dir = os.path.dirname(os.path.realpath(__file__))
    ufg_src_dir = os.path.join(script_dir, '..', '..')

    # Source directory defaults to <install>/src
    default_src_dir = os.path.join(args.install_dir, 'src')
    src_dir = args.src_dir if args.src_dir else default_src_dir

    # Build directory defaults to <install>/build
    default_build_dir = os.path.join(args.install_dir, 'build')
    build_dir = args.build_dir if args.build_dir else default_build_dir

    self.verbosity = args.verbosity
    self.script_dir = script_dir
    self.ufg_src_dir = os.path.abspath(ufg_src_dir)
    self.inst_dir = os.path.abspath(args.install_dir)
    self.usd_dir = os.path.abspath(args.usd_dir)
    self.src_dir = os.path.abspath(src_dir)
    self.build_dir = os.path.abspath(build_dir)
    self.generator = args.generator
    self.build_config = args.config
    self.force_build_all = args.force_all
    self.force_build = [dep.lower() for dep in args.force_build]


class Task(object):
  """Global task state."""

  def __init__(self):
    self.prefix = ''
    self.download_duration = 0.0
    self.build_duration = 0.0


@contextlib.contextmanager
def cwd(new_dir):
  """Set current working directory in a 'with' scope."""
  old_dir = os.getcwd()
  os.chdir(new_dir)
  try:
    yield
  finally:
    os.chdir(old_dir)


class Dep(object):
  """Base class for install dependencies."""

  def __init__(self, name, *inst_file_names):
    self.name = name
    self.installed = False
    self.inst_file_names = inst_file_names
    ALL_DEPS.append(self)

  def exists(self):
    for name in self.inst_file_names:
      path = os.path.join(cfg.inst_dir, name)
      if not os.path.isfile(path):
        return False
    return True

  def forced(self):
    return cfg.force_build_all or (self.name.lower() in cfg.force_build)

  def install_single(self, url, rel_path, patch_paths, extra_args=None):
    path = os.path.join(cfg.src_dir, rel_path)
    force = self.forced()
    dl_dir = download_single(url, path, force)
    copy_patch_files(patch_paths, dl_dir)
    with cwd(dl_dir):
      run_cmake(force, extra_args)


class DracoDep(Dep):
  """Installs Draco dependency."""

  def __init__(self):
    Dep.__init__(self, 'DRACO', 'include/draco/draco_features.h')

  def install(self):
    """Installs Draco dependency."""
    url = 'https://github.com/google/draco/archive/1.3.5.zip'
    path = os.path.join(cfg.src_dir, 'draco.zip')
    force = self.forced()
    dl_dir = download_archive(url, path, force)
    with cwd(dl_dir):
      extra_args = ['-DCMAKE_POSITION_INDEPENDENT_CODE=1']
      # Fix case-sensitivity bug in cmake script.
      patches = [
          ('${draco_build_dir}/DracoConfig.cmake',
           '${draco_build_dir}/dracoConfig.cmake')]
      patch_file_text('CMakeLists.txt', patches)
      run_cmake(force, extra_args)


class GifDep(Dep):
  """Installs giflib dependency."""

  def __init__(self):
    Dep.__init__(self, 'GIF', 'include/gif_lib.h')

  def install(self):
    """Installs giflib dependency."""
    url = 'https://sourceforge.net/projects/giflib/files/giflib-5.1.9.tar.gz/download'
    extra_args = ['-DCMAKE_POSITION_INDEPENDENT_CODE=1']
    patch_paths = ['giflib/CMakeLists.txt', 'giflib/giflib-config.cmake']
    if platform.system() == 'Windows':
      patch_paths.append('giflib/unistd.h')
    path = os.path.join(cfg.src_dir, 'giflib.tar.gz')
    force = self.forced()
    dl_dir = download_archive(url, path, force)
    copy_patch_files(patch_paths, dl_dir)
    with cwd(dl_dir):
      run_cmake(force, extra_args)


class JpgDep(Dep):
  """Installs libjpeg-turbo dependency."""

  def __init__(self):
    Dep.__init__(self, 'JPG', 'include/jpeglib.h')

  def install(self):
    """Installs libjpeg-turbo dependency."""
    url = 'https://github.com/libjpeg-turbo/libjpeg-turbo/archive/2.0.2.zip'
    extra_args = ['-DCMAKE_POSITION_INDEPENDENT_CODE=1']
    path = os.path.join(cfg.src_dir, 'jpg.zip')
    force = self.forced()
    dl_dir = download_archive(url, path, force)
    with cwd(dl_dir):
      run_cmake(force, extra_args)


class JsonDep(Dep):
  """Installs nlohmann/json dependency."""

  def __init__(self):
    Dep.__init__(self, 'JSON', 'include/json.hpp')

  def install(self):
    """Installs nlohmann/json dependency."""
    url = 'https://github.com/nlohmann/json/releases/download/v3.6.1/json.hpp'
    patch_paths = ['json/CMakeLists.txt', 'json/json-config.cmake']
    self.install_single(url, 'json/json.hpp', patch_paths)


class PngDep(Dep):
  """Installs libpng dependency."""

  def __init__(self):
    Dep.__init__(self, 'PNG', 'include/png.h')

  def install(self):
    """Installs libpng dependency."""
    url = 'https://download.sourceforge.net/libpng/libpng-1.6.37.tar.gz'
    extra_args = ['-DCMAKE_POSITION_INDEPENDENT_CODE=1']
    path = os.path.join(cfg.src_dir, 'png.zip')
    force = self.forced()
    dl_dir = download_archive(url, path, force)
    with cwd(dl_dir):
      run_cmake(force, extra_args)


class StbImageDep(Dep):
  """Installs STB-image dependency."""

  def __init__(self):
    Dep.__init__(self, 'STB_IMAGE', 'include/stb_image.h')

  def install(self):
    """Installs STB-image dependency."""
    url = 'https://raw.githubusercontent.com/nothings/stb/master/stb_image.h'
    extra_args = ['-DCMAKE_POSITION_INDEPENDENT_CODE=1']
    patch_paths = [
        'stb_image/CMakeLists.txt',
        'stb_image/stb_image.cc',
        'stb_image/stb_image-config.cmake'
    ]
    self.install_single(url, 'stb_image/stb_image.h', patch_paths, extra_args)


class TclapDep(Dep):
  """Installs TCLAP dependency."""

  def __init__(self):
    Dep.__init__(self, 'TCLAP', 'include/tclap/CmdLine.h')

  def install(self):
    """Installs TCLAP dependency."""
    url = 'https://sourceforge.net/projects/tclap/files/tclap-1.2.2.tar.gz/download'
    patch_paths = ['tclap/CMakeLists.txt', 'tclap/tclap-config.cmake']
    path = os.path.join(cfg.src_dir, 'tclap.tar.gz')
    force = self.forced()
    dl_dir = download_archive(url, path, force)
    copy_patch_files(patch_paths, dl_dir)
    with cwd(dl_dir):
      run_cmake(force)


class TestDataSamplesDep(Dep):
  """Installs Khronos sample test data dependency."""

  def __init__(self):
    sentinel = 'src/testdata/2.0/2CylinderEngine/glTF/2CylinderEngine.gltf'
    Dep.__init__(self, 'TESTDATA_SAMPLES', sentinel)

  def install(self):
    """Installs Khronos sample test data dependency."""
    url = 'https://codeload.github.com/KhronosGroup/glTF-Sample-Models/tar.gz/eaab0f2'
    path = os.path.join(cfg.src_dir, 'testdata_samples.tar.gz')
    force = self.forced()
    src_data_dir = os.path.join(
        cfg.src_dir, 'glTF-Sample-Models-eaab0f2', '2.0')
    dst_data_dir = os.path.join(cfg.src_dir, 'testdata', '2.0')
    if os.path.isdir(dst_data_dir):
      shutil.rmtree(dst_data_dir)
    download_archive(url, path, force)
    shutil.move(src_data_dir, dst_data_dir)


class TestDataReferenceDep(Dep):
  """Installs glTF generated reference test data dependency."""

  def __init__(self):
    sentinel = 'src/testdata/GeneratedAssets-0.6.0/Animation_Node/Animation_Node_00.gltf'
    Dep.__init__(self, 'TESTDATA_REFERENCE', sentinel)

  def install(self):
    """Installs glTF generated reference test data dependency."""
    url = 'https://github.com/KhronosGroup/glTF-Asset-Generator/releases/download/v0.6.0/GeneratedAssets-0.6.0.zip'
    path = os.path.join(cfg.src_dir, 'testdata_reference.zip')
    force = self.forced()
    src_data_dir = os.path.join(cfg.src_dir, 'testdata_reference')
    dst_data_dir = os.path.join(
        cfg.src_dir, 'testdata', 'GeneratedAssets-0.6.0')
    if os.path.isdir(dst_data_dir):
      shutil.rmtree(dst_data_dir)
    download_archive(url, path, force, root='testdata_reference')
    shutil.move(src_data_dir, dst_data_dir)


class UsdFromGltfDep(Dep):
  """Installs usd_from_gltf."""

  def __init__(self):
    Dep.__init__(self, 'USD_FROM_GLTF', None, 'include/ufg/convert/package.h')

  def install(self):
    """Installs usd_from_gltf."""
    with cwd(cfg.ufg_src_dir):
      extra_args = ['-DUSD_DIR=%s' % cfg.usd_dir]
      force = self.forced()
      run_cmake(force, extra_args)


DRACO = DracoDep()
GIF = GifDep()
JPG = JpgDep()
JSON = JsonDep()
PNG = PngDep()
STB_IMAGE = StbImageDep()
TCLAP = TclapDep()
TESTDATA_SAMPLES = TestDataSamplesDep()
TESTDATA_REFERENCE = TestDataReferenceDep()
USD_FROM_GLTF = UsdFromGltfDep()


def main():
  if platform.system() == 'Windows':
    os.system('color')  # Enable text colors.
  args = parse_args()
  global cfg
  cfg = Cfg(args)
  global task
  task = Task()

  # Get the set of dependencies to install.
  deps = [DRACO, GIF, JPG, JSON, PNG, STB_IMAGE, TCLAP]
  if args.testdata:
    deps += [TESTDATA_SAMPLES, TESTDATA_REFERENCE]
  installed_deps = []
  build_deps = []
  for dep in deps:
    dep.installed = not dep.forced() and dep.exists()
    if dep.installed:
      installed_deps.append(dep)
    else:
      build_deps.append(dep)
  build_deps.append(USD_FROM_GLTF)

  # Print summary.
  installed_text = ', '.join([d.name for d in installed_deps])
  build_text = ', '.join([d.name for d in build_deps])
  summary_format = """Install Settings:
  Source Directory:   {ufg_src_dir}
  Install Directory:  {inst_dir}
  USD Directory:      {usd_dir}
  Download Directory: {src_dir}
  Build Directory:    {build_dir}
  Build Config:       {build_config}
  CMake Generator:    {generator}
  Already Installed:  {installed_deps}
  Installing:         {build_deps}"""
  summary = summary_format.format(
      ufg_src_dir=cfg.ufg_src_dir,
      inst_dir=cfg.inst_dir,
      usd_dir=cfg.usd_dir,
      src_dir=cfg.src_dir,
      build_dir=cfg.build_dir,
      build_config=cfg.build_config,
      generator=('Default' if not cfg.generator else cfg.generator),
      installed_deps=(installed_text if installed_text else '<none>'),
      build_deps=(build_text if build_text else '<none>'))
  status(summary)

  if args.dry_run:
    status('\n-------- SUCCESS --------', LOG_COLOR_GREEN)
    status('Install skipped due to --dry_run.')
  else:
    # Create output directories and ensure they're writable.
    output_dirs = [cfg.inst_dir, cfg.src_dir, cfg.build_dir]
    ensure_directories(output_dirs)

    # Install dependencies.
    try:
      for dep in build_deps:
        status('\n-------- Installing %s --------' % dep.name, LOG_COLOR_CYAN)
        task.prefix = '  %s: ' % dep.name
        dep.install()
        task.prefix = ''
    except Exception as e:  # pylint: disable=broad-except
      error(str(e))
      return 1
    status('\n-------- SUCCESS --------', LOG_COLOR_GREEN)
    status('  Download Time: %.2fs. Build Time: %.2fs' %
           (task.download_duration, task.build_duration))

  status('\n-------- Usage Notes --------', LOG_COLOR_CYAN)

  exe_ext = '.exe' if (platform.system() == 'Windows') else ''
  exe_path = os.path.join(cfg.inst_dir, 'bin', 'usd_from_gltf' + exe_ext)
  status("""The executable is at:
  {exe_path}
Optionally, add this to your PATH:
  {bin_dir}""".format(
      exe_path=exe_path,
      bin_dir=os.path.join(cfg.inst_dir, 'bin')))

  if platform.system() == 'Windows':
    status("""
To rebuild or debug, open this project in Visual Studio:
  {sln_path}""".format(
      sln_path=os.path.join(cfg.build_dir, 'ufg', 'ufg_from_gltf.sln')))

  if args.testdata:
    status("""
Convert testdata with:
  cd {csv_dir}
  python "{ufgtest_path}" samp_gltf.csv samp_glb.csv samp_embed.csv samp_draco.csv samp_specgloss.csv ref.csv -i "{in_dir}" -o "{out_dir}" --exe "{exe_path}" --nodiff --nodeploy"""
           .format(
               csv_dir=os.path.join(cfg.ufg_src_dir, 'testdata'),
               ufgtest_path=os.path.join(cfg.ufg_src_dir, 'tools', 'ufgbatch',
                                         'ufgtest.py'),
               in_dir=os.path.join(cfg.src_dir, 'testdata'),
               out_dir=os.path.join(cfg.build_dir, 'testdata'),
               exe_path=exe_path))

  status("""
To view glTF files in Usdview, set PXR_PLUGINPATH_NAME to:
  {plugin_dir}""".format(
      plugin_dir=os.path.join(cfg.inst_dir, 'bin', 'ufg_plugin')))

  if platform.system() == 'Windows':
    status("""
To view USD/glTF files in Explorer, right-click 'Open with':
  {viewer_path}""".format(
      viewer_path=os.path.join(cfg.ufg_src_dir, 'tools', 'usdviewer.bat')))

  return 0


def parse_args():
  """Parse command-line arguments."""
  try:
    dep_text = ', '.join(sorted([d.name for d in ALL_DEPS]))

    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=__doc__)

    parser.add_argument(
        'install_dir', type=str, help='usd_from_gltf install directory.')
    parser.add_argument('usd_dir', type=str, help='USD library directory.')
    parser.add_argument(
        '--dry_run',
        dest='dry_run',
        action='store_true',
        help='Summarize without installing.')
    parser.add_argument(
        '--src_dir',
        type=str,
        help='Dependency download directory (default: <install_dir>/src).')
    parser.add_argument(
        '--testdata',
        action='store_true',
        default=False,
        help='Download test data.')

    verbosity_group = parser.add_mutually_exclusive_group()
    verbosity_group.add_argument(
        '-v',
        '--verbose',
        action='store_const',
        const=2,
        default=1,
        dest='verbosity',
        help='Verbose output.')
    verbosity_group.add_argument(
        '-q',
        '--quiet',
        action='store_const',
        const=0,
        dest='verbosity',
        help='Suppress all output except for errors.')

    build_group = parser.add_argument_group(title='Build Options')
    build_group.add_argument(
        '--build_dir',
        type=str,
        help=('Build directory (default: <install_dir>/build).'))
    build_group.add_argument(
        '-c',
        '--config',
        type=str,
        default='Release',
        help=('Build configuration (Debug or Release).'))
    build_group.add_argument(
        '--force',
        type=str,
        action='append',
        dest='force_build',
        default=[],
        help=('Force download and build library. May include: %s' % dep_text))
    build_group.add_argument(
        '--force-all',
        action='store_true',
        help='Force download and build all libraries.')
    build_group.add_argument(
        '--generator', type=str, help=('Override CMake generator (cmake -G).'))

    return parser.parse_args()
  except argparse.ArgumentError:
    exit(1)


def log(msg, color=LOG_COLOR_DEFAULT):
  """Log a message, prefixing lines with the current task."""
  # Colorize with ANSI escape codes: '\033[<style>;<fg-color>;<bg-color>m'.
  # See: https://en.wikipedia.org/wiki/ANSI_escape_code
  if cfg.verbosity > 0:
    if task.prefix:
      prefix = '\033[0;36;38m%s\033[1;%s;38m' % (task.prefix, color)
      msg = ''.join((prefix + line) for line in msg.splitlines(True))
      msg += '\033[0;0;0m'
    elif color != LOG_COLOR_DEFAULT:
      msg = '\033[1;%s;38m%s\033[0;0;0m' % (color, msg)
    print(msg)  # pylint: disable=superfluous-parens


def status(msg, color=LOG_COLOR_DEFAULT):
  """Log a status message."""
  if cfg.verbosity >= 1:
    log(msg, color)


def warn(msg):
  """Log an warning."""
  log('Warning: ' + msg, LOG_COLOR_YELLOW)


def error(msg):
  """Log an error."""
  log('ERROR: ' + msg, LOG_COLOR_RED)


def delete_directory_content(del_dir):
  """Delete all files and directories under 'del_dir', preserving 'del_dir'."""
  for name in os.listdir(del_dir):
    path = os.path.join(del_dir, name)
    if os.path.isdir(path):
      shutil.rmtree(path)
    else:
      os.remove(path)


def ensure_directories(dirs):
  """Create directories and ensure they're writable."""
  for cur_dir in dirs:
    try:
      if os.path.isdir(cur_dir):
        test_path = os.path.join(cur_dir, 'canwrite')
        open(test_path, 'w').close()
        os.remove(test_path)
      else:
        os.makedirs(cur_dir)
    except IOError:
      error('Cannot write to directory: %s' % cur_dir)
      return 1


def run(args):
  """Run the specified command in a subprocess."""
  cmd = ' '.join(args)
  status('CWD: %s' % os.getcwd())
  status('Run: %s' % cmd)
  with open('log.txt', 'a') as log_file:
    log_file.write('\n')
    log_file.write(cmd)
    log_file.write('\n')
    process = subprocess.Popen(
        args,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        universal_newlines=True)
    (cmd_stdout, _) = process.communicate()
    output = cmd_stdout if cmd_stdout else ''
    if cmd_stdout:
      log_file.write(output)
    if process.returncode != 0 or cfg.verbosity >= 2:
      print(output)  # pylint: disable=superfluous-parens
    if process.returncode != 0:
      raise RuntimeError('Command failed: %s\nSee log at: %s' %
                         (cmd, os.path.abspath('log.txt')))


def run_cmake(force, extra_args=None):
  """Run CMake to install and build in the current working directory."""
  src_dir = os.getcwd()
  inst_dir = cfg.inst_dir
  build_dir = os.path.join(cfg.build_dir, os.path.split(src_dir)[1])
  if force and os.path.isdir(build_dir):
    # Delete existing directory content, but preserve the directory itself
    # because we're going to need it (and on Windows, quickly deleting then
    # recreating a directory may cause transient failures).
    delete_directory_content(build_dir)

  if not os.path.isdir(build_dir):
    os.makedirs(build_dir)

  with cwd(build_dir):
    # CMake command to generate project files.
    gen_args = [
        'cmake',
        src_dir,
        '-DCMAKE_INSTALL_PREFIX=%s' % inst_dir,
        '-DCMAKE_PREFIX_PATH=%s' % inst_dir
    ]
    if extra_args:
      gen_args += extra_args
    if cfg.generator:
      gen_args += ['-G', cfg.generator]

    # CMake command to build.
    build_args = [
        'cmake',
        '--build', '.',
        '--config', cfg.build_config,
        '--target', 'install',
        '--'
    ]
    if platform.system() == 'Windows':
      gen_args.append('-DCMAKE_GENERATOR_PLATFORM=x64')
      build_args.append('/m')  # Multithreaded build.

    start_time = time.time()
    run(gen_args)
    run(build_args)
    end_time = time.time()
    duration = end_time - start_time
    task.build_duration += duration


def unpack_archive(archive_path, dst_parent_path, root):
  """Unpack archive_path to a subdirectory in dst_parent_path."""
  try:
    # Open the archive and get the root directory inside the archive (or
    # override with 'root').
    archive = None
    root_dir = None
    if tarfile.is_tarfile(archive_path):
      archive = tarfile.open(archive_path)
      root_dir = root if root else archive.getnames()[0].split('/')[0]
    elif zipfile.is_zipfile(archive_path):
      archive = zipfile.ZipFile(archive_path)
      root_dir = root if root else archive.namelist()[0].split('/')[0]
    else:
      raise RuntimeError('Unrecognized archive file type: %s' % archive_path)

    # Unpack to a temporary directory then move the root to the target.
    with archive:
      dst_path = os.path.join(dst_parent_path, root_dir)
      status('Unpacking to: %s' % dst_path)
      if os.path.isdir(dst_path):
        shutil.rmtree(dst_path)
      temp_path = os.path.join(dst_parent_path, 'unpack_tmp')
      if os.path.isdir(temp_path):
        delete_directory_content(temp_path)
      archive.extractall(temp_path)
      temp_root_path = temp_path if root else os.path.join(temp_path, root_dir)
      shutil.move(temp_root_path, dst_path)
      if not root:
        shutil.rmtree(temp_path)
      return dst_path

  except Exception as e:  # pylint: disable=broad-except
    shutil.move(archive_path, archive_path + '.bad')
    raise RuntimeError('Failed to extract archive "%s": %s' % (archive_path, e))


def download(url, path):
  """Download 'url' to 'path', retrying if necessary."""
  # Download to a temporary file and rename on completion.
  temp_path = path + '.tmp'
  if os.path.exists(temp_path):
    os.remove(temp_path)

  # Download with retries.
  start_time = time.time()
  max_retries = 5
  i = 0
  while True:
    try:
      src = urlopen(url)
      with open(temp_path, 'wb') as dst:
        dst.write(src.read())
      break
    except Exception as e:  # pylint: disable=broad-except
      if i == max_retries:
        raise RuntimeError('Failed to download %s: %s' % (url, e))
      warn('Retrying download due to error: %s\n' % e)
      i += 1
  end_time = time.time()
  duration = end_time - start_time
  task.download_duration += duration

  shutil.move(temp_path, path)


def download_single(url, path, force):
  """Download a single file at 'url' to 'path'."""
  # Make containing directory, cleaning if necessary.
  (dl_dir, _) = os.path.split(path)
  if force and os.path.isdir(dl_dir):
    delete_directory_content(dl_dir)
  if not os.path.isdir(dl_dir):
    os.makedirs(dl_dir)

  # Download file.
  if os.path.exists(path):
    status('File already exists, skipping download: %s' % path)
  else:
    status('Downloading %s to %s' % (url, path))
    download(url, path)
  return dl_dir


def download_archive(url, path, force, root=None):
  """Download and unpack an archive at 'url' to 'path'."""
  # Cleaning cached archive if necessary.
  if force and os.path.exists(path):
    os.remove(path)

  # Download and unpack archive.
  if os.path.exists(path):
    status('File already exists, skipping download: %s' % path)
  else:
    status('Downloading %s to %s' % (url, path))
    download(url, path)
  return unpack_archive(path, cfg.src_dir, root)


def copy_patch_files(patch_paths, dst_dir):
  """Copy patch files from the script directory to the downloaded directory."""
  patch_dir = os.path.join(cfg.script_dir, 'patches')
  for patch_path in patch_paths:
    src_path = os.path.join(patch_dir, patch_path)
    shutil.copy(src_path, dst_dir)


def patch_file_text(path, patches):
  """Applies patches as a list of (old, new) tuples to a text file."""
  try:
    with open(path, 'r') as fp:
      old_text = fp.read()
    new_text = old_text
    for (old, new) in patches:
      new_text = new_text.replace(old, new)
    if new_text != old_text:
      with open(path, 'w') as fp:
        fp.write(new_text)
  except IOError:
    pass


if __name__ == '__main__':
  exit_code = main()
  sys.exit(exit_code)
