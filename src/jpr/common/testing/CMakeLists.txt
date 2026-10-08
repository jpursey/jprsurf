## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# A fake REAPER for unit tests. Only tests link this, never the plugin.

set(jpr_common_testing_SOURCE
  cached_track.h
  contract_test.h
  contract_test_fake.cc
  fake_midi.cc fake_midi.h
  fake_project.cc fake_project.h
  fake_reaper.cc fake_reaper.h
  fake_track.h
  project_file.cc project_file.h
  reaper_actions.cc reaper_actions.h
  recording_surface.cc recording_surface.h
  surface_notifier.cc surface_notifier.h
  test_control_surface.cc test_control_surface.h
)

set(jpr_common_testing_DEPS
  absl::any_invocable
  absl::flat_hash_set
  absl::function_ref
  absl::log
  absl::node_hash_map
  absl::span
  absl::str_format
  absl::strings
  absl::time
  gb_base
  gtest
  jpr_common
  reaper_sdk
)

set(jpr_common_testing_TEST_SOURCE
  fake_midi_test.cc
  fake_reaper_test.cc
  project_file_test.cc
  reaper_actions_test.cc
  recording_surface_test.cc
  surface_notifier_contract_test.cc
  surface_notifier_test.cc
)

gb_add_library(jpr_common_testing)
