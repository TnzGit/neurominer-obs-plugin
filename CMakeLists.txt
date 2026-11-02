cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# Require C++17
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  if(APPLE AND NOT TARGET WrapOpenGL::WrapOpenGL)
    # Provide WrapOpenGL without legacy AGL (removed on newer macOS SDKs).
    add_library(WrapOpenGL::WrapOpenGL INTERFACE IMPORTED)
    set(WrapOpenGL_FOUND ON)
    target_link_libraries(WrapOpenGL::WrapOpenGL INTERFACE "-framework OpenGL")
  endif()
  find_package(Qt6 COMPONENTS Widgets Core)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# --- Common sources ---
target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/recording-manager.h
    src/recording-manager.cpp
    src/input-state.h
    src/input-capturer.h
    src/dock-widget.h
    src/dock-widget.cpp
    src/key-names.h
)

# --- Platform-specific input capturer ---
if(APPLE)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/input-capturer-macos.h src/input-capturer-macos.cpp)
  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE "-framework CoreGraphics" "-framework CoreFoundation" "-framework ApplicationServices"
  )
elseif(WIN32)
  target_sources(${CMAKE_PROJECT_NAME} PRIVATE src/input-capturer-win.h src/input-capturer-win.cpp)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE user32)

  # FFmpeg's installed demuxer is the authority for video packet PTS.
  # OBS ships these DLLs; use the same OBS SDK dependency tree as the plugin.
  string(JSON _obs_source_version GET ${buildspec} dependencies obs-studio version)
  list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/.deps/obs-studio-${_obs_source_version}/cmake/finders")
  find_package(FFmpeg REQUIRED COMPONENTS avformat avcodec avutil)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE FFmpeg::avformat FFmpeg::avcodec FFmpeg::avutil)
elseif(UNIX)
  message(STATUS "Linux build temporarily disabled for this plugin.")
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Optional standalone keyboard diagnostic. Does not run inside OBS, does not
# register a raw keyboard target in OBS, and does not load automatically.
if(WIN32)
  add_executable(neurominer-raw-input-probe tools/raw-keyboard-probe.cpp)
  target_compile_features(neurominer-raw-input-probe PRIVATE cxx_std_17)
  target_link_libraries(neurominer-raw-input-probe PRIVATE user32)
endif()

