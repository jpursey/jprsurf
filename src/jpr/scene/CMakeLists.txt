## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_scene_SOURCE
  command_properties.cc command_properties.h
  const_property.cc const_property.h
  modifier_property.h
  polled_toggle_property.cc polled_toggle_property.h
  route_properties.cc route_properties.h
  route_reference.cc route_reference.h
  scene.cc scene.h
  scene_state_property.cc scene_state_property.h
  state_properties.cc state_properties.h
  timeline_property.cc timeline_property.h
  track_actions.cc track_actions.h
  track_anchor_property.cc track_anchor_property.h
  track_pick_property.cc track_pick_property.h
  track_properties.cc track_properties.h
  track_reference.cc track_reference.h
  value_property.h
  view.cc view.h
  view_condition.cc view_condition.h
  view_list.cc view_list.h
  view_mapping.cc view_mapping.h
  view_property.cc view_property.h
  view_reference.h
)

set(jpr_scene_DEPS
  absl::log
  absl::log_initialize
  gb_config
  gb_profile
  jpr_common
  jpr_device
  reaper_sdk
)

set(jpr_scene_TEST_SOURCE
  command_properties_test.cc
  const_property_test.cc
  polled_toggle_property_test.cc
  route_properties_test.cc
  route_reference_test.cc
  scene_test.cc
  state_properties_test.cc
  timeline_property_test.cc
  track_actions_test.cc
  track_anchor_property_test.cc
  track_pick_property_test.cc
  track_properties_test.cc
  track_reference_test.cc
  value_property_test.cc
  view_condition_test.cc
  view_list_test.cc
  view_mapping_test.cc
  view_property_test.cc
  view_test.cc
)

set(jpr_scene_TEST_DEPS
  jpr_scene_testing
)

gb_add_library(jpr_scene)
target_include_directories(jpr_scene PUBLIC "../..")

add_subdirectory(testing)
