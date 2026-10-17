## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# What contract tests need, without the fake, so the DLL that runs them in
# REAPER doesn't link the fake: a contract test that uses it doesn't build
# there. Only tests link this, never the plugin.

set(jpr_common_contract_SOURCE
  contract_test.h
  fake_project.cc fake_project.h
  fake_track.h
  large_project.cc large_project.h
  project_file.cc project_file.h
  reaper_action_list.h
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
# reaper_jpr_check, which runs them in REAPER.

set(jpr_common_contract_TESTS
  fake_reaper_contract_test.cc
  reaper_actions_contract_test.cc
  surface_notifier_contract_test.cc
)

# A fake REAPER for unit tests. Only tests link this, never the plugin.

set(jpr_common_testing_SOURCE
  cached_track.h
  call_cost.cc call_cost.h
  call_counter.cc call_counter.h
  contract_test_fake.cc
  fake_midi.cc fake_midi.h
  fake_reaper.cc fake_reaper.h
  reaper_actions.cc reaper_actions.h
  surface_notifier.cc surface_notifier.h
  test_control_surface.cc test_control_surface.h
)

set(jpr_common_testing_DEPS
  absl::algorithm_container
  absl::any_invocable
  absl::cleanup
  absl::flat_hash_map
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
  call_cost_test.cc
  call_counter_test.cc
  fake_midi_test.cc
  fake_reaper_test.cc
  large_project_test.cc
  project_file_test.cc
  reaper_actions_test.cc
  recording_surface_test.cc
  surface_notifier_test.cc
  ${jpr_common_contract_TESTS}
)

gb_add_library(jpr_common_testing)

# What a DLL that runs inside the test install of REAPER needs (see "Contract
# tests in REAPER" in docs/testing_and_profiling.md). Only those DLLs link
# this, never the plugin or a unit test.

set(jpr_common_test_install_SOURCE
  test_install.cc test_install.h
)

set(jpr_common_test_install_DEPS
  absl::log
  absl::strings
  gb_base
  gb_file
  jpr_common
  jpr_common_contract
  reaper_sdk
)

gb_add_library(jpr_common_test_install)

# The DLL that runs the contract tests inside the test install, and the target
# that runs it there.

set(reaper_jpr_check_SOURCE
  contract_test_reaper.cc
  ${jpr_common_contract_TESTS}
)

set(reaper_jpr_check_DEPS
  absl::function_ref
  absl::log
  absl::strings
  gmock
  gtest
  jpr_common
  jpr_common_contract
  jpr_common_test_install
  reaper_sdk
)

gb_add_shared_library(reaper_jpr_check)

add_custom_target(check_in_reaper
  COMMAND ${CMAKE_COMMAND}
      -DRUN_DLL=$<TARGET_FILE:reaper_jpr_check>
      -DSURFACE_TYPE=RECORDING
      -P "${CMAKE_CURRENT_SOURCE_DIR}/run_in_test_install.cmake"
  DEPENDS reaper_jpr_check
  USES_TERMINAL
  COMMENT "Running the contract tests in REAPER..."
)
