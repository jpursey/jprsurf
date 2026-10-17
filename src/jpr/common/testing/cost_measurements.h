// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>
#include <vector>

#include "absl/types/span.h"
#include "jpr/common/testing/cost_measurement.h"

namespace jpr {

// A function on the API list that isn't measured, and why.
struct UnmeasuredFunction {
  std::string_view function;
  std::string_view why;
};

// Returns every measurement of what REAPER's API costs, in the order they run
// (see docs/worklog/measure_reaper_costs.md): each function on the API list but
// those GetUnmeasuredFunctions() lists, with the conditions its cost depends
// on, and each action the extension runs, by Main_OnCommand(). Each is named
// for the function it calls.
std::vector<CostMeasurement> GetCostMeasurements();

// Returns the functions on the API list that GetCostMeasurements() doesn't
// measure, with why.
absl::Span<const UnmeasuredFunction> GetUnmeasuredFunctions();

}  // namespace jpr
