## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_scene_SOURCE
  scene.cc scene.h
  view.cc view.h
  view_control.cc view_control.h
  view_mapping.cc view_mapping.h
  view_property.cc view_property.h
)

set(jpr_scene_DEPS
  absl::log
  absl::log_initialize
  gb_config
  jpr_common
  jpr_device
  reaper_sdk
)

gb_add_library(jpr_scene)
target_include_directories(jpr_scene PUBLIC "../..")
