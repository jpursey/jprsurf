## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# The fixture for tests of the scene. Only tests link this, never the plugin.

set(jpr_scene_testing_SOURCE
  scene_test.cc scene_test.h
  test_property.h
)

set(jpr_scene_testing_DEPS
  absl::any_invocable
  absl::strings
  absl::time
  gb_test
  gtest
  jpr_common
  jpr_common_testing
  jpr_device
  jpr_device_fakes
  jpr_scene
)

set(jpr_scene_testing_TEST_SOURCE
  scene_test_test.cc
)

gb_add_library(jpr_scene_testing)
