# CMake macOS build dependencies module

include_guard(GLOBAL)

include(buildspec_common)

# _check_dependencies_macos: Set up macOS slice for _check_dependencies
function(_check_dependencies_macos)
  set(arch universal)
  set(platform macos)

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/buildspec.json" buildspec)

  set(dependencies_dir "${CMAKE_CURRENT_SOURCE_DIR}/.deps")
  set(prebuilt_filename "macos-deps-VERSION-ARCH_REVISION.tar.xz")
  set(prebuilt_destination "obs-deps-VERSION-ARCH")
  set(qt6_filename "macos-deps-qt6-VERSION-ARCH-REVISION.tar.xz")
  set(qt6_destination "obs-deps-qt6-VERSION-ARCH")
  set(obs-studio_filename "VERSION.tar.gz")
  set(obs-studio_destination "obs-studio-VERSION")
  set(dependencies_list prebuilt qt6 obs-studio)

  _check_dependencies()

  execute_process(
    COMMAND "xattr" -r -d com.apple.quarantine "${dependencies_dir}"
    RESULT_VARIABLE result
    COMMAND_ERROR_IS_FATAL ANY
  )

  # Prefer OBS's Qt frameworks if OBS is installed (they don't have AGL dependency)
  # Check common OBS installation locations
  set(_obs_qt_paths
    "/Applications/OBS.app/Contents/Frameworks"
    "$ENV{HOME}/Applications/OBS.app/Contents/Frameworks"
    "/usr/local/opt/obs/Contents/Frameworks"
  )
  
  set(_found_obs_qt FALSE)
  foreach(_obs_path IN LISTS _obs_qt_paths)
    if(EXISTS "${_obs_path}/QtGui.framework/QtGui")
      # Verify OBS's QtGui doesn't have AGL dependency
      execute_process(
        COMMAND otool -L "${_obs_path}/QtGui.framework/QtGui"
        OUTPUT_VARIABLE _qtgui_deps
        ERROR_QUIET
      )
      if(NOT _qtgui_deps MATCHES "AGL")
        message(STATUS "Found OBS Qt frameworks at ${_obs_path} (no AGL dependency) - using for build")
        # Prepend OBS's frameworks to search path so they're found first
        list(INSERT CMAKE_FRAMEWORK_PATH 0 "${_obs_path}")
        # Also add OBS app's Contents directory to CMAKE_PREFIX_PATH so find_package(Qt6) finds it
        get_filename_component(_obs_app_dir "${_obs_path}/../.." ABSOLUTE)
        list(INSERT CMAKE_PREFIX_PATH 0 "${_obs_app_dir}")
        # Remove downloaded Qt6 from CMAKE_PREFIX_PATH to prevent it from being used
        file(GLOB _qt6_deps_dirs "${dependencies_dir}/obs-deps-qt6-*")
        foreach(_qt6_dir IN LISTS _qt6_deps_dirs)
          if(IS_DIRECTORY "${_qt6_dir}")
            list(REMOVE_ITEM CMAKE_PREFIX_PATH "${_qt6_dir}")
          endif()
        endforeach()
        set(_found_obs_qt TRUE)
        break()
      endif()
    endif()
  endforeach()
  
  # Only use downloaded Qt6 if OBS's Qt is not available
  if(NOT _found_obs_qt)
    list(APPEND CMAKE_FRAMEWORK_PATH "${dependencies_dir}/Frameworks")
    message(STATUS "Using downloaded Qt6 from obs-deps (may have AGL dependency)")
  endif()
  
  # Update CMAKE_PREFIX_PATH in cache
  set(CMAKE_PREFIX_PATH ${CMAKE_PREFIX_PATH} CACHE PATH "CMake prefix search path" FORCE)
  
  set(CMAKE_FRAMEWORK_PATH ${CMAKE_FRAMEWORK_PATH} PARENT_SCOPE)
  
  # libobs config - prefer build directory version (has correct paths) over installed framework version
  # The installed framework version has incorrect paths in its targets files
  file(GLOB _obs_studio_dirs "${dependencies_dir}/obs-studio-*")
  foreach(_obs_dir IN LISTS _obs_studio_dirs)
    if(IS_DIRECTORY "${_obs_dir}")
      set(_libobs_build_config "${_obs_dir}/build_universal/libobs/libobsConfig.cmake")
      if(EXISTS "${_libobs_build_config}")
        set(libobs_DIR "${_obs_dir}/build_universal/libobs" CACHE PATH "libobs config directory" FORCE)
        break()
      endif()
    endif()
  endforeach()
endfunction()

_check_dependencies_macos()
