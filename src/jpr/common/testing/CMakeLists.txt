## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# A fake REAPER for unit tests. Only tests link this, never the plugin.

set(jpr_common_testing_SOURCE
  fake_project.cc fake_project.h
  fake_reaper.cc fake_reaper.h
  fake_track.h
  test_control_surface.cc test_control_surface.h
)

set(jpr_common_testing_DEPS
  absl::flat_hash_set
  absl::log
  absl::node_hash_map
  absl::span
  absl::str_format
  absl::time
  gtest
  jpr_common
  reaper_sdk
)

set(jpr_common_testing_TEST_SOURCE
  fake_reaper_test.cc
)

gb_add_library(jpr_common_testing)
