## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# The plugin, as a static library that tests link.

set(jpr_plugin_SOURCE
  plugin.cc plugin.h
  plugin_surface.cc plugin_surface.h
)

set(jpr_plugin_DEPS
  absl::log
  gb_config
  jpr_common
  jpr_device
  jpr_scene
  reaper_sdk
)

set(jpr_plugin_TEST_SOURCE
  plugin_test.cc
)

set(jpr_plugin_TEST_DEPS
  jpr_common_testing
)

gb_add_library(jpr_plugin)
target_include_directories(jpr_plugin PUBLIC "../..")

# jpr/plugin/build_info.h, with the git commit and whether it was modified, for
# the profile. It is regenerated on every build, but only rewritten when it
# changes (see build_info.cmake).
find_package(Git QUIET)
set(JPR_BUILD_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(JPR_BUILD_INFO_HEADER "${JPR_BUILD_INFO_DIR}/jpr/plugin/build_info.h")
add_custom_target(jpr_build_info
  COMMAND "${CMAKE_COMMAND}"
    "-DGIT=${GIT_EXECUTABLE}"
    "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DOUTPUT=${JPR_BUILD_INFO_HEADER}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/build_info.cmake"
  BYPRODUCTS "${JPR_BUILD_INFO_HEADER}")
add_dependencies(jpr_plugin jpr_build_info)
target_include_directories(jpr_plugin PRIVATE "${JPR_BUILD_INFO_DIR}")

add_subdirectory(testing)
add_subdirectory(default_config)

# The DLL REAPER loads: just its entry points, which read the environment.

set(reaper_jprsurf_SOURCE
  dll_main.cc
)

set(reaper_jprsurf_DEPS
  absl::log
  gb_base
  gb_file
  jpr_common
  jpr_plugin
  reaper_sdk
)

gb_add_shared_library(reaper_jprsurf)

# The deploy is a target of its own, rather than a step of reaper_jprsurf's
# build, so targets that only need the DLL, such as check_load_in_reaper, don't
# deploy it.
# It runs only when the DLL changed, as the stamp is touched after the copy.
if(JPR_DEPLOY_TO_REAPER)
  set(deploy_stamp "${CMAKE_CURRENT_BINARY_DIR}/deploy_reaper_jprsurf.stamp")
  add_custom_command(OUTPUT "${deploy_stamp}"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:reaper_jprsurf>
      "${REAPER_PLUGINS_DIR}/reaper_jprsurf.dll"
      COMMAND ${CMAKE_COMMAND} -E touch "${deploy_stamp}"
      DEPENDS reaper_jprsurf $<TARGET_FILE:reaper_jprsurf>
      COMMENT "Deploying plugin to Reaper..."
  )
  add_custom_target(deploy_reaper_jprsurf ALL DEPENDS "${deploy_stamp}")
endif()

# Checks that REAPER loads and unloads the DLL, in the test install of REAPER
# that runs the contract tests (see check_load_in_reaper.cmake).

add_custom_target(check_load_in_reaper
  COMMAND ${CMAKE_COMMAND}
      -DPLUGIN_DLL=$<TARGET_FILE:reaper_jprsurf>
      -DCHECK_DLL=$<TARGET_FILE:reaper_jpr_check>
      "-DCHECK_SCRIPT=${PROJECT_SOURCE_DIR}/src/jpr/common/testing/check_in_reaper.cmake"
      "-DRUN_DIR=${CMAKE_CURRENT_BINARY_DIR}/check_load_in_reaper"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_load_in_reaper.cmake"
  DEPENDS reaper_jprsurf reaper_jpr_check
  USES_TERMINAL
  COMMENT "Checking that REAPER loads the plugin..."
)
