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
  log_path.cc log_path.h
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
  runner.cc runner.h
  test_reset.cc test_reset.h
  timeline.cc timeline.h
  track.cc track.h
  track_state.h
  track_cache.cc track_cache.h
  undo.cc undo.h
  volume_utils.h
)

set(jpr_common_DEPS
  absl::base
  absl::log
  absl::time
  gb_base
  gb_config
  gb_profile
  reaper_sdk
)

set(jpr_common_TEST_SOURCE
  anchor_test.cc
  automation_test.cc
  color_test.cc
  control_surface_test.cc
  guid_test.cc
  midi_port_test.cc
  midi_ports_test.cc
  numbered_name_test.cc
  prefixed_name_test.cc
  reaper_api_test.cc
  reaper_trace_test.cc
  timeline_test.cc
  track_cache_test.cc
  track_test.cc
  undo_test.cc
)

set(jpr_common_TEST_DEPS
  jpr_common_testing
)

gb_add_library(jpr_common)
target_include_directories(jpr_common PUBLIC "../..")

add_subdirectory(testing)

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
