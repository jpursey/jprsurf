## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Fake hardware for unit tests. Only tests link this, never the plugin. It is
# written from the hardware's protocols rather than from jpr_device, so it
# doesn't link jpr_device.

set(jpr_device_testing_SOURCE
  fake_xtouch.cc fake_xtouch.h
)

set(jpr_device_testing_DEPS
  absl::span
  absl::str_format
  absl::strings
  absl::time
  gtest
  jpr_common
  jpr_common_testing
)

set(jpr_device_testing_TEST_SOURCE
  fake_xtouch_test.cc
)

gb_add_library(jpr_device_testing)

# Fake controls, and a device made of them, for unit tests of anything built on
# devices. Only tests link this, never the plugin. It is kept apart from
# jpr_device_testing, as it links jpr_device.

set(jpr_device_fakes_SOURCE
  fake_control_io.h
  fake_device.cc fake_device.h
)

set(jpr_device_fakes_DEPS
  absl::span
  jpr_common
  jpr_device
)

set(jpr_device_fakes_TEST_SOURCE
  fake_device_test.cc
)

gb_add_library(jpr_device_fakes)
