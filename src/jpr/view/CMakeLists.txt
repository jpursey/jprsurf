## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_view_SOURCE
  scene.cc scene.h
  view.cc view.h
)

set(jpr_view_DEPS
  absl::log
  absl::log_initialize
  gb_config
  jpr_common
  jpr_device
  reaper_sdk
)

gb_add_library(jpr_view)
target_include_directories(jpr_view PUBLIC "../..")
