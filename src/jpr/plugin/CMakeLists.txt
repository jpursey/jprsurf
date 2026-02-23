## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(reaper_jprsurf_SOURCE
  control_surface.cc control_surface.h
  plugin.cc plugin.h
  jprsurf.cc
)

set(reaper_jprsurf_DEPS
  absl::log
  absl::log_initialize
  gb_config
  jpr_common
  jpr_device
  reaper_sdk
)

gb_add_shared_library(reaper_jprsurf)
target_include_directories(reaper_jprsurf PUBLIC "../..")

add_custom_command(TARGET reaper_jprsurf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:reaper_jprsurf>
    "${REAPER_PLUGINS_DIR}/reaper_jprsurf.dll"
    COMMENT "Deploying plugin to Reaper..."
)
