cmake_minimum_required(VERSION 3.20)

project(alog2media VERSION 0.3.2 LANGUAGES CXX)

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
find_package(MOOS REQUIRED CONFIG
  PATHS "${MOOS_IVP_ROOT}/build/MOOS/MOOSCore"
  NO_DEFAULT_PATH)

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

find_library(IVP_GEODAID_LIBRARY
  NAMES geodaid
  PATHS "${MOOS_IVP_ROOT}/lib"
  NO_DEFAULT_PATH
  REQUIRED)

add_executable(alog2media
  src/ALogTimeline.cpp
  src/FfmpegEncoder.cpp
  src/GeometryReplay.cpp
  src/HeadlessSceneViewer.cpp
  src/HeadlessText.cpp
  src/MediaRenderer.cpp
  src/MissionConfig.cpp
  src/Options.cpp
  src/main.cpp)

target_include_directories(alog2media PRIVATE
  src
  "${MOOS_IVP_ROOT}/include/ivp"
  "${MOOS_GEODESY_INCLUDE_DIR}"
  ${FLTK_INCLUDE_DIR})

target_compile_definitions(alog2media PRIVATE
  ALOG2MEDIA_VERSION="${PROJECT_VERSION}"
  ALOG2MEDIA_SOURCE_MAP_DIR="${MOOS_IVP_ROOT}/ivp/data"
  ALOG2MEDIA_INSTALL_MAP_DIR="${CMAKE_INSTALL_FULL_DATADIR}/alog2media/maps"
  MOOSGEODESY_IMPL=MOOSGEODESY_IMPL_PROJ
  USE_UTM
  _USE_UNIQUE_PTR)

target_compile_options(alog2media PRIVATE
  -Wall -Wextra -Wpedantic -Wno-unused-parameter -Wno-cast-function-type)

