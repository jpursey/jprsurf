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
  jpr_common
  jpr_plugin
  reaper_sdk
)

gb_add_shared_library(reaper_jprsurf)

if(JPR_DEPLOY_TO_REAPER)
  add_custom_command(TARGET reaper_jprsurf POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:reaper_jprsurf>
      "${REAPER_PLUGINS_DIR}/reaper_jprsurf.dll"
      COMMENT "Deploying plugin to Reaper..."
  )
endif()
