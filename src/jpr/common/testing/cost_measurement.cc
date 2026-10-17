// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/cost_measurement.h"

#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_split.h"

namespace jpr {

std::vector<CostMeasurement> FilterCostMeasurements(
    std::vector<CostMeasurement> measurements, std::string_view filter) {
  if (filter.empty()) {
    return measurements;
  }
  const std::vector<std::string_view> names = absl::StrSplit(filter, ':');
  std::erase_if(measurements, [&names](const CostMeasurement& measurement) {
    return !absl::c_linear_search(names, measurement.name);
  });
  return measurements;
}

}  // namespace jpr
