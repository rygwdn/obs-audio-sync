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
# Verify the DLL file exists
if(NOT EXISTS "${PLUGIN_DLL}")
  message(FATAL_ERROR "Plugin DLL not found: ${PLUGIN_DLL}")
endif()

# Get the directory where the resource file will be located
get_filename_component(RESOURCE_DIR "${RESOURCE_OUT}" DIRECTORY)

# Try to make the path relative to the resource file directory for better compatibility
file(RELATIVE_PATH RELATIVE_DLL_PATH "${RESOURCE_DIR}" "${PLUGIN_DLL}")

# If relative path would go up too many levels, use absolute path instead
if(RELATIVE_DLL_PATH MATCHES "^\\.\\.")
  # Use absolute path if relative is too complex
  file(TO_NATIVE_PATH "${PLUGIN_DLL}" NATIVE_DLL_PATH)
  string(REPLACE "\\" "\\\\" ESCAPED_DLL_PATH "${NATIVE_DLL_PATH}")
else()
  # Use relative path
  file(TO_NATIVE_PATH "${RELATIVE_DLL_PATH}" NATIVE_DLL_PATH)
  string(REPLACE "\\" "\\\\" ESCAPED_DLL_PATH "${NATIVE_DLL_PATH}")
endif()

# Generate the resource file
file(WRITE "${RESOURCE_OUT}" "#include <windows.h>\n")
file(APPEND "${RESOURCE_OUT}" "IDR_PLUGIN_DLL RCDATA \"${ESCAPED_DLL_PATH}\"\n")
