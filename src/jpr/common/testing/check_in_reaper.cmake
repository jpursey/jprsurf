## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Runs the contract tests in the test install of REAPER, the portable REAPER in
# the folder JPR_REAPER_CHECK_DIR names (see "Contract tests in REAPER" in
# docs/testing_and_profiling.md). The check_in_reaper target runs this, with
# CHECK_DLL set to the reaper_jprsurf_check DLL it built.
#
# It copies the DLL into the test install's UserPlugins, makes the
# RecordingSurface its only control surface, and runs REAPER until the DLL
# quits it. Then it prints what gtest printed, and fails if a test failed,
# REAPER didn't quit in time, or the DLL wrote no result. GTEST_FILTER, if set,
# picks the tests, as it does for any gtest binary.

# How long REAPER may take to start, run the tests, and quit, which
# JPR_REAPER_CHECK_TIMEOUT sets, in seconds. Opening each test's project takes
# about half a second.
set(timeout_secs 300)
if(NOT "$ENV{JPR_REAPER_CHECK_TIMEOUT}" STREQUAL "")
  set(timeout_secs "$ENV{JPR_REAPER_CHECK_TIMEOUT}")
  if(NOT timeout_secs MATCHES "^[0-9]+$")
    message(FATAL_ERROR "JPR_REAPER_CHECK_TIMEOUT must be a whole number of "
        "seconds, not \"${timeout_secs}\".")
  endif()
endif()

set(install_dir "$ENV{JPR_REAPER_CHECK_DIR}")
if(install_dir STREQUAL "")
  message(FATAL_ERROR "JPR_REAPER_CHECK_DIR isn't set. It names the test "
      "install of REAPER (see \"Contract tests in REAPER\" in "
      "docs/testing_and_profiling.md).")
endif()
file(TO_CMAKE_PATH "${install_dir}" install_dir)
set(reaper_exe "${install_dir}/reaper.exe")
set(reaper_ini "${install_dir}/reaper.ini")
if(NOT EXISTS "${reaper_exe}")
  message(FATAL_ERROR "${install_dir} has no reaper.exe. JPR_REAPER_CHECK_DIR "
      "must name a portable install of REAPER.")
endif()
if(NOT EXISTS "${reaper_ini}")
  message(FATAL_ERROR "${install_dir} has no reaper.ini. Run REAPER there once "
      "to set it up.")
endif()

# The DLL.
file(COPY "${CHECK_DLL}" DESTINATION "${install_dir}/UserPlugins")

# The RecordingSurface, as the only control surface. REAPER keeps each surface
# as its type and its config string, and creates none without a config string,
# so it has one it ignores.
file(READ "${reaper_ini}" ini)
string(REGEX REPLACE "\ncsurf_(cnt|[0-9]+)=[^\r\n]*\r?" "" surfaces_removed
    "${ini}")
string(REGEX REPLACE "(\\[[Rr][Ee][Aa][Pp][Ee][Rr]\\])(\r?\n)"
    "\\1\\2csurf_cnt=1\\2csurf_0=RECORDING null\\2" new_ini
    "${surfaces_removed}")
if(new_ini STREQUAL surfaces_removed)
  message(FATAL_ERROR "${reaper_ini} has no [REAPER] section.")
endif()
if(NOT new_ini STREQUAL ini)
  file(WRITE "${reaper_ini}" "${new_ini}")
endif()

# Where the DLL writes its files.
set(check_dir "${install_dir}/check")
file(REMOVE_RECURSE "${check_dir}")
file(MAKE_DIRECTORY "${check_dir}")
file(TO_NATIVE_PATH "${check_dir}" native_check_dir)
set(ENV{JPRSURF_CHECK_DIR} "${native_check_dir}")

execute_process(
  COMMAND "${reaper_exe}" -newinst -nosplash -new
  TIMEOUT ${timeout_secs}
  RESULT_VARIABLE reaper_result
)

if(EXISTS "${check_dir}/output.txt")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${check_dir}/output.txt")
endif()
set(failure "")
if(NOT reaper_result STREQUAL "0")
  set(failure "REAPER didn't quit within ${timeout_secs} seconds, or failed: "
      "${reaper_result}")
elseif(NOT EXISTS "${check_dir}/result.txt")
  set(failure "REAPER quit without running the tests.")
else()
  file(STRINGS "${check_dir}/result.txt" result LIMIT_COUNT 1)
  if(NOT result STREQUAL "0")
    set(failure "The contract tests failed in REAPER.")
  endif()
endif()
if(NOT failure STREQUAL "")
  # How far the DLL got, and why it stopped.
  message(STATUS "log.txt:")
  if(EXISTS "${check_dir}/log.txt")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${check_dir}/log.txt")
  else()
    message(STATUS "(none, so REAPER didn't load reaper_jprsurf_check.dll)")
  endif()
  message(FATAL_ERROR "${failure}")
endif()
message(STATUS "The contract tests passed in REAPER.")
