## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# The fixture for tests of the whole surface. Only tests link this, never the
# plugin's DLL.

set(jpr_plugin_testing_SOURCE
  surface_test.cc surface_test.h
)

set(jpr_plugin_testing_DEPS
  absl::strings
  absl::time
  gb_test
  gtest
  jpr_common_testing
  jpr_device
  jpr_device_testing
  jpr_plugin
)

set(jpr_plugin_testing_TEST_SOURCE
  surface_test_test.cc
)

gb_add_library(jpr_plugin_testing)
