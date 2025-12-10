# CMake script to generate installer resource file with embedded DLL
# This script is called at build time to generate the .rc file with the actual DLL path

# Get the DLL path from command line
if(NOT DEFINED PLUGIN_DLL)
  message(FATAL_ERROR "PLUGIN_DLL not defined")
endif()

if(NOT DEFINED RESOURCE_OUT)
  message(FATAL_ERROR "RESOURCE_OUT not defined")
endif()

# The DLL path comes from generator expressions, so it should already be expanded
# Strip any surrounding quotes first
string(STRIP "${PLUGIN_DLL}" PLUGIN_DLL_STRIPPED)
string(REGEX REPLACE "^\"(.*)\"$" "\\1" PLUGIN_DLL_CLEAN "${PLUGIN_DLL_STRIPPED}")

# Normalize the path (handle both forward and backslashes)
# Only make absolute if it's not already absolute
if(IS_ABSOLUTE "${PLUGIN_DLL_CLEAN}")
  set(PLUGIN_DLL_ABS "${PLUGIN_DLL_CLEAN}")
else()
  get_filename_component(PLUGIN_DLL_ABS "${PLUGIN_DLL_CLEAN}" ABSOLUTE)
endif()

file(TO_CMAKE_PATH "${PLUGIN_DLL_ABS}" PLUGIN_DLL_NORMALIZED)
file(TO_NATIVE_PATH "${PLUGIN_DLL_NORMALIZED}" PLUGIN_DLL_NATIVE)

# Verify the DLL file exists
# Try multiple path formats in case of path issues
set(ACTUAL_DLL_PATH "")
if(EXISTS "${PLUGIN_DLL_CLEAN}")
  set(ACTUAL_DLL_PATH "${PLUGIN_DLL_CLEAN}")
elseif(EXISTS "${PLUGIN_DLL_ABS}")
  set(ACTUAL_DLL_PATH "${PLUGIN_DLL_ABS}")
elseif(EXISTS "${PLUGIN_DLL_NORMALIZED}")
  set(ACTUAL_DLL_PATH "${PLUGIN_DLL_NORMALIZED}")
elseif(EXISTS "${PLUGIN_DLL_NATIVE}")
  set(ACTUAL_DLL_PATH "${PLUGIN_DLL_NATIVE}")
endif()

# If still not found, provide detailed error message
if(NOT ACTUAL_DLL_PATH OR NOT EXISTS "${ACTUAL_DLL_PATH}")
  message(STATUS "Plugin DLL path resolution:")
  message(STATUS "  Original: ${PLUGIN_DLL}")
  message(STATUS "  Cleaned: ${PLUGIN_DLL_CLEAN}")
  message(STATUS "  Absolute: ${PLUGIN_DLL_ABS}")
  message(STATUS "  Normalized: ${PLUGIN_DLL_NORMALIZED}")
  message(STATUS "  Native: ${PLUGIN_DLL_NATIVE}")
  message(FATAL_ERROR "Plugin DLL not found. The plugin target must be built before generating the installer resource. Expected DLL at: ${PLUGIN_DLL_CLEAN}")
endif()

# Get the directory where the resource file will be located
get_filename_component(RESOURCE_DIR "${RESOURCE_OUT}" DIRECTORY)

# Try to make the path relative to the resource file directory for better compatibility
file(RELATIVE_PATH RELATIVE_DLL_PATH "${RESOURCE_DIR}" "${ACTUAL_DLL_PATH}")

# If relative path would go up too many levels, use absolute path instead
if(RELATIVE_DLL_PATH MATCHES "^\\.\\.")
  # Use absolute path if relative is too complex
  file(TO_NATIVE_PATH "${ACTUAL_DLL_PATH}" NATIVE_DLL_PATH)
  string(REPLACE "\\" "\\\\" ESCAPED_DLL_PATH "${NATIVE_DLL_PATH}")
else()
  # Use relative path
  file(TO_NATIVE_PATH "${RELATIVE_DLL_PATH}" NATIVE_DLL_PATH)
  string(REPLACE "\\" "\\\\" ESCAPED_DLL_PATH "${NATIVE_DLL_PATH}")
endif()

# Generate the resource file
file(WRITE "${RESOURCE_OUT}" "#include <windows.h>\n")
file(APPEND "${RESOURCE_OUT}" "IDR_PLUGIN_DLL RCDATA \"${ESCAPED_DLL_PATH}\"\n")
