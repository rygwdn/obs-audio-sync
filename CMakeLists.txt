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
# Only apply to GCC/Clang (MSVC doesn't support -Wno flags)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wno-deprecated-declarations>
)

# FFmpeg/libav
# FFmpeg is required for audio/video analysis
# Try multiple methods to find FFmpeg:
# 1. Through CMAKE_PREFIX_PATH (OBS dependencies) - prioritized for universal builds
# 2. Through pkg-config (if available and not building universal)
# 3. Through system paths (only if not building universal)

# Check if building universal binary
set(IS_UNIVERSAL_BUILD FALSE)
if(APPLE AND CMAKE_OSX_ARCHITECTURES)
  list(LENGTH CMAKE_OSX_ARCHITECTURES arch_count)
  if(arch_count GREATER 1)
    set(IS_UNIVERSAL_BUILD TRUE)
    message(STATUS "Building universal binary - prioritizing OBS dependencies for FFmpeg")
  endif()
endif()

# Helper function to check if a library is universal (macOS only)
function(check_library_architectures library_path result_var)
  set(${result_var} FALSE PARENT_SCOPE)
  if(APPLE AND EXISTS "${library_path}")
    execute_process(
      COMMAND lipo -info "${library_path}"
      OUTPUT_VARIABLE lipo_output
      ERROR_VARIABLE lipo_error
      RESULT_VARIABLE lipo_result
    )
    if(lipo_result EQUAL 0)
      # Check if output contains multiple architectures
      string(FIND "${lipo_output}" "arm64" has_arm64)
      string(FIND "${lipo_output}" "x86_64" has_x86_64)
      if(has_arm64 GREATER_EQUAL 0 AND has_x86_64 GREATER_EQUAL 0)
        set(${result_var} TRUE PARENT_SCOPE)
      endif()
    endif()
  endif()
endfunction()

# Build search paths - prioritize CMAKE_PREFIX_PATH (OBS dependencies)
set(FFMPEG_SEARCH_PATHS)
foreach(path ${CMAKE_PREFIX_PATH})
  list(APPEND FFMPEG_SEARCH_PATHS "${path}/lib" "${path}")
