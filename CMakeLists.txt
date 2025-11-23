cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

# Check if we're generating compile_commands.json for linting
# When passed via -DCMAKE_EXPORT_COMPILE_COMMANDS=ON, it's available as a cache variable
# Use get_property for compatibility with CMake 3.28+
get_property(_export_compile_commands_set CACHE CMAKE_EXPORT_COMPILE_COMMANDS PROPERTY VALUE SET)
if(_export_compile_commands_set)
  get_property(_export_compile_commands CACHE CMAKE_EXPORT_COMPILE_COMMANDS PROPERTY VALUE)
  if(_export_compile_commands)
    set(_LINTING_MODE ON)
  endif()
elseif(DEFINED CMAKE_EXPORT_COMPILE_COMMANDS AND CMAKE_EXPORT_COMPILE_COMMANDS)
  set(_LINTING_MODE ON)
endif()

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

# When generating compile_commands.json for linting, make dependencies optional
if(_LINTING_MODE OR CMAKE_EXPORT_COMPILE_COMMANDS)
  find_package(libobs QUIET)
  if(libobs_FOUND OR TARGET OBS::libobs)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)
  endif()

  if(ENABLE_FRONTEND_API)
    find_package(obs-frontend-api QUIET)
    if(obs-frontend-api_FOUND OR TARGET OBS::obs-frontend-api)
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
    endif()
  endif()

  if(ENABLE_QT)
    find_package(Qt6 QUIET COMPONENTS Widgets Core)
    if(Qt6_FOUND)
      target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
    endif()
  endif()
else()
  find_package(libobs REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

  if(ENABLE_FRONTEND_API)
    find_package(obs-frontend-api REQUIRED)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
  endif()

  if(ENABLE_QT)
    find_package(Qt6 COMPONENTS Widgets Core)
    target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
  endif()
endif()

if(ENABLE_QT AND (Qt6_FOUND OR _LINTING_MODE OR NOT CMAKE_EXPORT_COMPILE_COMMANDS))
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

# Suppress deprecation warnings from OBS headers
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wno-deprecated-declarations)

# FFmpeg/libav
# FFmpeg is required for audio/video analysis
# Try multiple methods to find FFmpeg:
# 1. Through pkg-config (if available)
# 2. Through CMAKE_PREFIX_PATH (OBS dependencies)
# 3. Through system paths

find_package(PkgConfig)
if(PkgConfig_FOUND)
  pkg_check_modules(AVCODEC libavcodec)
  pkg_check_modules(AVFORMAT libavformat)
  pkg_check_modules(AVUTIL libavutil)
  pkg_check_modules(SWSCALE libswscale)
endif()

