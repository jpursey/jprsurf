// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/cost_measurement.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

std::vector<CostMeasurement> MakeMeasurements() {
  std::vector<CostMeasurement> measurements;
  for (const char* name : {"A", "B", "A", "C"}) {
    measurements.push_back({.name = name});
  }
  return measurements;
}

std::vector<std::string> GetNames(
    const std::vector<CostMeasurement>& measurements) {
  std::vector<std::string> names;
  for (const CostMeasurement& measurement : measurements) {
    names.push_back(measurement.name);
  }
  return names;
}

TEST(CostMeasurementTest, AnEmptyFilterKeepsEveryMeasurement) {
  EXPECT_THAT(GetNames(FilterCostMeasurements(MakeMeasurements(), "")),
              ElementsAre("A", "B", "A", "C"));
}

TEST(CostMeasurementTest, AFilterKeepsTheNamesItLists) {
  EXPECT_THAT(GetNames(FilterCostMeasurements(MakeMeasurements(), "C:A")),
              ElementsAre("A", "A", "C"));
}

TEST(CostMeasurementTest, AFilterMatchesWholeNamesOnly) {
  EXPECT_THAT(FilterCostMeasurements(MakeMeasurements(), "AB:"), IsEmpty());
}

}  // namespace
}  // namespace jpr
