// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "absl/time/time.h"

namespace jpr {

// Returns the user's local time zone, from Windows.
//
// Use this rather than absl::LocalTimeZone(), which is UTC inside REAPER.
absl::TimeZone GetLocalTimeZone();

}  // namespace jpr