# If pkg-config didn't find them, try finding libraries directly
# Check CMAKE_PREFIX_PATH first (OBS dependencies), then system paths
if(NOT AVCODEC_FOUND)
  # Build search paths from CMAKE_PREFIX_PATH
  set(FFMPEG_SEARCH_PATHS)
  foreach(path ${CMAKE_PREFIX_PATH})
    list(APPEND FFMPEG_SEARCH_PATHS "${path}/lib" "${path}")
  endforeach()
  list(APPEND FFMPEG_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib" /usr/local/lib /opt/homebrew/lib /usr/lib)

  set(FFMPEG_INCLUDE_PATHS)
  foreach(path ${CMAKE_PREFIX_PATH})
    list(APPEND FFMPEG_INCLUDE_PATHS "${path}/include" "${path}")
  endforeach()
  list(
    APPEND
    FFMPEG_INCLUDE_PATHS
    "${CMAKE_CURRENT_SOURCE_DIR}/.deps/include"
    /usr/local/include
    /opt/homebrew/include
    /usr/include
  )

  find_library(AVCODEC_LIBRARY NAMES avcodec PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(AVCODEC_LIBRARY NAMES avcodec PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)

  find_library(AVFORMAT_LIBRARY NAMES avformat PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(AVFORMAT_LIBRARY NAMES avformat PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)

  find_library(AVUTIL_LIBRARY NAMES avutil PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(AVUTIL_LIBRARY NAMES avutil PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)

  find_library(SWSCALE_LIBRARY NAMES swscale PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(SWSCALE_LIBRARY NAMES swscale PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)

  find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS /usr/local/include /opt/homebrew/include /usr/include)

  find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(
    AVFORMAT_INCLUDE_DIR
    NAMES libavformat/avformat.h
    PATHS /usr/local/include /opt/homebrew/include /usr/include
  )

  find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS /usr/local/include /opt/homebrew/include /usr/include)

  find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h PATHS /usr/local/include /opt/homebrew/include /usr/include)

  if(
    AVCODEC_LIBRARY
    AND AVFORMAT_LIBRARY
    AND AVUTIL_LIBRARY
    AND SWSCALE_LIBRARY
    AND AVCODEC_INCLUDE_DIR
    AND AVFORMAT_INCLUDE_DIR
    AND AVUTIL_INCLUDE_DIR
    AND SWSCALE_INCLUDE_DIR
  )
    set(AVCODEC_FOUND TRUE)
    set(AVFORMAT_FOUND TRUE)
    set(AVUTIL_FOUND TRUE)
    set(SWSCALE_FOUND TRUE)
    set(AVCODEC_LIBRARIES ${AVCODEC_LIBRARY})
    set(AVFORMAT_LIBRARIES ${AVFORMAT_LIBRARY})
    set(AVUTIL_LIBRARIES ${AVUTIL_LIBRARY})
    set(SWSCALE_LIBRARIES ${SWSCALE_LIBRARY})
    set(AVCODEC_INCLUDE_DIRS ${AVCODEC_INCLUDE_DIR})
    set(AVFORMAT_INCLUDE_DIRS ${AVFORMAT_INCLUDE_DIR})
    set(AVUTIL_INCLUDE_DIRS ${AVUTIL_INCLUDE_DIR})
    set(SWSCALE_INCLUDE_DIRS ${SWSCALE_INCLUDE_DIR})
  endif()
endif()

# Check if all required FFmpeg components were found
if(NOT AVCODEC_FOUND OR NOT AVFORMAT_FOUND OR NOT AVUTIL_FOUND OR NOT SWSCALE_FOUND)
  if(_LINTING_MODE OR CMAKE_EXPORT_COMPILE_COMMANDS)
    # When generating compile_commands.json for linting, FFmpeg is optional
    message(STATUS "FFmpeg libraries not found, but CMAKE_EXPORT_COMPILE_COMMANDS is set. Skipping FFmpeg for linting.")
  else()
    message(
      FATAL_ERROR
      "FFmpeg libraries are required but not found.\n"
      "Searched in:\n"
      "  - pkg-config\n"
      "  - CMAKE_PREFIX_PATH (OBS dependencies)\n"
      "  - System paths\n"
      "\n"
      "Please install FFmpeg development libraries:\n"
      "  macOS: brew install ffmpeg\n"
      "  Ubuntu: sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev\n"
      "  Windows: Install FFmpeg and set CMAKE_PREFIX_PATH\n"
      "\n"
      "Note: FFmpeg should be available through OBS dependencies (obs-deps).\n"
      "If building in CI, ensure OBS dependencies are properly downloaded and extracted."
    )
  endif()
endif()

if(AVCODEC_FOUND AND AVFORMAT_FOUND AND AVUTIL_FOUND AND SWSCALE_FOUND)
  message(STATUS "Found FFmpeg libraries")
  target_include_directories(
    ${CMAKE_PROJECT_NAME}
    PRIVATE ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS}
  )

  # Add library directories to linker search path
  # Handle pkg-config case (LIBRARY_DIRS) or direct find case (LIBRARY)
  if(AVCODEC_LIBRARY_DIRS)
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${AVCODEC_LIBRARY_DIRS})
  elseif(AVCODEC_LIBRARY)
    get_filename_component(AVCODEC_LIB_DIR ${AVCODEC_LIBRARY} DIRECTORY)
    target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE ${AVCODEC_LIB_DIR})
  endif()

  target_link_libraries(
    ${CMAKE_PROJECT_NAME}
    PRIVATE ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES}
  )
endif()

