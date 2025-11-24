# CMake Linux build dependencies module

include_guard(GLOBAL)

# Linux doesn't have pre-built OBS deps like macOS/Windows
# Instead, we use system packages (libobs-dev, etc.)
# This file exists for consistency but doesn't download dependencies
function(_check_dependencies_linux)
  # Linux uses system packages, so we don't need to download OBS deps
  # The build will use system-installed libobs-dev and FFmpeg
  message(STATUS "Linux build: Using system packages for OBS dependencies")
endfunction()

_check_dependencies_linux()
