## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_common_SOURCE
  child_track_iterator.cc child_track_iterator.h
  color.cc color.h
  guid.cc guid.h
  midi_message.h
  midi_port.cc midi_port.h
  runner.cc runner.h
  track.cc track.h
  track_cache.cc track_cache.h
)

set(jpr_common_DEPS
  absl::log
  absl::log_initialize
  gb_config
  reaper_sdk
)

gb_add_library(jpr_common)
target_include_directories(jpr_common PUBLIC "../..")
