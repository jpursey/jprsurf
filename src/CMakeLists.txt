## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(reaper_sdk_SOURCE
  reaper_sdk.cc
)
gb_add_library(reaper_sdk)
target_include_directories(reaper_sdk PUBLIC "${REAPER_EXTENSION_SDK}/sdk")

set(jprsurf_SOURCE
  plugin_load.cc
  jprsurf.cc
)

set(jprsurf_DEPS
  reaper_sdk
)

gb_add_shared_library(jprsurf)

add_custom_command(TARGET jprsurf POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:jprsurf>
    "${REAPER_PLUGINS_DIR}/jprsurf.dll"
    COMMENT "Deploying plugin to Reaper..."
)
