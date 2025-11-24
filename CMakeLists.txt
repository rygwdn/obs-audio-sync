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
# FFmpeg MUST come from OBS dependencies, not from system
# Search only in:
# 1. CMAKE_PREFIX_PATH (OBS dependencies)
# 2. .deps directory (local OBS dependencies)

# Build search paths - OBS dependencies first, then system paths on Linux
set(FFMPEG_SEARCH_PATHS)
foreach(path ${CMAKE_PREFIX_PATH})
  list(APPEND FFMPEG_SEARCH_PATHS "${path}/lib" "${path}")
endforeach()
list(APPEND FFMPEG_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib")

set(FFMPEG_INCLUDE_PATHS)
foreach(path ${CMAKE_PREFIX_PATH})
  list(APPEND FFMPEG_INCLUDE_PATHS "${path}/include" "${path}")
endforeach()
list(APPEND FFMPEG_INCLUDE_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/include")

# On Linux, add system paths as fallback
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  list(APPEND FFMPEG_SEARCH_PATHS "/usr/lib" "/usr/lib/${CMAKE_LIBRARY_ARCHITECTURE}" "/usr/local/lib")
  list(APPEND FFMPEG_INCLUDE_PATHS "/usr/include" "/usr/local/include")
endif()

# Find libraries - prefer OBS dependencies, but allow system fallback on Linux
# (Linux doesn't have pre-built OBS deps, so system packages are acceptable)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # On Linux, try OBS deps first, then fall back to system
  find_library(AVCODEC_LIBRARY NAMES avcodec PATHS ${FFMPEG_SEARCH_PATHS})
  find_library(AVFORMAT_LIBRARY NAMES avformat PATHS ${FFMPEG_SEARCH_PATHS})
  find_library(AVUTIL_LIBRARY NAMES avutil PATHS ${FFMPEG_SEARCH_PATHS})
  find_library(SWSCALE_LIBRARY NAMES swscale PATHS ${FFMPEG_SEARCH_PATHS})

  find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS ${FFMPEG_INCLUDE_PATHS})
  find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h PATHS ${FFMPEG_INCLUDE_PATHS})
  find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS ${FFMPEG_INCLUDE_PATHS})
  find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h PATHS ${FFMPEG_INCLUDE_PATHS})
