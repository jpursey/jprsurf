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
  call_count_test.cc
  default_config_test_test.cc
  global_test.cc
  send_mode_test.cc
  track_list_test.cc
  track_strip_test.cc
)

gb_add_library(jpr_default_config)

# The benchmarks under the fake, and the target that runs them, writing to
# benchmark/ in the build tree (see "Benchmarks" in
# docs/testing_and_profiling.md). They aren't a ctest test, as their times
# depend on the machine and its load.

set(jpr_benchmark_DEPS
  absl::function_ref
  absl::strings
  absl::time
  gb_base
  gtest
  jpr_default_config
)

add_executable(jpr_benchmark benchmark.cc)
gb_set_target_properties(jpr_benchmark)
add_dependencies(jpr_benchmark jpr_build_info)
target_include_directories(jpr_benchmark PRIVATE "${JPR_BUILD_INFO_DIR}")

add_custom_target(benchmark
  COMMAND jpr_benchmark "${CMAKE_BINARY_DIR}/benchmark"
  DEPENDS jpr_benchmark
  USES_TERMINAL
  COMMENT "Running the benchmarks under the fake..."
)
