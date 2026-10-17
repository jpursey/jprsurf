## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Checks that REAPER loads and unloads the plugin, in the test install of REAPER
# (see "Loading the plugin" in docs/testing_and_profiling.md). The
# check_load_in_reaper target runs this, with:
# - PLUGIN_DLL: the reaper_jprsurf DLL it built.
# - CHECK_DLL: the reaper_jpr_check DLL it built, which runs no tests.
# - RUN_SCRIPT: run_in_test_install.cmake, which runs the test install.
# - RUN_DIR: a folder in the build tree for the plugin's files.

file(REMOVE_RECURSE "${RUN_DIR}")
file(MAKE_DIRECTORY "${RUN_DIR}")
file(TO_NATIVE_PATH "${RUN_DIR}" native_run_dir)
set(ENV{APPDATA} "${native_run_dir}")
set(ENV{JPRSURF_TRACE} "1")
set(ENV{GTEST_FILTER} "-*")

execute_process(
  COMMAND "${CMAKE_COMMAND}" "-DRUN_DLL=${CHECK_DLL}" -DSURFACE_TYPE=RECORDING
      "-DEXTRA_DLLS=${PLUGIN_DLL}" -P "${RUN_SCRIPT}"
  RESULT_VARIABLE run_result
  OUTPUT_VARIABLE run_output
  ERROR_VARIABLE run_output
)

# Sets `failure_var` to why the check failed, or empty if it passed.
function(find_failure failure_var)
  set(${failure_var} "" PARENT_SCOPE)
  set(log "${RUN_DIR}/jprsurf.log")
  set(trace "${RUN_DIR}/jprsurf_trace.txt")
  if(EXISTS "${log}")
    file(READ "${log}" log_text)
    message(STATUS "jprsurf.log:\n${log_text}")
  endif()
  if(NOT run_result STREQUAL "0")
    message(STATUS "${run_output}")
    set(${failure_var} "The run in REAPER failed." PARENT_SCOPE)
    return()
  endif()
  if(NOT EXISTS "${log}")
    set(${failure_var} "There is no jprsurf.log, so REAPER didn't load the "
        "plugin." PARENT_SCOPE)
    return()
  endif()

  # Each line of the log starts with its severity: I, W, E, or F.
  if(log_text MATCHES "(^|\n)[EF]")
    set(${failure_var} "jprsurf.log has errors." PARENT_SCOPE)
    return()
  endif()
  if(NOT EXISTS "${trace}")
    set(${failure_var} "The plugin didn't write its trace." PARENT_SCOPE)
    return()
  endif()
  file(STRINGS "${trace}" trace_start LIMIT_COUNT 1)
  if(NOT trace_start MATCHES "^JPRSurf REAPER trace, started ")
    set(${failure_var} "The trace has no header." PARENT_SCOPE)
    return()
  endif()
  file(STRINGS "${trace}" trace_end REGEX "^Trace ended$")
  if(trace_end STREQUAL "")
    set(${failure_var} "The trace didn't end, so the plugin didn't unload."
        PARENT_SCOPE)
  endif()
endfunction()

find_failure(failure)
if(NOT failure STREQUAL "")
  message(FATAL_ERROR "${failure} The plugin's files are in ${RUN_DIR}.")
endif()
message(STATUS "REAPER loaded and unloaded the plugin.")
