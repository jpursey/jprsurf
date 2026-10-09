## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# What contract tests need, without the fake, so the DLL that runs them in
# REAPER doesn't link the fake: a contract test that uses it doesn't build
# there. Only tests link this, never the plugin.

set(jpr_common_contract_SOURCE
  action_ids.h
  contract_test.h
  fake_project.cc fake_project.h
  fake_track.h
  project_file.cc project_file.h
  recording_surface.cc recording_surface.h
)

set(jpr_common_contract_DEPS
  absl::any_invocable
  absl::function_ref
  absl::memory
  absl::node_hash_map
  absl::span
  absl::str_format
  absl::strings
  gtest
  jpr_common
  reaper_sdk
)

gb_add_library(jpr_common_contract)

# The contract tests, which act only through REAPER's API. They build into
# jpr_common_testing_test, which runs them under the fake, and into
# reaper_jprsurf_check, which runs them in REAPER.

set(jpr_common_contract_TESTS
  fake_reaper_contract_test.cc
  surface_notifier_contract_test.cc
)

# A fake REAPER for unit tests. Only tests link this, never the plugin.

set(jpr_common_testing_SOURCE
  cached_track.h
  contract_test_fake.cc
  fake_midi.cc fake_midi.h
  fake_reaper.cc fake_reaper.h
  reaper_actions.cc reaper_actions.h
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
  jpr_common_contract
  reaper_sdk
)

set(jpr_common_testing_TEST_SOURCE
  fake_midi_test.cc
  fake_reaper_test.cc
  project_file_test.cc
  reaper_actions_test.cc
  recording_surface_test.cc
  surface_notifier_test.cc
  ${jpr_common_contract_TESTS}
)

gb_add_library(jpr_common_testing)

# The DLL that runs the contract tests inside the test install of REAPER, and
# the target that runs it there (see "Contract tests in REAPER" in
# docs/testing_and_profiling.md).

set(reaper_jprsurf_check_SOURCE
  contract_test_reaper.cc
  ${jpr_common_contract_TESTS}
)

set(reaper_jprsurf_check_DEPS
  absl::function_ref
  absl::log
  absl::strings
  gb_base
  gmock
  gtest
  jpr_common
  jpr_common_contract
  reaper_sdk
)

gb_add_shared_library(reaper_jprsurf_check)

add_custom_target(check_in_reaper
  COMMAND ${CMAKE_COMMAND}
      -DCHECK_DLL=$<TARGET_FILE:reaper_jprsurf_check>
      -P "${CMAKE_CURRENT_SOURCE_DIR}/check_in_reaper.cmake"
  DEPENDS reaper_jprsurf_check
  USES_TERMINAL
  COMMENT "Running the contract tests in REAPER..."
)
