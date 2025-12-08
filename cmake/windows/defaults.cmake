# CMake Windows defaults module

include_guard(GLOBAL)

# Enable find_package targets to become globally available targets
set(CMAKE_FIND_PACKAGE_TARGETS_GLOBAL TRUE)

include(buildspec)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
  set(
    CMAKE_INSTALL_PREFIX
    "$ENV{ALLUSERSPROFILE}/obs-studio/plugins"
    CACHE STRING
    "Default plugin installation directory"
    FORCE
  )
endif()

# CPack configuration for source packages only
# Note: Windows installer is now built as a separate executable (${CMAKE_PROJECT_NAME}-installer)
# See CMakeLists.txt for installer executable configuration
set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "${PLUGIN_AUTHOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${CMAKE_PROJECT_NAME} - OBS Studio Plugin")
set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_NAME} plugin for OBS Studio")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# Source package configuration
set(CPACK_SOURCE_GENERATOR "ZIP")
set(
  CPACK_SOURCE_IGNORE_FILES
  ".*~$"
  "\\.git/"
  "\\.github/"
  "\\.gitignore"
  "\\.ccache/"
  "build_.*"
  "cmake/\\.CMakeBuildNumber"
  "release/"
)

set(CPACK_VERBATIM_VARIABLES YES)
set(CPACK_SOURCE_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-source")

# Only include CPack for source packages (ZIP)
# Windows installer is built separately as ${CMAKE_PROJECT_NAME}-installer executable
set(CPACK_GENERATOR "ZIP")
include(CPack)
