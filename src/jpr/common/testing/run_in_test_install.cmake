## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# Runs a DLL in the test install of REAPER, the portable REAPER in the folder
# JPR_REAPER_CHECK_DIR names (see "Contract tests in REAPER" in
# docs/testing_and_profiling.md), with:
# - RUN_DLL: the DLL that runs there, built on test_install.h, such as
#   reaper_jpr_check.
# - SURFACE_TYPE: the type of the control surface RUN_DLL registers.
# - EXTRA_DLLS, if set: other extensions to load beside it.
#
# It copies the DLLs into the test install's UserPlugins, makes SURFACE_TYPE
# its only control surface, and runs REAPER until RUN_DLL quits it. Then it
# removes the DLLs again, whether or not the run passed, prints the
# output.txt RUN_DLL wrote, and fails if RUN_DLL wrote no result or a failed
# one, or REAPER didn't quit in time.

foreach(var RUN_DLL SURFACE_TYPE)
  if("${${var}}" STREQUAL "")
    message(FATAL_ERROR "${var} isn't set.")
  endif()
endforeach()
get_filename_component(run_dll_name "${RUN_DLL}" NAME)

# How long REAPER may take to start, run, and quit, which
# JPR_REAPER_CHECK_TIMEOUT sets, in seconds.
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

# RUN_DLL's surface, as the only control surface. REAPER keeps each surface as
# its type and its config string, and creates none without a config string,
# so it has one it ignores.
file(READ "${reaper_ini}" ini)
string(REGEX REPLACE "\ncsurf_(cnt|[0-9]+)=[^\r\n]*\r?" "" surfaces_removed
    "${ini}")
string(REGEX REPLACE "(\\[[Rr][Ee][Aa][Pp][Ee][Rr]\\])(\r?\n)"
    "\\1\\2csurf_cnt=1\\2csurf_0=${SURFACE_TYPE} null\\2" new_ini
    "${surfaces_removed}")
if(new_ini STREQUAL surfaces_removed)
  message(FATAL_ERROR "${reaper_ini} has no [REAPER] section.")
endif()
if(NOT new_ini STREQUAL ini)
  file(WRITE "${reaper_ini}" "${new_ini}")
endif()

# The run directory, where the DLL writes its files.
set(run_dir "${install_dir}/run")
file(REMOVE_RECURSE "${run_dir}")
file(MAKE_DIRECTORY "${run_dir}")
file(TO_NATIVE_PATH "${run_dir}" native_run_dir)
set(ENV{JPR_REAPER_RUN_DIR} "${native_run_dir}")

# The DLLs, copied last, so nothing above can stop the run with them left
# behind. Each run removes them again, so a DLL from another run never loads
# beside them, writing over their files in the run directory.
set(dlls "${RUN_DLL}" ${EXTRA_DLLS})
file(COPY ${dlls} DESTINATION "${install_dir}/UserPlugins")

execute_process(
  COMMAND "${reaper_exe}" -newinst -nosplash -new
  TIMEOUT ${timeout_secs}
  RESULT_VARIABLE reaper_result
)

foreach(dll IN LISTS dlls)
  get_filename_component(dll_name "${dll}" NAME)
  set(installed_dll "${install_dir}/UserPlugins/${dll_name}")
  file(REMOVE "${installed_dll}")
  if(EXISTS "${installed_dll}")
    message(WARNING "Failed to remove ${installed_dll}, so REAPER will keep "
        "loading it in the test install. Remove it by hand.")
  endif()
endforeach()

if(EXISTS "${run_dir}/output.txt")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${run_dir}/output.txt")
endif()
set(failure "")
if(NOT reaper_result STREQUAL "0")
  set(failure "REAPER didn't quit within ${timeout_secs} seconds, or failed: "
      "${reaper_result}")
elseif(NOT EXISTS "${run_dir}/result.txt")
  set(failure "REAPER quit without a result from ${run_dll_name}.")
else()
  file(STRINGS "${run_dir}/result.txt" result LIMIT_COUNT 1)
  if(NOT result STREQUAL "0")
    set(failure "${run_dll_name} failed in REAPER.")
  endif()
endif()
if(NOT failure STREQUAL "")
  # How far the DLL got, and why it stopped.
  message(STATUS "log.txt:")
  if(EXISTS "${run_dir}/log.txt")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E cat "${run_dir}/log.txt")
  else()
    message(STATUS "(none, so REAPER didn't load ${run_dll_name})")
  endif()
  message(FATAL_ERROR "${failure}")
endif()
message(STATUS "${run_dll_name} passed in REAPER.")
