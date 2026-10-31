## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_device_SOURCE
  control.cc control.h
  control_input.cc control_input.h
  control_input_handle.cc control_input_handle.h
  control_input_midi.cc control_input_midi.h
  control_output.cc control_output.h
  control_output_handle.cc control_output_handle.h
  control_output_midi.cc control_output_midi.h
  control_override.cc control_override.h
  device.cc device.h
  device_xtouch.cc device_xtouch.h
  device_xtouch_doc.cc
  device_types.cc device_types.h
  widget.cc widget.h
  widget_definition.cc widget_definition.h
  widget_doc.cc widget_doc.h
  widget_path.cc widget_path.h
  widget_shape.cc widget_shape.h
)

set(jpr_device_DEPS
  absl::algorithm_container
  absl::flat_hash_map
  absl::flat_hash_set
  absl::log
  absl::log_initialize
  absl::memory
  absl::span
  absl::strings
  gb_config
  gb_parse
  gb_profile
  jpr_common
  reaper_sdk
)

set(jpr_device_TEST_SOURCE
  control_input_handle_test.cc
  control_input_midi_test.cc
  control_input_test.cc
  control_output_handle_test.cc
  control_output_midi_test.cc
  control_output_test.cc
  control_override_test.cc
  control_test.cc
  device_test.cc
  device_types_test.cc
  device_xtouch_test.cc
  widget_definition_test.cc
  widget_doc_test.cc
  widget_path_test.cc
  widget_shape_test.cc
  widget_test.cc
)

set(jpr_device_TEST_DEPS
  jpr_device_fakes
  jpr_device_testing
)

gb_add_library(jpr_device)
target_include_directories(jpr_device PUBLIC "../..")

add_subdirectory(testing)
