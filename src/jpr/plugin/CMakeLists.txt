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
  jpr_device_testing
)

gb_add_library(jpr_plugin)
target_include_directories(jpr_plugin PUBLIC "../..")

# The DLL REAPER loads: just its entry points, which read the environment.

set(reaper_jprsurf_SOURCE
  dll_main.cc
)

set(reaper_jprsurf_DEPS
  absl::log
  absl::log_initialize
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
