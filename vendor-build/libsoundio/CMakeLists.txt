# Our own build rules for the vendored libsoundio snapshot.
#
# vendor/libsoundio/ is a byte-exact upstream tree and must not be modified
# (scripts/verify-vendor.sh checks its digest), so the build rules live here.
#
# This deliberately does NOT use libsoundio's own CMakeLists: that file carries
# cmake_minimum_required(VERSION 2.8.5), which CMake 4.x refuses outright. By
# compiling the sources ourselves we sidestep that permanently, which is what
# makes the orphaned patches/libsoundio-cmake4.diff moot rather than merely dead.
#
# Only the CoreAudio backend is built. alsa.c, jack.c, pulseaudio.c and wasapi.c
# are never compiled - smaller surface, nothing to maintain for platforms we do
# not target.

cmake_minimum_required(VERSION 3.28)
project(womm_libsoundio VERSION 2.0.0 LANGUAGES C)

set(LIBSOUNDIO_SRC "${CMAKE_CURRENT_SOURCE_DIR}/../../vendor/libsoundio")
if(NOT EXISTS "${LIBSOUNDIO_SRC}/src/soundio.c")
  message(FATAL_ERROR "vendored libsoundio not found at ${LIBSOUNDIO_SRC}")
endif()

set(LIBSOUNDIO_VERSION_MAJOR 2)
set(LIBSOUNDIO_VERSION_MINOR 0)
set(LIBSOUNDIO_VERSION_PATCH 0)
set(LIBSOUNDIO_VERSION "2.0.0")
set(SOUNDIO_HAVE_COREAUDIO ON) # consumed by config.h.in via #cmakedefine

configure_file("${LIBSOUNDIO_SRC}/src/config.h.in" "${CMAKE_CURRENT_BINARY_DIR}/config.h")

add_library(soundio SHARED
  "${LIBSOUNDIO_SRC}/src/soundio.c"
  "${LIBSOUNDIO_SRC}/src/util.c"
  "${LIBSOUNDIO_SRC}/src/os.c"
  "${LIBSOUNDIO_SRC}/src/dummy.c"
  "${LIBSOUNDIO_SRC}/src/channel_layout.c"
  "${LIBSOUNDIO_SRC}/src/ring_buffer.c"
  "${LIBSOUNDIO_SRC}/src/coreaudio.c")

target_include_directories(soundio
  PRIVATE "${LIBSOUNDIO_SRC}/src" "${CMAKE_CURRENT_BINARY_DIR}"
  PUBLIC $<BUILD_INTERFACE:${LIBSOUNDIO_SRC}> $<INSTALL_INTERFACE:include>)

# upstream sources are C99 and warn under modern clang; they are not ours to fix
target_compile_options(soundio PRIVATE -std=c11 -w)

find_library(COREAUDIO_LIBRARY CoreAudio REQUIRED)
find_library(COREFOUNDATION_LIBRARY CoreFoundation REQUIRED)
find_library(AUDIOUNIT_LIBRARY AudioUnit REQUIRED)
target_link_libraries(soundio PRIVATE
  ${COREAUDIO_LIBRARY} ${COREFOUNDATION_LIBRARY} ${AUDIOUNIT_LIBRARY})

set_target_properties(soundio PROPERTIES
  VERSION "${LIBSOUNDIO_VERSION}" SOVERSION "${LIBSOUNDIO_VERSION_MAJOR}"
  MACOSX_RPATH ON)

install(TARGETS soundio LIBRARY DESTINATION lib)
install(FILES "${LIBSOUNDIO_SRC}/soundio/soundio.h" "${LIBSOUNDIO_SRC}/soundio/endian.h"
        DESTINATION include/soundio)

# gr4 looks for this via pkg_search_module(soundio libsoundio) first, then falls
# back to find_path/find_library. Providing the .pc satisfies the preferred path.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/libsoundio.pc"
"prefix=${CMAKE_INSTALL_PREFIX}
exec_prefix=\${prefix}
libdir=\${prefix}/lib
includedir=\${prefix}/include

Name: libsoundio
Description: cross-platform audio input and output (CoreAudio backend only)
Version: ${LIBSOUNDIO_VERSION}
Libs: -L\${libdir} -lsoundio
Cflags: -I\${includedir}
")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/libsoundio.pc" DESTINATION lib/pkgconfig)
