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
# Convert to native path format (Windows uses backslashes)
file(TO_NATIVE_PATH "${PLUGIN_DLL}" NATIVE_DLL_PATH)

# Escape backslashes for the resource file (they need to be doubled in .rc files)
string(REPLACE "\\" "\\\\" ESCAPED_DLL_PATH "${NATIVE_DLL_PATH}")

# Generate the resource file
file(WRITE "${RESOURCE_OUT}" "#include <windows.h>\n")
file(APPEND "${RESOURCE_OUT}" "IDR_PLUGIN_DLL RCDATA \"${ESCAPED_DLL_PATH}\"\n")

