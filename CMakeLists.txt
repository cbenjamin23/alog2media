cmake_minimum_required(VERSION 3.20)

project(alog2media VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(MOOS_IVP_ROOT "$ENV{MOOS_IVP_ROOT}" CACHE PATH
    "Path to a configured and built MOOS-IvP source tree")

if(NOT MOOS_IVP_ROOT)
  get_filename_component(_sibling_moos_ivp "${PROJECT_SOURCE_DIR}/../moos-ivp" ABSOLUTE)
  if(EXISTS "${_sibling_moos_ivp}/ivp/src")
    set(MOOS_IVP_ROOT "${_sibling_moos_ivp}" CACHE PATH
        "Path to a configured and built MOOS-IvP source tree" FORCE)
  endif()
endif()

if(NOT EXISTS "${MOOS_IVP_ROOT}/ivp/src" OR
   NOT EXISTS "${MOOS_IVP_ROOT}/include/ivp" OR
   NOT EXISTS "${MOOS_IVP_ROOT}/lib/libmarineview.a")
  message(FATAL_ERROR
    "MOOS_IVP_ROOT must name a configured, built MOOS-IvP checkout. "
    "Configure with -DMOOS_IVP_ROOT=/path/to/moos-ivp")
endif()

set(FLTK_SKIP_FLUID TRUE)
set(FLTK_SKIP_FORMS TRUE)
set(FLTK_SKIP_IMAGES TRUE)
find_package(FLTK REQUIRED)
find_package(Freetype REQUIRED)
if(APPLE)
  find_package(OpenGL REQUIRED)
else()
  find_package(OpenGL REQUIRED COMPONENTS OpenGL EGL)
endif()
find_package(TIFF REQUIRED)

find_library(MOOS_GEODESY_LIBRARY
  NAMES MOOSGeodesy
  PATHS "${MOOS_IVP_ROOT}/build/MOOS/MOOSGeodesy/lib"
  NO_DEFAULT_PATH
  REQUIRED)

set(MOOS_GEODESY_INCLUDE_DIR
  "${MOOS_IVP_ROOT}/MOOS/MOOSGeodesy/libMOOSGeodesy/include")

set(_ivp_library_names
  marineview
  logutils
  helmivp
  geometry
  contacts
  ipfview
  encounters
  ivpbuild
  apputil
  mbutil
  ivpcore)

set(IVP_LIBRARIES)
foreach(_name IN LISTS _ivp_library_names)
  find_library(IVP_${_name}_LIBRARY
    NAMES ${_name}
    PATHS "${MOOS_IVP_ROOT}/lib"
    NO_DEFAULT_PATH
    REQUIRED)
  list(APPEND IVP_LIBRARIES "${IVP_${_name}_LIBRARY}")
endforeach()

add_executable(alog2media
  src/FfmpegEncoder.cpp
  src/HeadlessText.cpp
  src/MediaRenderer.cpp
  src/Options.cpp
  src/main.cpp
  "${MOOS_IVP_ROOT}/ivp/src/app_alogview/NavPlotViewer.cpp")

target_include_directories(alog2media PRIVATE
  src
  "${MOOS_IVP_ROOT}/include/ivp"
  "${MOOS_IVP_ROOT}/ivp/src/app_alogview"
  "${MOOS_GEODESY_INCLUDE_DIR}"
  ${FLTK_INCLUDE_DIR})

target_compile_definitions(alog2media PRIVATE
  ALOG2MEDIA_VERSION="${PROJECT_VERSION}"
  MOOSGEODESY_IMPL=MOOSGEODESY_IMPL_PROJ
  USE_UTM
  _USE_UNIQUE_PTR)

target_compile_options(alog2media PRIVATE
  -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-cast-function-type)

target_link_libraries(alog2media PRIVATE
  ${IVP_LIBRARIES}
  "${MOOS_GEODESY_LIBRARY}"
  ${FLTK_LIBRARIES}
  Freetype::Freetype
  TIFF::TIFF
  ${CMAKE_DL_LIBS}
  m
  ${IVP_geometry_LIBRARY}
  ${IVP_mbutil_LIBRARY}
  ${IVP_ivpcore_LIBRARY})

if(APPLE)
  find_library(COCOA_FRAMEWORK Cocoa REQUIRED)
  target_link_libraries(alog2media PRIVATE "${COCOA_FRAMEWORK}")
  target_sources(alog2media PRIVATE src/OffscreenContextCGL.cpp)
  target_compile_definitions(alog2media PRIVATE ALOG2MEDIA_HAVE_CGL=1)
  target_link_libraries(alog2media PRIVATE OpenGL::GL)
  set_source_files_properties(src/OffscreenContextCGL.cpp PROPERTIES
    COMPILE_OPTIONS "-Wno-deprecated-declarations")
elseif(UNIX)
  target_sources(alog2media PRIVATE src/OffscreenContextEGL.cpp)
  target_compile_definitions(alog2media PRIVATE
    ALOG2MEDIA_HAVE_EGL=1
    GL_GLEXT_PROTOTYPES=1)
  target_link_libraries(alog2media PRIVATE OpenGL::OpenGL OpenGL::EGL)
else()
  target_sources(alog2media PRIVATE src/OffscreenContextWindow.cpp)
endif()

set_target_properties(alog2media PROPERTIES
  BUILD_RPATH "${MOOS_IVP_ROOT}/build/MOOS/MOOSGeodesy/lib")

install(TARGETS alog2media RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(BUILD_TESTING)
  add_executable(options_test tests/options_test.cpp src/Options.cpp)
  target_include_directories(options_test PRIVATE src)
  target_compile_definitions(options_test PRIVATE
    ALOG2MEDIA_VERSION="${PROJECT_VERSION}")
  target_compile_options(options_test PRIVATE -Wall -Wextra -Wpedantic)

  add_test(NAME options COMMAND options_test)
  add_test(NAME cli_help COMMAND alog2media --help)
  add_test(NAME cli_version COMMAND alog2media --version)

  add_executable(fixture_map tests/fixture_map.cpp)
  target_link_libraries(fixture_map PRIVATE TIFF::TIFF)
  target_compile_options(fixture_map PRIVATE -Wall -Wextra -Wpedantic)

  find_program(FFMPEG_EXECUTABLE ffmpeg)
  find_program(FFPROBE_EXECUTABLE ffprobe)
  if((APPLE OR UNIX) AND FFMPEG_EXECUTABLE AND FFPROBE_EXECUTABLE)
    add_test(NAME render_smoke
      COMMAND ${CMAKE_COMMAND}
        -DALOG2MEDIA=$<TARGET_FILE:alog2media>
        -DFIXTURE_MAP=$<TARGET_FILE:fixture_map>
        -DFFMPEG=${FFMPEG_EXECUTABLE}
        -DFFPROBE=${FFPROBE_EXECUTABLE}
        -DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/render-smoke
        -P ${PROJECT_SOURCE_DIR}/tests/render_smoke.cmake)
  endif()
endif()