endforeach()
list(APPEND FFMPEG_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib")

# Only add system paths if not building universal (they're usually single-arch)
if(NOT IS_UNIVERSAL_BUILD)
  list(APPEND FFMPEG_SEARCH_PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)
endif()

set(FFMPEG_INCLUDE_PATHS)
foreach(path ${CMAKE_PREFIX_PATH})
  list(APPEND FFMPEG_INCLUDE_PATHS "${path}/include" "${path}")
endforeach()
list(APPEND FFMPEG_INCLUDE_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/include")

# Only add system include paths if not building universal
if(NOT IS_UNIVERSAL_BUILD)
  list(APPEND FFMPEG_INCLUDE_PATHS /usr/local/include /opt/homebrew/include /usr/include)
endif()

# Try pkg-config only if not building universal (pkg-config finds system libs)
if(NOT IS_UNIVERSAL_BUILD)
  find_package(PkgConfig)
  if(PkgConfig_FOUND)
    pkg_check_modules(AVCODEC libavcodec)
    pkg_check_modules(AVFORMAT libavformat)
    pkg_check_modules(AVUTIL libavutil)
    pkg_check_modules(SWSCALE libswscale)
  endif()
endif()

# Find libraries directly - prioritize CMAKE_PREFIX_PATH
if(NOT AVCODEC_FOUND)
  # Search in CMAKE_PREFIX_PATH first (OBS dependencies)
  find_library(AVCODEC_LIBRARY NAMES avcodec PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)

  # Only search system paths if not building universal and not found in OBS deps
  if(NOT AVCODEC_LIBRARY AND NOT IS_UNIVERSAL_BUILD)
    find_library(AVCODEC_LIBRARY NAMES avcodec PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)
  endif()

  find_library(AVFORMAT_LIBRARY NAMES avformat PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  if(NOT AVFORMAT_LIBRARY AND NOT IS_UNIVERSAL_BUILD)
    find_library(AVFORMAT_LIBRARY NAMES avformat PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)
  endif()

  find_library(AVUTIL_LIBRARY NAMES avutil PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  if(NOT AVUTIL_LIBRARY AND NOT IS_UNIVERSAL_BUILD)
    find_library(AVUTIL_LIBRARY NAMES avutil PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)
  endif()

  find_library(SWSCALE_LIBRARY NAMES swscale PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  if(NOT SWSCALE_LIBRARY AND NOT IS_UNIVERSAL_BUILD)
    find_library(SWSCALE_LIBRARY NAMES swscale PATHS /usr/local/lib /opt/homebrew/lib /usr/lib)
  endif()

  find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  if(NOT AVCODEC_INCLUDE_DIR AND NOT IS_UNIVERSAL_BUILD)
    find_path(
      AVCODEC_INCLUDE_DIR
      NAMES libavcodec/avcodec.h
      PATHS /usr/local/include /opt/homebrew/include /usr/include
    )
  endif()

  find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  if(NOT AVFORMAT_INCLUDE_DIR AND NOT IS_UNIVERSAL_BUILD)
    find_path(
      AVFORMAT_INCLUDE_DIR
      NAMES libavformat/avformat.h
      PATHS /usr/local/include /opt/homebrew/include /usr/include
    )
  endif()

  find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  if(NOT AVUTIL_INCLUDE_DIR AND NOT IS_UNIVERSAL_BUILD)
    find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS /usr/local/include /opt/homebrew/include /usr/include)
  endif()

  find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  if(NOT SWSCALE_INCLUDE_DIR AND NOT IS_UNIVERSAL_BUILD)
    find_path(
      SWSCALE_INCLUDE_DIR
      NAMES libswscale/swscale.h
      PATHS /usr/local/include /opt/homebrew/include /usr/include
    )
  endif()

  # Verify libraries are universal if building universal
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
    set(libs_ok TRUE)
    if(IS_UNIVERSAL_BUILD)
      # Check each library is universal
      check_library_architectures("${AVCODEC_LIBRARY}" avcodec_universal)
      check_library_architectures("${AVFORMAT_LIBRARY}" avformat_universal)
      check_library_architectures("${AVUTIL_LIBRARY}" avutil_universal)
      check_library_architectures("${SWSCALE_LIBRARY}" swscale_universal)

      if(NOT avcodec_universal OR NOT avformat_universal OR NOT avutil_universal OR NOT swscale_universal)
        set(libs_ok FALSE)
        # Save library paths for error message before clearing
        set(non_universal_avcodec ${AVCODEC_LIBRARY})
        set(non_universal_avformat ${AVFORMAT_LIBRARY})
        set(non_universal_avutil ${AVUTIL_LIBRARY})
        set(non_universal_swscale ${SWSCALE_LIBRARY})
        # Clear the found libraries so error message is shown
        unset(AVCODEC_LIBRARY)
        unset(AVFORMAT_LIBRARY)
        unset(AVUTIL_LIBRARY)
        unset(SWSCALE_LIBRARY)
        message(
          WARNING
          "FFmpeg libraries found but not universal. "
          "When building universal binaries, FFmpeg must be universal.\n"
          "Found libraries:\n"
          "  avcodec: ${non_universal_avcodec}\n"
          "  avformat: ${non_universal_avformat}\n"
          "  avutil: ${non_universal_avutil}\n"
          "  swscale: ${non_universal_swscale}\n"
          "\n"
          "Please use OBS dependencies (obs-deps) which provide universal libraries."
        )
      endif()
    endif()

    if(libs_ok)
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
endif()

# Check if all required FFmpeg components were found
if(NOT AVCODEC_FOUND OR NOT AVFORMAT_FOUND OR NOT AVUTIL_FOUND OR NOT SWSCALE_FOUND)
  if(_LINTING_MODE OR CMAKE_EXPORT_COMPILE_COMMANDS)
    # When generating compile_commands.json for linting, FFmpeg is optional
    message(STATUS "FFmpeg libraries not found, but CMAKE_EXPORT_COMPILE_COMMANDS is set. Skipping FFmpeg for linting.")
  else()
    set(
      error_msg
      "FFmpeg libraries are required but not found.\n"
      "Searched in:\n"
      "  - CMAKE_PREFIX_PATH (OBS dependencies)\n"
    )
    if(NOT IS_UNIVERSAL_BUILD)
      string(APPEND error_msg "  - pkg-config\n" "  - System paths\n")
    endif()
    string(APPEND error_msg "\n" "Please install FFmpeg development libraries:\n")
    if(IS_UNIVERSAL_BUILD)
      string(
        APPEND
        error_msg
        "  macOS (universal): Use OBS dependencies (obs-deps) which provide universal libraries.\n"
        "    System FFmpeg (Homebrew) is arm64-only and cannot be used for universal builds.\n"
      )
    else()
      string(APPEND error_msg "  macOS: brew install ffmpeg\n")
    endif()
    string(
      APPEND
      error_msg
      "  Ubuntu: sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev\n"
      "  Windows: Install FFmpeg and set CMAKE_PREFIX_PATH\n"
      "\n"
      "Note: FFmpeg should be available through OBS dependencies (obs-deps).\n"
      "If building in CI, ensure OBS dependencies are properly downloaded and extracted."
    )
    message(FATAL_ERROR ${error_msg})
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
