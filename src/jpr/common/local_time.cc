// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/local_time.h"

#include <windows.h>

#include "absl/time/time.h"

namespace jpr {

absl::TimeZone GetLocalTimeZone() {
  TIME_ZONE_INFORMATION tzi;
  DWORD result = GetTimeZoneInformation(&tzi);
  if (result == TIME_ZONE_ID_INVALID) {
    return absl::UTCTimeZone();
  }

  char tz_name[128];
  WideCharToMultiByte(CP_UTF8, 0, tzi.StandardName, -1, tz_name,
                      sizeof(tz_name), nullptr, nullptr);
  absl::TimeZone tz;
  if (absl::LoadTimeZone(tz_name, &tz)) {
    return tz;
  }

  // Windows biases are minutes to add to local time to get UTC, the opposite
  // of an offset from UTC.
  int bias_minutes =
      tzi.Bias +
      (result == TIME_ZONE_ID_DAYLIGHT ? tzi.DaylightBias : tzi.StandardBias);
  return absl::FixedTimeZone(-bias_minutes * 60);
}

}  // namespace jpr