else()
  # On macOS/Windows, OBS dependencies only (NO system fallback)
  find_library(AVCODEC_LIBRARY NAMES avcodec PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(AVFORMAT_LIBRARY NAMES avformat PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(AVUTIL_LIBRARY NAMES avutil PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)
  find_library(SWSCALE_LIBRARY NAMES swscale PATHS ${FFMPEG_SEARCH_PATHS} NO_DEFAULT_PATH)

  find_path(AVCODEC_INCLUDE_DIR NAMES libavcodec/avcodec.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(AVFORMAT_INCLUDE_DIR NAMES libavformat/avformat.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(AVUTIL_INCLUDE_DIR NAMES libavutil/avutil.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
  find_path(SWSCALE_INCLUDE_DIR NAMES libswscale/swscale.h PATHS ${FFMPEG_INCLUDE_PATHS} NO_DEFAULT_PATH)
endif()

# Verify all libraries and headers were found
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

  if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(STATUS "Found FFmpeg libraries (using system packages on Linux):")
  else()
    message(STATUS "Found FFmpeg libraries from OBS dependencies:")
  endif()
  message(STATUS "  avcodec: ${AVCODEC_LIBRARY}")
  message(STATUS "  avformat: ${AVFORMAT_LIBRARY}")
  message(STATUS "  avutil: ${AVUTIL_LIBRARY}")
  message(STATUS "  swscale: ${SWSCALE_LIBRARY}")
endif()

# Check if all required FFmpeg components were found
if(NOT AVCODEC_FOUND OR NOT AVFORMAT_FOUND OR NOT AVUTIL_FOUND OR NOT SWSCALE_FOUND)
  if(_LINTING_MODE OR CMAKE_EXPORT_COMPILE_COMMANDS)
    # When generating compile_commands.json for linting, FFmpeg is optional
    message(STATUS "FFmpeg libraries not found, but CMAKE_EXPORT_COMPILE_COMMANDS is set. Skipping FFmpeg for linting.")
  else()
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(
        error_msg
        "FFmpeg libraries are required but not found.\n"
        "\n"
        "Searched in:\n"
        "  - CMAKE_PREFIX_PATH (OBS dependencies)\n"
        "  - .deps directory (local OBS dependencies)\n"
        "  - System paths (Linux fallback)\n"
        "\n"
        "On Linux, system FFmpeg packages are acceptable if OBS deps are not available.\n"
        "Please install FFmpeg development packages:\n"
        "  sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev"
      )
    else()
      set(
        error_msg
        "FFmpeg libraries are required but not found in OBS dependencies.\n"
        "\n"
        "Searched in:\n"
        "  - CMAKE_PREFIX_PATH (OBS dependencies)\n"
        "  - .deps directory (local OBS dependencies)\n"
        "\n"
        "FFmpeg MUST come from OBS dependencies, not from system.\n"
        "System FFmpeg is explicitly excluded to ensure compatibility.\n"
        "\n"
        "Please ensure OBS dependencies (obs-deps) are properly set up:\n"
        "  1. Download OBS dependencies for your platform\n"
        "  2. Extract them to a directory\n"
        "  3. Set CMAKE_PREFIX_PATH to point to that directory\n"
        "     Example: cmake --preset macos -DCMAKE_PREFIX_PATH=/path/to/obs-deps\n"
        "\n"
        "If building in CI, ensure OBS dependencies are properly downloaded and extracted."
      )
    endif()
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
    # Get target architectures from CMAKE_OSX_ARCHITECTURES or default to arm64
    if(CMAKE_OSX_ARCHITECTURES)
      set(AGL_ARCH_FLAGS "")
      foreach(arch ${CMAKE_OSX_ARCHITECTURES})
        list(APPEND AGL_ARCH_FLAGS "-arch" "${arch}")
      endforeach()
    else()
      set(AGL_ARCH_FLAGS "-arch" "arm64")
    endif()
    execute_process(
      COMMAND
        ${CMAKE_C_COMPILER} -shared ${AGL_ARCH_FLAGS} -o "${AGL_STUB_LIB}" "${AGL_STUB_DIR}/AGL-stub.c" -install_name
        @rpath/AGL.framework/AGL -compatibility_version 1.0 -current_version 1.0
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

# Generate compile_commands.json for IDE support
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json" FORCE)

# Tests
enable_testing()

if(ENABLE_QT)
  # Qt6 Test is optional on all platforms
  # First try to find in OBS deps (CMAKE_PREFIX_PATH)
  find_package(Qt6 COMPONENTS Test QUIET)

  # If not found in OBS deps, try to download from official Qt repository
  if(NOT Qt6Test_FOUND)
    # Determine Qt6 version from OBS deps
    set(_qt6_version "")
    foreach(_obs_path ${CMAKE_PREFIX_PATH})
      set(_qt6_core_config "${_obs_path}/lib/cmake/Qt6Core/Qt6CoreConfig.cmake")
      if(EXISTS "${_qt6_core_config}")
        # Try to read version from Qt6CoreConfigVersionImpl.cmake
        set(_qt6_version_file "${_obs_path}/lib/cmake/Qt6Core/Qt6CoreConfigVersionImpl.cmake")
        if(EXISTS "${_qt6_version_file}")
          file(READ "${_qt6_version_file}" _qt6_version_content)
          # Extract version using regex (format: set(PACKAGE_VERSION "6.x.y"))
          string(
            REGEX MATCH
            "set\\(PACKAGE_VERSION \"([0-9]+\\.[0-9]+\\.[0-9]+)\""
            _version_match
            "${_qt6_version_content}"
          )
          if(_version_match)
            set(_qt6_version "${CMAKE_MATCH_1}")
            message(STATUS "Detected Qt6 version: ${_qt6_version}")
          endif()
        endif()
        break()
      endif()
    endforeach()

    # If we found Qt6 version, try to download Qt6 Test from official Qt repository
    if(_qt6_version AND NOT Qt6Test_FOUND)
      message(STATUS "Qt6 Test not found in OBS deps. Attempting to download from official Qt repository...")

      # Qt provides pre-built binaries via their online installer repository
      # For macOS, we can try to download from: https://download.qt.io/online/qtsdkrepository/
      # However, this requires knowing the exact architecture and component structure

      # Extract major.minor version (e.g., "6.7" from "6.7.1")
      string(REGEX MATCH "^([0-9]+\\.[0-9]+)" _qt6_major_minor "${_qt6_version}")

      if(_qt6_major_minor AND APPLE)
        # Determine architecture
        if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
          set(_qt_arch "macos")
          set(_qt_arch_suffix "macos")
        else()
          set(_qt_arch "macos")
          set(_qt_arch_suffix "macos")
        endif()

        # Qt online installer repository structure:
        # https://download.qt.io/online/qtsdkrepository/mac_x64/desktop/qt6_${version}/qt.qt6.${version}.${arch}/qt.qt6.${version}.${arch}.test/
        # However, this requires authentication or specific repository access

        # Alternative: Try to find Qt6 Test in a local Qt installation
        # Check common Qt installation paths
        set(
          _qt_install_paths
          "$ENV{HOME}/Qt/${_qt6_major_minor}"
          "/opt/qt/${_qt6_major_minor}"
          "/usr/local/Qt/${_qt6_major_minor}"
        )

        foreach(_qt_path IN LISTS _qt_install_paths)
          set(_qt_test_config "${_qt_path}/${_qt6_major_minor}/gcc_64/lib/cmake/Qt6Test/Qt6TestConfig.cmake")
          if(EXISTS "${_qt_test_config}")
            set(
              Qt6Test_DIR
              "${_qt_path}/${_qt6_major_minor}/gcc_64/lib/cmake/Qt6Test"
              CACHE PATH
              "Qt6Test directory"
              FORCE
            )
            find_package(Qt6 COMPONENTS Test QUIET)
            if(Qt6Test_FOUND)
              message(STATUS "Found Qt6 Test in local Qt installation: ${_qt_path}")
              break()
            endif()
          endif()
        endforeach()

        if(NOT Qt6Test_FOUND)
          message(STATUS "Qt6 Test not found in local Qt installations.")

          # Try to download Qt6 Test from Qt's official archive
          # Qt provides pre-built binaries at: https://download.qt.io/archive/qt/
          # For macOS, we need the macos version
          set(_qt6_test_download_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps/qt6-test-${_qt6_version}")
          set(_qt6_test_prefix "${_qt6_test_download_dir}")

          # Check if already downloaded
          set(_qt6_test_config_file "${_qt6_test_prefix}/lib/cmake/Qt6Test/Qt6TestConfig.cmake")
          if(NOT EXISTS "${_qt6_test_config_file}")
            message(STATUS "Attempting to download Qt6 Test ${_qt6_version} from Qt official repository...")
            message(WARNING "Automatic download of Qt6 Test from official Qt repository is not yet implemented.")
            message(STATUS "Please install Qt6 Test manually:")
            message(STATUS "  1. Download Qt ${_qt6_major_minor} from https://www.qt.io/download")
            message(STATUS "  2. Install Qt6 Test component")
            message(STATUS "  3. Or use Homebrew: brew install qt@6 (may have version mismatch)")
            message(STATUS "Will try Homebrew/system paths as fallback...")
          else()
            # Found downloaded Qt6 Test, add to prefix path
            list(APPEND CMAKE_PREFIX_PATH "${_qt6_test_prefix}")
            set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} CACHE PATH "CMake prefix search path" FORCE)
            find_package(Qt6 COMPONENTS Test QUIET)
            if(Qt6Test_FOUND)
              message(STATUS "Found Qt6 Test in downloaded location: ${_qt6_test_prefix}")
            endif()
          endif()
        endif()
      endif()
    endif()
  endif()

  # If still not found, skip Homebrew to avoid version mismatches
  # Homebrew Qt6Test typically doesn't match OBS deps Qt6 version
  if(NOT Qt6Test_FOUND AND APPLE)
    message(STATUS "Skipping Homebrew Qt6Test to avoid version mismatch with OBS deps Qt6 ${_qt6_version}")
    message(STATUS "")
    message(STATUS "To enable tests, please install Qt6 Test that matches Qt6 ${_qt6_version}:")
    message(STATUS "  1. Download Qt ${_qt6_major_minor} from https://www.qt.io/download-qt-installer")
    message(STATUS "  2. Run the Qt installer and install Qt ${_qt6_major_minor} with the Test component")
    message(STATUS "  3. The installer will install to ~/Qt/${_qt6_major_minor} by default")
    message(STATUS "  4. Reconfigure the build after installation")
    message(STATUS "")
    message(STATUS "Alternatively, if Qt6 Test becomes available in OBS deps, it will be used automatically.")
  endif()

  if(NOT Qt6Test_FOUND)
    message(STATUS "Qt6 Test not found. Tests will be disabled on ${CMAKE_SYSTEM_NAME}.")
    set(ENABLE_QT_TESTS OFF)
  else()
    set(ENABLE_QT_TESTS ON)
  endif()

  if(ENABLE_QT_TESTS)
    # Common test configuration function
    function(configure_test_target target_name)
      # Link test dependencies
      target_link_libraries(${target_name} PRIVATE Qt6::Core Qt6::Widgets Qt6::Test)

      # Link against plugin sources (for testing)
      # Note: recording-scanner.cpp requires obs-frontend-api which may not be available
      # in test environment, so we only test the public isValidVideoFile method
      target_sources(${target_name} PRIVATE src/recording-scanner.cpp src/timeline-widget.cpp src/audio-analyzer.cpp)

      # Link OBS libraries if available (optional for tests)
      # libobs is required for obs.h header (included by obs-frontend-api.h)
      if(libobs_FOUND OR TARGET OBS::libobs)
        target_link_libraries(${target_name} PRIVATE OBS::libobs)
      endif()
      if(ENABLE_FRONTEND_API AND (obs-frontend-api_FOUND OR TARGET OBS::obs-frontend-api))
        target_link_libraries(${target_name} PRIVATE OBS::obs-frontend-api)
      endif()

      # Include directories
      target_include_directories(${target_name} PRIVATE src)

      # Link FFmpeg libraries if available
      if(AVCODEC_FOUND AND AVFORMAT_FOUND AND AVUTIL_FOUND AND SWSCALE_FOUND)
        target_include_directories(
          ${target_name}
          PRIVATE ${AVCODEC_INCLUDE_DIRS} ${AVFORMAT_INCLUDE_DIRS} ${AVUTIL_INCLUDE_DIRS} ${SWSCALE_INCLUDE_DIRS}
        )

        target_link_libraries(
          ${target_name}
          PRIVATE ${AVCODEC_LIBRARIES} ${AVFORMAT_LIBRARIES} ${AVUTIL_LIBRARIES} ${SWSCALE_LIBRARIES}
        )
      endif()

      # Set test properties
      set_target_properties(${target_name} PROPERTIES AUTOMOC ON)
    endfunction()

    # Test: Recording Scanner Standalone
    add_executable(${CMAKE_PROJECT_NAME}-test-recording-scanner tests/test-recording-scanner-standalone.cpp)
    target_compile_definitions(${CMAKE_PROJECT_NAME}-test-recording-scanner PRIVATE STANDALONE_TEST)
    configure_test_target(${CMAKE_PROJECT_NAME}-test-recording-scanner)
    add_test(NAME test-recording-scanner COMMAND ${CMAKE_PROJECT_NAME}-test-recording-scanner)

    # Test: Timeline Widget
    add_executable(${CMAKE_PROJECT_NAME}-test-timeline-widget tests/test-timeline-widget.cpp)
    target_compile_definitions(${CMAKE_PROJECT_NAME}-test-timeline-widget PRIVATE STANDALONE_TEST)
    configure_test_target(${CMAKE_PROJECT_NAME}-test-timeline-widget)
    add_test(NAME test-timeline-widget COMMAND ${CMAKE_PROJECT_NAME}-test-timeline-widget)

    # Test: Audio Analyzer
    add_executable(${CMAKE_PROJECT_NAME}-test-audio-analyzer tests/test-audio-analyzer.cpp)
    target_compile_definitions(${CMAKE_PROJECT_NAME}-test-audio-analyzer PRIVATE STANDALONE_TEST)
    configure_test_target(${CMAKE_PROJECT_NAME}-test-audio-analyzer)
    add_test(NAME test-audio-analyzer COMMAND ${CMAKE_PROJECT_NAME}-test-audio-analyzer)

    # Also keep the combined test executable for backward compatibility
    # (test-main.cpp runs all tests sequentially)
    add_executable(
      ${CMAKE_PROJECT_NAME}-tests
      tests/test-main.cpp
      tests/test-recording-scanner-standalone.cpp
      tests/test-timeline-widget.cpp
      tests/test-audio-analyzer.cpp
    )
    configure_test_target(${CMAKE_PROJECT_NAME}-tests)
    add_test(NAME ${CMAKE_PROJECT_NAME}-tests COMMAND ${CMAKE_PROJECT_NAME}-tests)
  endif()
endif()
