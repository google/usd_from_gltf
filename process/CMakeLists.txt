find_package(ZLIB REQUIRED)
if (NOT ZLIB_FOUND)
  message(FATAL_ERROR "Missing zlib libs.")
endif (NOT ZLIB_FOUND)

find_package(draco)
if (NOT draco_FOUND)
  message(FATAL_ERROR "Draco package not found.")
endif (NOT draco_FOUND)

find_package(giflib)
if (NOT giflib_FOUND)
  message(FATAL_ERROR "Giflib package not found.")
endif (NOT giflib_FOUND)

find_package(stb_image)
if (NOT stb_image_FOUND)
  message(FATAL_ERROR "STB Image package not found.")
endif (NOT stb_image_FOUND)

include_directories(
  ..
  ${USD_INCLUDE_DIRS}
  ${draco_INCLUDE_DIR}/..
  ${giflib_INCLUDE_DIR}
  ${stb_image_INCLUDE_DIR}
)

add_library(process
  image_png.h
  math.cc
  math.h
  mesh.cc
  mesh.h
  process_util.cc
  process_util.h
  skin.cc
  skin.h
  access.h
  animation.cc
  animation.h
  color.cc
  color.h
  float_image.cc
  float_image.h
  image.cc
  image.h
  image_fallback.cc
  image_fallback.h
  image_gif.cc
  image_gif.h
  image_jpg.cc
  image_jpg.h
  image_png.cc
)

file(GLOB PROCESS_HEADERS "*.h")
set_target_properties(process PROPERTIES PUBLIC_HEADER "${PROCESS_HEADERS}")

# Libpng and libturbojpeg do not define library directories. We need to search
# for them because on different distros, they appear in different locations.
set(INSTALLED_LIBRARY_DIR ${stb_image_LIBRARY_DIR})
get_filename_component(PARENT_DIR ${stb_image_LIBRARY_DIR} DIRECTORY)
find_library(PNG_LIBRARY
  NAMES libpng16.a libpng16_static.lib
  HINTS ${stb_image_LIBRARY_DIR} ${PARENT_DIR}/lib64
)
if (NOT PNG_LIBRARY)
  message(FATAL_ERROR "Libpng not found.")
endif (NOT PNG_LIBRARY)

find_library(TURBOJPEG_LIBRARY
  NAMES libturbojpeg.a turbojpeg-static.lib
  HINTS ${stb_image_LIBRARY_DIR} ${PARENT_DIR}/lib64
)
if (NOT TURBOJPEG_LIBRARY)
  message(FATAL_ERROR "Libturbojpeg not found.")
endif (NOT TURBOJPEG_LIBRARY)

if (MSVC)
target_link_libraries(process
  common
  "${ZLIB_LIBRARIES}"
  "${draco_LIBRARY_DIR}/dracodec.lib"
  "${giflib_LIBRARY_DIR}/giflib.lib"
  "${stb_image_LIBRARY_DIR}/stb_image.lib"
  "${PNG_LIBRARY}"
  "${TURBOJPEG_LIBRARY}"
)
else (MSVC)

target_link_libraries(process
  common
  "${ZLIB_LIBRARIES}"
  "${draco_LIBRARY_DIR}/libdracodec.a"
  "${giflib_LIBRARY_DIR}/libgiflib.a"
  "${stb_image_LIBRARY_DIR}/libstb_image.a"
  "${PNG_LIBRARY}"
  "${TURBOJPEG_LIBRARY}"
)
endif (MSVC)

install(TARGETS process EXPORT ufglib DESTINATION lib/ufg PUBLIC_HEADER DESTINATION include/ufg/process)
