cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
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
	list(APPEND FFMPEG_SEARCH_PATHS
		"${CMAKE_CURRENT_SOURCE_DIR}/.deps/lib"
		/usr/local/lib
		/opt/homebrew/lib
		/usr/lib
	)
	
	set(FFMPEG_INCLUDE_PATHS)
	foreach(path ${CMAKE_PREFIX_PATH})
		list(APPEND FFMPEG_INCLUDE_PATHS "${path}/include" "${path}")
	endforeach()
	list(APPEND FFMPEG_INCLUDE_PATHS
		"${CMAKE_CURRENT_SOURCE_DIR}/.deps/include"
		/usr/local/include
		/opt/homebrew/include
		/usr/include
	)

	find_library(AVCODEC_LIBRARY
		NAMES avcodec
		PATHS ${FFMPEG_SEARCH_PATHS}
		NO_DEFAULT_PATH
	)
	find_library(AVCODEC_LIBRARY
		NAMES avcodec
		PATHS
			/usr/local/lib
			/opt/homebrew/lib
			/usr/lib
	)

	find_library(AVFORMAT_LIBRARY
		NAMES avformat
		PATHS ${FFMPEG_SEARCH_PATHS}
		NO_DEFAULT_PATH
	)
	find_library(AVFORMAT_LIBRARY
		NAMES avformat
		PATHS
			/usr/local/lib
			/opt/homebrew/lib
			/usr/lib
	)

	find_library(AVUTIL_LIBRARY
		NAMES avutil
		PATHS ${FFMPEG_SEARCH_PATHS}
		NO_DEFAULT_PATH
	)
	find_library(AVUTIL_LIBRARY
		NAMES avutil
		PATHS
			/usr/local/lib
			/opt/homebrew/lib
			/usr/lib
	)

	find_library(SWSCALE_LIBRARY
		NAMES swscale
		PATHS ${FFMPEG_SEARCH_PATHS}
		NO_DEFAULT_PATH
	)
	find_library(SWSCALE_LIBRARY
		NAMES swscale
		PATHS
			/usr/local/lib
			/opt/homebrew/lib
			/usr/lib
	)

	find_path(AVCODEC_INCLUDE_DIR
		NAMES libavcodec/avcodec.h
		PATHS ${FFMPEG_INCLUDE_PATHS}
		NO_DEFAULT_PATH
	)
	find_path(AVCODEC_INCLUDE_DIR
		NAMES libavcodec/avcodec.h
		PATHS
			/usr/local/include
			/opt/homebrew/include
			/usr/include
	)

	find_path(AVFORMAT_INCLUDE_DIR
		NAMES libavformat/avformat.h
		PATHS ${FFMPEG_INCLUDE_PATHS}
		NO_DEFAULT_PATH
	)
	find_path(AVFORMAT_INCLUDE_DIR
		NAMES libavformat/avformat.h
		PATHS
			/usr/local/include
			/opt/homebrew/include
			/usr/include
	)

	find_path(AVUTIL_INCLUDE_DIR
		NAMES libavutil/avutil.h
		PATHS ${FFMPEG_INCLUDE_PATHS}
		NO_DEFAULT_PATH
	)
	find_path(AVUTIL_INCLUDE_DIR
		NAMES libavutil/avutil.h
		PATHS
			/usr/local/include
			/opt/homebrew/include
			/usr/include
	)

	find_path(SWSCALE_INCLUDE_DIR
		NAMES libswscale/swscale.h
		PATHS ${FFMPEG_INCLUDE_PATHS}
		NO_DEFAULT_PATH
	)
	find_path(SWSCALE_INCLUDE_DIR
		NAMES libswscale/swscale.h
		PATHS
			/usr/local/include
			/opt/homebrew/include
			/usr/include
	)

	if(AVCODEC_LIBRARY AND AVFORMAT_LIBRARY AND AVUTIL_LIBRARY AND SWSCALE_LIBRARY
	   AND AVCODEC_INCLUDE_DIR AND AVFORMAT_INCLUDE_DIR AND AVUTIL_INCLUDE_DIR
	   AND SWSCALE_INCLUDE_DIR)
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
	message(FATAL_ERROR "FFmpeg libraries are required but not found.")
	message(FATAL_ERROR "Searched in:")
	message(FATAL_ERROR "  - pkg-config")
	foreach(path ${CMAKE_PREFIX_PATH})
		message(FATAL_ERROR "  - ${path}")
	endforeach()
	message(FATAL_ERROR "  - System paths")
	message(FATAL_ERROR "")
	message(FATAL_ERROR "Please install FFmpeg development libraries:")
	message(FATAL_ERROR "  macOS: brew install ffmpeg")
	message(FATAL_ERROR "  Ubuntu: sudo apt-get install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev")
	message(FATAL_ERROR "  Windows: Install FFmpeg and set CMAKE_PREFIX_PATH")
	message(FATAL_ERROR "")
	message(FATAL_ERROR "Note: FFmpeg should be available through OBS dependencies (obs-deps).")
	message(FATAL_ERROR "If building in CI, ensure OBS dependencies are properly downloaded and extracted.")
