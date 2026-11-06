## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_config_SOURCE
  surface_config.cc surface_config.h
)

set(jpr_config_DEPS
  absl::flat_hash_map
  absl::log
  absl::no_destructor
  absl::span
  absl::strings
  gb_parse
  jpr_common
  jpr_device
  jpr_scene
)

set(jpr_config_TEST_SOURCE
  surface_config_test.cc
)

gb_add_library(jpr_config)
target_include_directories(jpr_config PUBLIC "../..")
