## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jpr_common_SOURCE
  anchor.cc anchor.h
  automation.cc automation.h
  color.cc color.h
  control_surface.cc control_surface.h
  guid.cc guid.h
  local_time.cc local_time.h
  log_file.cc log_file.h
  midi_message.h
  midi_port.cc midi_port.h
  midi_ports.cc midi_ports.h
  midi_sysex.cc midi_sysex.h
  modifiers.cc modifiers.h
  numbered_name.h
  prefixed_name.h
  reaper_api.cc reaper_api.h
  reaper_profiler.cc reaper_profiler.h
  reaper_trace.cc reaper_trace.h
  reset_test_key.h
  runner.cc runner.h
  test_reset_common.cc test_reset_common.h
  timeline.cc timeline.h
  track.cc track.h
  track_cache.cc track_cache.h
  undo.cc undo.h
  volume_utils.h
)

set(jpr_common_DEPS
  absl::base
  absl::log
  absl::log_initialize
  absl::time
  gb_base
  gb_config
  gb_profile
  reaper_sdk
)

set(jpr_common_TEST_SOURCE
  anchor_test.cc
  color_test.cc
  numbered_name_test.cc
  prefixed_name_test.cc
  reaper_api_test.cc
  reaper_trace_test.cc
)

gb_add_library(jpr_common)
target_include_directories(jpr_common PUBLIC "../..")

# jpr/common/build_info.h, with the git commit and whether it was modified, for
# the profile snapshot. It is regenerated on every build, but only rewritten
# when it changes (see build_info.cmake).
find_package(Git QUIET)
set(JPR_BUILD_INFO_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(JPR_BUILD_INFO_HEADER "${JPR_BUILD_INFO_DIR}/jpr/common/build_info.h")
add_custom_target(jpr_build_info
  COMMAND "${CMAKE_COMMAND}"
    "-DGIT=${GIT_EXECUTABLE}"
    "-DSOURCE_DIR=${PROJECT_SOURCE_DIR}"
    "-DOUTPUT=${JPR_BUILD_INFO_HEADER}"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/build_info.cmake"
  BYPRODUCTS "${JPR_BUILD_INFO_HEADER}")
add_dependencies(jpr_common jpr_build_info)
target_include_directories(jpr_common PRIVATE "${JPR_BUILD_INFO_DIR}")
