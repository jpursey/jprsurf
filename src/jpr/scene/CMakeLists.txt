## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_scene_SOURCE
  catalog.cc catalog.h
  command_properties.cc command_properties.h
  const_property.cc const_property.h
  create_scene.cc create_scene.h
  modifier_property.h
  polled_toggle_property.cc polled_toggle_property.h
  route_properties.cc route_properties.h
  route_reference.cc route_reference.h
  scene.cc scene.h
  scene_state_property.cc scene_state_property.h
  state_properties.cc state_properties.h
  surface_spec.cc surface_spec.h
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
  absl::flat_hash_map
  absl::flat_hash_set
  absl::log
  absl::log_initialize
  absl::span
  absl::strings
  gb_config
  gb_profile
  jpr_common
  jpr_device
  reaper_sdk
)

set(jpr_scene_TEST_SOURCE
  catalog_test.cc
  command_properties_test.cc
  const_property_test.cc
  create_scene_test.cc
  polled_toggle_property_test.cc
  route_properties_test.cc
  route_reference_test.cc
  scene_test.cc
  state_properties_test.cc
  surface_spec_test.cc
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
  jpr_device_testing
  jpr_scene_testing
)

gb_add_library(jpr_scene)
target_include_directories(jpr_scene PUBLIC "../..")

add_subdirectory(testing)
