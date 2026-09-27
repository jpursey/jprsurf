## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_scene_SOURCE
  command_properties.cc command_properties.h
  modifier_property.h
  polled_toggle_property.cc polled_toggle_property.h
  route_properties.cc route_properties.h
  scene.cc scene.h
  scene_state_property.cc scene_state_property.h
  state_properties.cc state_properties.h
  timeline_property.cc timeline_property.h
  track_actions.cc track_actions.h
  track_properties.cc track_properties.h
  value_property.h
  view.cc view.h
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
