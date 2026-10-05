## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Tests of the config PluginSurface builds, and their fixture. Only tests link
# this, never the plugin's DLL.

set(jpr_default_config_SOURCE
  default_config_test.cc default_config_test.h
)

set(jpr_default_config_DEPS
  absl::strings
  jpr_device_testing
  jpr_plugin_testing
)

set(jpr_default_config_TEST_SOURCE
  default_config_test_test.cc
  global_test.cc
  track_list_test.cc
  track_strip_test.cc
)

gb_add_library(jpr_default_config)