target_link_libraries(alog2media PRIVATE
  ${IVP_LIBRARIES}
  MOOS::MOOS
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

if(APPLE)
  set(_alog2media_install_rpath "@loader_path/../${CMAKE_INSTALL_LIBDIR}")
elseif(UNIX)
  set(_alog2media_install_rpath "$ORIGIN/../${CMAKE_INSTALL_LIBDIR}")
endif()

set_target_properties(alog2media PROPERTIES
  BUILD_RPATH "${MOOS_IVP_ROOT}/build/MOOS/MOOSGeodesy/lib"
  INSTALL_RPATH "${_alog2media_install_rpath}")

install(TARGETS alog2media RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES "${MOOS_GEODESY_LIBRARY}" DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY "${MOOS_IVP_ROOT}/ivp/data/"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/alog2media/maps"
  FILES_MATCHING
    PATTERN "*.tif"
    PATTERN "*.tiff"
    PATTERN "*.info")
install(FILES LICENSE CHANGELOG.md THIRD_PARTY_NOTICES.md README.md
  DESTINATION ${CMAKE_INSTALL_DOCDIR})

if(BUILD_TESTING)
  add_executable(options_test tests/options_test.cpp src/Options.cpp)
  target_include_directories(options_test PRIVATE src)
  target_compile_definitions(options_test PRIVATE
    ALOG2MEDIA_VERSION="${PROJECT_VERSION}")
  target_compile_options(options_test PRIVATE -Wall -Wextra -Wpedantic)

  add_test(NAME options COMMAND options_test)
  add_test(NAME cli_help COMMAND alog2media --help)
  add_test(NAME cli_version COMMAND alog2media --version)

  add_executable(timeline_test
    tests/timeline_test.cpp
    src/ALogTimeline.cpp
    src/GeometryReplay.cpp)
  target_include_directories(timeline_test PRIVATE
    src
    "${MOOS_IVP_ROOT}/include/ivp"
    "${MOOS_GEODESY_INCLUDE_DIR}")
  target_compile_definitions(timeline_test PRIVATE
    MOOSGEODESY_IMPL=MOOSGEODESY_IMPL_PROJ
    USE_UTM
    _USE_UNIQUE_PTR)
  target_compile_options(timeline_test PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(timeline_test PRIVATE
    "${MOOS_GEODESY_LIBRARY}"
    ${IVP_contacts_LIBRARY}
    ${IVP_geometry_LIBRARY}
    ${IVP_mbutil_LIBRARY}
    m)
  add_test(NAME timeline
    COMMAND timeline_test
      ${PROJECT_SOURCE_DIR}/tests/fixtures/timeline.alog
      ${PROJECT_SOURCE_DIR}/tests/fixtures/latlon_only.alog)

  add_executable(geometry_replay_test
    tests/geometry_replay_test.cpp
    src/GeometryReplay.cpp)
  target_include_directories(geometry_replay_test PRIVATE
    src
    "${MOOS_IVP_ROOT}/include/ivp")
  target_compile_options(geometry_replay_test PRIVATE
    -Wall -Wextra -Wpedantic)
  target_link_libraries(geometry_replay_test PRIVATE
    ${IVP_geometry_LIBRARY}
    ${IVP_mbutil_LIBRARY}
    m)
  add_test(NAME geometry_replay COMMAND geometry_replay_test)

  add_executable(mission_config_test
    tests/mission_config_test.cpp
    src/MissionConfig.cpp)
  target_include_directories(mission_config_test PRIVATE src)
  target_compile_options(mission_config_test PRIVATE
    -Wall -Wextra -Wpedantic)
  target_link_libraries(mission_config_test PRIVATE MOOS::MOOS)
  add_test(NAME mission_config
    COMMAND mission_config_test
      ${PROJECT_SOURCE_DIR}/tests/fixtures/mission.moos)

  add_executable(fixture_map tests/fixture_map.cpp)
  target_link_libraries(fixture_map PRIVATE TIFF::TIFF)
  target_compile_options(fixture_map PRIVATE -Wall -Wextra -Wpedantic)

  add_executable(pmv_reference
    tests/pmv_reference.cpp
    src/ALogTimeline.cpp
    src/GeometryReplay.cpp
    src/HeadlessText.cpp
    src/MissionConfig.cpp
    "${MOOS_IVP_ROOT}/ivp/src/pMarineViewer/PMV_Viewer.cpp")
  if(APPLE)
    target_sources(pmv_reference PRIVATE src/OffscreenContextCGL.cpp)
    target_compile_definitions(pmv_reference PRIVATE ALOG2MEDIA_HAVE_CGL=1)
    set_source_files_properties(src/OffscreenContextCGL.cpp PROPERTIES
      COMPILE_OPTIONS "-Wno-deprecated-declarations")
  elseif(UNIX)
    target_sources(pmv_reference PRIVATE src/OffscreenContextEGL.cpp)
    target_compile_definitions(pmv_reference PRIVATE
      ALOG2MEDIA_HAVE_EGL=1
      GL_GLEXT_PROTOTYPES=1)
  endif()
  target_include_directories(pmv_reference PRIVATE
    src
    "${MOOS_IVP_ROOT}/ivp/src/pMarineViewer"
    "${MOOS_IVP_ROOT}/include/ivp"
    "${MOOS_GEODESY_INCLUDE_DIR}"
    ${FLTK_INCLUDE_DIR})
  target_compile_definitions(pmv_reference PRIVATE
    MOOSGEODESY_IMPL=MOOSGEODESY_IMPL_PROJ
    USE_UTM
    _USE_UNIQUE_PTR)
  target_compile_options(pmv_reference PRIVATE
    -Wall -Wextra -Wpedantic -Wno-unused-parameter
    -Wno-cast-function-type)
  target_link_libraries(pmv_reference PRIVATE
    ${IVP_LIBRARIES}
    "${IVP_GEODAID_LIBRARY}"
    MOOS::MOOS
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
    target_link_libraries(pmv_reference PRIVATE
      "${COCOA_FRAMEWORK}" OpenGL::GL)
  elseif(UNIX)
    target_link_libraries(pmv_reference PRIVATE
      OpenGL::OpenGL OpenGL::EGL)
  endif()

  find_program(FFMPEG_EXECUTABLE ffmpeg)
  find_program(FFPROBE_EXECUTABLE ffprobe)
  find_program(PYTHON3_EXECUTABLE python3)
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

  if((APPLE OR UNIX) AND FFMPEG_EXECUTABLE AND FFPROBE_EXECUTABLE AND
     PYTHON3_EXECUTABLE)
    add_test(NAME pmv_fidelity
      COMMAND ${CMAKE_COMMAND}
        -DALOG2MEDIA=$<TARGET_FILE:alog2media>
        -DPMV_REFERENCE=$<TARGET_FILE:pmv_reference>
        -DFIXTURE_MAP=$<TARGET_FILE:fixture_map>
        -DFFMPEG=${FFMPEG_EXECUTABLE}
        -DPYTHON=${PYTHON3_EXECUTABLE}
        -DFIXTURE_DIR=${PROJECT_SOURCE_DIR}/tests/fixtures
        -DPROOF_DIR=${PROJECT_SOURCE_DIR}/tests/proof
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/pmv-fidelity
        -P ${PROJECT_SOURCE_DIR}/tests/pmv_fidelity.cmake)
  endif()

  add_test(NAME install_smoke
    COMMAND ${CMAKE_COMMAND}
      -DBUILD_DIR=${CMAKE_CURRENT_BINARY_DIR}
      -DINSTALL_PREFIX=${CMAKE_CURRENT_BINARY_DIR}/install-smoke
      -DINSTALL_BINDIR=${CMAKE_INSTALL_BINDIR}
      -DINSTALL_DATADIR=${CMAKE_INSTALL_DATADIR}
      -P ${PROJECT_SOURCE_DIR}/tests/install_smoke.cmake)
endif()
