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

# CPack configuration for NSIS installer
set(CPACK_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${CMAKE_PROJECT_VERSION}")
set(CPACK_PACKAGE_VENDOR "${PLUGIN_AUTHOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${CMAKE_PROJECT_NAME} - OBS Studio Plugin")
set(CPACK_PACKAGE_DESCRIPTION "${CMAKE_PROJECT_NAME} plugin for OBS Studio")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")

# NSIS-specific configuration
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-windows-x64")
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/release")

# NSIS installer settings
set(CPACK_NSIS_PACKAGE_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_NSIS_DISPLAY_NAME "${CMAKE_PROJECT_NAME}")
set(CPACK_NSIS_HELP_LINK "${PLUGIN_WEBSITE}")
set(CPACK_NSIS_URL_INFO_ABOUT "${PLUGIN_WEBSITE}")
set(CPACK_NSIS_CONTACT "${PLUGIN_EMAIL}")
set(CPACK_NSIS_MODIFY_PATH OFF)
set(CPACK_NSIS_ENABLE_UNINSTALL_BEFORE_INSTALL ON)

# Installation directory - default to Program Files
# Users can change this during installation
set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
set(CPACK_NSIS_DEFAULT_INSTALL_DIR "$PROGRAMFILES64\\obs-studio")

# Use DESTDIR for staging (required for proper file paths in NSIS)
# Note: CPack warns about using CPACK_SET_DESTDIR with NSIS, but it's needed
# for proper staging. The actual install location is controlled by
# CPACK_NSIS_DEFAULT_INSTALL_DIR, not CMAKE_INSTALL_PREFIX.
set(CPACK_SET_DESTDIR ON)

# Override CMAKE_INSTALL_PREFIX to a relative path for CPack packaging
# When CPACK_SET_DESTDIR is ON, CPack uses DESTDIR + CMAKE_INSTALL_PREFIX
# An absolute path in CMAKE_INSTALL_PREFIX causes path mixing issues.
# Use "." to make installs relative to DESTDIR directly, matching NSIS expectations.
# The actual install location is controlled by CPACK_NSIS_DEFAULT_INSTALL_DIR.
if(CMAKE_INSTALL_PREFIX MATCHES "^[A-Za-z]:")
  # Original prefix is absolute Windows path, store it for reference
  set(_ORIGINAL_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE INTERNAL "Original install prefix")
  # Use "." to make installs relative to DESTDIR root during CPack staging
  # This ensures files go to DESTDIR/${CMAKE_PROJECT_NAME}/bin/64bit/ as expected by NSIS
  set(CMAKE_INSTALL_PREFIX "." CACHE STRING "Install prefix for CPack staging" FORCE)
endif()

# Custom install commands to place files in OBS directory structure
# CPack first installs files to $INSTDIR/${CMAKE_PROJECT_NAME}/bin/64bit/ and data/
# We then move them to the correct OBS plugin structure
set(
  CPACK_NSIS_EXTRA_INSTALL_COMMANDS
  "
  ; Move plugin DLL from staging to obs-plugins/64bit/
  IfFileExists \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\bin\\\\64bit\\\\${CMAKE_PROJECT_NAME}.dll\\\" 0 skip_dll
    CreateDirectory \\\"$INSTDIR\\\\obs-plugins\\\\64bit\\\"
    CopyFiles /SILENT \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\bin\\\\64bit\\\\${CMAKE_PROJECT_NAME}.dll\\\" \\\"$INSTDIR\\\\obs-plugins\\\\64bit\\\\${CMAKE_PROJECT_NAME}.dll\\\"
    Delete \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\bin\\\\64bit\\\\${CMAKE_PROJECT_NAME}.dll\\\"
  skip_dll:
  
  ; Move data files from staging to data/obs-plugins/<plugin-name>/
  IfFileExists \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\data\\\" 0 skip_data
    CreateDirectory \\\"$INSTDIR\\\\data\\\\obs-plugins\\\\${CMAKE_PROJECT_NAME}\\\"
    CopyFiles /SILENT \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\data\\\\*\\\" \\\"$INSTDIR\\\\data\\\\obs-plugins\\\\${CMAKE_PROJECT_NAME}\\\"
    RMDir /r \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\data\\\"
  skip_data:
  
  ; Clean up staging directory structure
  RMDir \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\bin\\\\64bit\\\"
  RMDir \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\\bin\\\"
  RMDir \\\"$INSTDIR\\\\${CMAKE_PROJECT_NAME}\\\"
"
)

set(
  CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
  "
  ; Remove plugin DLL
  Delete \\\"$INSTDIR\\\\obs-plugins\\\\64bit\\\\${CMAKE_PROJECT_NAME}.dll\\\"
  
  ; Remove data files
  RMDir /r \\\"$INSTDIR\\\\data\\\\obs-plugins\\\\${CMAKE_PROJECT_NAME}\\\"
"
)

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

include(CPack)