# AGL framework is deprecated and not available on newer macOS
# Create a stub framework and add it to the search path
if(APPLE)
  # Create stub AGL framework if it doesn't exist
  set(AGL_STUB_DIR "${CMAKE_CURRENT_BINARY_DIR}/AGL.framework")
  set(AGL_STUB_LIB "${AGL_STUB_DIR}/AGL")
  if(NOT EXISTS "${AGL_STUB_LIB}")
    file(MAKE_DIRECTORY "${AGL_STUB_DIR}")
    file(
      WRITE
      "${AGL_STUB_DIR}/AGL-stub.c"
      "// Minimal stub for deprecated AGL framework\n__attribute__((visibility(\"default\"))) void AGLStub() {}\n"
    )
    execute_process(
      COMMAND
        ${CMAKE_C_COMPILER} -shared -arch arm64 -arch x86_64 -o "${AGL_STUB_LIB}" "${AGL_STUB_DIR}/AGL-stub.c"
        -install_name @rpath/AGL.framework/AGL -compatibility_version 1.0 -current_version 1.0
      RESULT_VARIABLE AGL_STUB_RESULT
    )
    if(NOT AGL_STUB_RESULT EQUAL 0)
      message(WARNING "Failed to create AGL stub framework")
    endif()
  endif()
  # Add stub framework to search path
  target_link_options(${CMAKE_PROJECT_NAME} PRIVATE "-F${CMAKE_CURRENT_BINARY_DIR}")
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE
    src/plugin-main.cpp
    src/audio-sync-panel.cpp
    src/recording-scanner.cpp
    src/audio-analyzer.cpp
    src/timeline-widget.cpp
    src/video-extractor.cpp
)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Generate compile_commands.json for clang-tidy
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json" FORCE)

# Tests
enable_testing()

if(ENABLE_QT)
  # Qt6 Test is optional on all platforms
  find_package(Qt6 COMPONENTS Test)
  if(NOT Qt6Test_FOUND)
    message(STATUS "Qt6 Test not found. Tests will be disabled on ${CMAKE_SYSTEM_NAME}.")
    set(ENABLE_QT_TESTS OFF)
  else()
    set(ENABLE_QT_TESTS ON)
  endif()

  if(ENABLE_QT_TESTS)
    # Test executable
    # test-main.cpp is the entry point, other test files provide test classes
    add_executable(
      ${CMAKE_PROJECT_NAME}-tests
      tests/test-main.cpp
      tests/test-recording-scanner-standalone.cpp
      tests/test-timeline-widget.cpp
      tests/test-audio-analyzer.cpp
    )

    # Link test dependencies
    target_link_libraries(${CMAKE_PROJECT_NAME}-tests PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)

    # Link against plugin sources (for testing)
    # Note: recording-scanner.cpp requires obs-frontend-api which may not be available
    # in test environment, so we only test the public isValidVideoFile method
    target_sources(
      ${CMAKE_PROJECT_NAME}-tests
      PRIVATE src/recording-scanner.cpp src/timeline-widget.cpp src/audio-analyzer.cpp
    )

    # Link OBS libraries if available (optional for tests)
    if(ENABLE_FRONTEND_API)
      target_link_libraries(${CMAKE_PROJECT_NAME}-tests PRIVATE OBS::obs-frontend-api)
    endif()

    # Include directories
    target_include_directories(${CMAKE_PROJECT_NAME}-tests PRIVATE src)

    # Link FFmpeg libraries if available
    if(AVCODEC_FOUND AND AVFORMAT_FOUND AND AVUTIL_FOUND AND SWSCALE_FOUND)
      target_include_directories(
        ${CMAKE_PROJECT_NAME}-tests
        PRIVATE ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS}
      )

      target_link_libraries(
        ${CMAKE_PROJECT_NAME}-tests
        PRIVATE ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES}
      )
    endif()

    # Set test properties
    set_target_properties(${CMAKE_PROJECT_NAME}-tests PROPERTIES AUTOMOC ON)

    # Add test
    add_test(NAME ${CMAKE_PROJECT_NAME}-tests COMMAND ${CMAKE_PROJECT_NAME}-tests)
  endif()
endif()