endif()

message(STATUS "Found FFmpeg libraries")
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
	${AVCODEC_INCLUDE_DIRS}
	${AVFORMAT_INCLUDE_DIRS}
	${AVUTIL_INCLUDE_DIRS}
	${SWSCALE_INCLUDE_DIRS}
)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE
	${AVCODEC_LIBRARIES}
	${AVFORMAT_LIBRARIES}
	${AVUTIL_LIBRARIES}
	${SWSCALE_LIBRARIES}
)

target_sources(${CMAKE_PROJECT_NAME} PRIVATE
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
	find_package(Qt6 COMPONENTS Test REQUIRED)
	
	# Test executable
	add_executable(${CMAKE_PROJECT_NAME}-tests
		tests/test-recording-scanner-standalone.cpp
		tests/test-timeline-widget.cpp
		tests/test-audio-analyzer.cpp
	)
	
	# Link test dependencies
	target_link_libraries(${CMAKE_PROJECT_NAME}-tests PRIVATE
		Qt6::Core
		Qt6::Widgets
		Qt6::Test
	)
	
	# Link against plugin sources (for testing)
	# Note: recording-scanner.cpp requires obs-frontend-api which may not be available
	# in test environment, so we only test the public isValidVideoFile method
	target_sources(${CMAKE_PROJECT_NAME}-tests PRIVATE
		src/recording-scanner.cpp
		src/timeline-widget.cpp
		src/audio-analyzer.cpp
	)
	
	# Link OBS libraries if available (optional for tests)
	if(ENABLE_FRONTEND_API)
		target_link_libraries(${CMAKE_PROJECT_NAME}-tests PRIVATE
			OBS::obs-frontend-api
		)
	endif()
	
	# Include directories
	target_include_directories(${CMAKE_PROJECT_NAME}-tests PRIVATE
		src
		${AVCODEC_INCLUDE_DIRS}
		${AVFORMAT_INCLUDE_DIRS}
		${AVUTIL_INCLUDE_DIRS}
		${SWSCALE_INCLUDE_DIRS}
	)
	
	# Link FFmpeg libraries
	target_link_libraries(${CMAKE_PROJECT_NAME}-tests PRIVATE
		${AVCODEC_LIBRARIES}
		${AVFORMAT_LIBRARIES}
		${AVUTIL_LIBRARIES}
		${SWSCALE_LIBRARIES}
	)
	
	# Set test properties
	set_target_properties(${CMAKE_PROJECT_NAME}-tests PROPERTIES
		AUTOMOC ON
	)
	
	# Add test
	add_test(NAME ${CMAKE_PROJECT_NAME}-tests COMMAND ${CMAKE_PROJECT_NAME}-tests)
endif()
