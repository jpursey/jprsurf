## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_common_SOURCE
  anchor.cc anchor.h
  color.cc color.h
  guid.cc guid.h
  midi_message.h
  midi_port.cc midi_port.h
  midi_sysex.cc midi_sysex.h
  modifiers.cc modifiers.h
  numbered_name.h
  runner.cc runner.h
  timeline.cc timeline.h
  track.cc track.h
  track_cache.cc track_cache.h
  undo.cc undo.h
  volume_utils.h
)

set(jpr_common_DEPS
  absl::log
  absl::log_initialize
  absl::time
  gb_config
  reaper_sdk
)

set(jpr_common_TEST_SOURCE
  anchor_test.cc
  numbered_name_test.cc
)

gb_add_library(jpr_common)
target_include_directories(jpr_common PUBLIC "../..")
