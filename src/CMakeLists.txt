## Copyright (c) 2026 John Pursey
##
## Use of this source code is governed by an MIT-style License that can be found
## in the LICENSE file or at https://opensource.org/licenses/MIT.

set(jprsurf_SOURCE
  jprsurf.cc
)

set(jprsurf_DEPS
  gb_file
  gb_parse
)

gb_add_executable(jprsurf)
