## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_device_SOURCE
  control.h
  control_input.cc control_input.h
  control_input_midi.cc control_input_midi.h
  control_output.cc control_output.h
  control_output_midi.cc control_output_midi.h
  device.cc device.h
  device_xtouch.cc device_xtouch.h
)

set(jpr_device_DEPS
  absl::log
  absl::log_initialize
  gb_config
  jpr_common
  reaper_sdk
)

gb_add_library(jpr_device)
target_include_directories(jpr_device PUBLIC "../..")
