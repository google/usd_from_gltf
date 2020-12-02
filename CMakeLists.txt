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

cmake_minimum_required (VERSION 3.0)
project (usd_from_gltf)

# We don't use Python in this tool, but when the USD library is built with
# PXR_PYTHON_SUPPORT_ENABLED it references it anyway.
if (EXISTS "${USD_DIR}/lib/python/")
  find_package(Python COMPONENTS Development)
  if (NOT Python_FOUND)
    message(FATAL_ERROR "Missing python libs.")
  endif (NOT Python_FOUND)
endif (EXISTS "${USD_DIR}/lib/python/")

set(USD_INCLUDE_DIRS
  "${USD_DIR}/include"
  "${USD_DIR}/include/boost-1_61"
# Visual Studio 2017 requires boost 1.65.1.
  "${USD_DIR}/include/boost-1_65_1"
  "${USD_DIR}/include/boost-1_70"
  ${Python_INCLUDE_DIRS}
)

link_directories(
  "${USD_DIR}/lib"
  ${Python_LIBRARY_DIRS}
)

if (MSVC)
set(USD_LIBS
  gf.lib
  plug.lib
  sdf.lib
  tf.lib
  usd.lib
  usdGeom.lib
  usdShade.lib
  usdSkel.lib
  usdUtils.lib
  vt.lib
)
elseif (APPLE)
set(USD_LIBS
  ${Python_LIBRARIES}
  -lpthread
  libgf.dylib
  libplug.dylib
  libsdf.dylib
  libtf.dylib
  libusd.dylib
  libusdGeom.dylib
  libusdShade.dylib
  libusdSkel.dylib
  libusdUtils.dylib
  libvt.dylib
)
if (Python_FOUND)
  list(APPEND USD_LIBS "${USD_DIR}/lib/libboost_python.dylib")
endif (Python_FOUND)
else ()
set(USD_LIBS
  ${Python_LIBRARIES}
  -lpthread
  libgf.so
  libplug.so
  libsdf.so
  libtf.so
  libusd.so
  libusdGeom.so
  libusdShade.so
  libusdSkel.so
  libusdUtils.so
  libvt.so
)
if (Python_FOUND)
  list(APPEND USD_LIBS "${USD_DIR}/lib/libboost_python.so")
endif (Python_FOUND)
endif ()

if (MSVC)
add_compile_definitions(
    _CRT_SECURE_NO_WARNINGS
)
add_compile_options(
  /wd4996 # Call to 'std::copy' with parameters that may be unsafe
)

# Use the release runtime for all builds so we can compile the library in Debug
# without having to recompile dependencies.
set(CompilerFlags
  CMAKE_CXX_FLAGS
  CMAKE_CXX_FLAGS_DEBUG
  CMAKE_CXX_FLAGS_RELEASE
  CMAKE_C_FLAGS
  CMAKE_C_FLAGS_DEBUG
  CMAKE_C_FLAGS_RELEASE
)
foreach(CompilerFlag ${CompilerFlags})
  string(REPLACE "/MDd" "/MD" ${CompilerFlag} "${${CompilerFlag}}")
  string(REPLACE "/RTC1" "" ${CompilerFlag} "${${CompilerFlag}}")
endforeach()

else (MSVC)
add_compile_options(
  -Wno-deprecated # Silence deprecation warnings due to USD headers.
  -std=c++14
)

endif (MSVC)

# PIC is necessary for building the plugin shared library.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Set RPATH to locate USD shared libraries on Linux/OSX.
set(CMAKE_INSTALL_RPATH "${USD_DIR}/lib")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

add_subdirectory(common)
add_subdirectory(convert)
add_subdirectory(gltf)
add_subdirectory(process)
add_subdirectory(usd_from_gltf)
add_subdirectory(ufg_plugin)

install(EXPORT ufglib DESTINATION lib/ufg)
install(FILES ufg-config.cmake DESTINATION .)
