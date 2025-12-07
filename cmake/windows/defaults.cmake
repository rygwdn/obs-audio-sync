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
# Override CPACK_PACKAGE_INSTALL_DIRECTORY to prevent CPack from using "${PACKAGE_NAME} ${PACKAGE_VERSION}"
# The default install directory is: ${CPACK_NSIS_INSTALL_ROOT}/${CPACK_PACKAGE_INSTALL_DIRECTORY}
# Setting this to "obs-studio" ensures the installer defaults to Program Files\obs-studio
# instead of Program Files\obs-audio-sync 1.0.0
set(CPACK_PACKAGE_INSTALL_DIRECTORY "obs-studio")

# Note: CMAKE_INSTALL_PREFIX is left at its default value ($ENV{ALLUSERSPROFILE}/obs-studio/plugins)
# CPack will use this for staging files internally when creating the NSIS installer.
# The NSIS installer's default directory shown to users is controlled by CPACK_NSIS_INSTALL_ROOT
# and CPACK_PACKAGE_INSTALL_DIRECTORY (set above), which are separate from CMAKE_INSTALL_PREFIX.
# CPack handles staging automatically when CPACK_SET_DESTDIR is OFF.

# Don't use CPACK_SET_DESTDIR with NSIS - it causes issues and CPack warns against it
# NSIS generator handles staging internally when CPACK_SET_DESTDIR is OFF
set(CPACK_SET_DESTDIR OFF)

# Files are installed directly to the OBS directory structure:
# - DLL: obs-plugins/64bit/${CMAKE_PROJECT_NAME}.dll
# - Data: data/obs-plugins/${CMAKE_PROJECT_NAME}/
# No extra install commands needed since install destinations match OBS structure

# Query registry for OBS Studio installation path (similar to SceneSwitcher's Inno Setup approach)
# If found, use it; otherwise fall back to default Program Files\obs-studio
set(
  CPACK_NSIS_EXTRA_INSTALL_COMMANDS
  "
  ; Try to find OBS Studio installation path from registry
  ; ReadRegStr reads from the registry view matching the installer architecture (64-bit)
  ReadRegStr $R0 HKLM \"SOFTWARE\\OBS Studio\" \"\"
  ; If registry value is not empty, use it as install directory
  StrCmp $R0 \"\" +2
    StrCpy $INSTDIR \"$R0\"
"
)

set(
  CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS
  "
  ; Remove plugin DLL
  Delete \"$INSTDIR\\obs-plugins\\64bit\\${CMAKE_PROJECT_NAME}.dll\"
  
  ; Remove data files
  RMDir /r \"$INSTDIR\\data\\obs-plugins\\${CMAKE_PROJECT_NAME}\"
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
