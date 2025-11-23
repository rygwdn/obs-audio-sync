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
find_package(PkgConfig REQUIRED)
pkg_check_modules(AVCODEC REQUIRED libavcodec)
pkg_check_modules(AVFORMAT REQUIRED libavformat)
pkg_check_modules(AVUTIL REQUIRED libavutil)
pkg_check_modules(SWSCALE REQUIRED libswscale)

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
