## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

# The REAPER SDK's headers. Only jpr/common/reaper_api.h includes its API
# functions header.
add_library(reaper_sdk INTERFACE)
target_include_directories(reaper_sdk INTERFACE "${REAPER_EXTENSION_SDK}")

add_subdirectory(jpr)
