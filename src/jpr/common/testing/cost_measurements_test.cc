// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/cost_measurements.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/strings/str_split.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/cost_measurement.h"
#include "jpr/common/testing/fake_measuring_environment.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/measuring_surface.h"
#include "jpr/common/testing/reaper_actions.h"

namespace jpr {
namespace {

using ::testing::SizeIs;

bool IsMeasured(const std::vector<CostMeasurement>& measurements,
                std::string_view function) {
  return absl::c_any_of(measurements,
                        [function](const CostMeasurement& measurement) {
                          return measurement.name == function;
                        });
}

bool IsUnmeasured(std::string_view function) {
  return absl::c_any_of(GetUnmeasuredFunctions(),
                        [function](const UnmeasuredFunction& unmeasured) {
                          return unmeasured.function == function;
                        });
}

TEST(CostMeasurementsTest, EveryFunctionOnTheListIsMeasuredOrSaysWhyNot) {
  const std::vector<CostMeasurement> measurements = GetCostMeasurements();
  for (std::string_view function : kReaperApiNames) {
    EXPECT_NE(IsMeasured(measurements, function), IsUnmeasured(function))
        << function << " must be measured, or listed as unmeasured, not both";
  }
}

TEST(CostMeasurementsTest, MeasuresOnlyFunctionsOnTheList) {
  for (const CostMeasurement& measurement : GetCostMeasurements()) {
    EXPECT_TRUE(absl::c_linear_search(kReaperApiNames, measurement.name))
        << measurement.name;
  }
  for (const UnmeasuredFunction& unmeasured : GetUnmeasuredFunctions()) {
    EXPECT_TRUE(absl::c_linear_search(kReaperApiNames, unmeasured.function))
        << unmeasured.function;
    EXPECT_FALSE(unmeasured.why.empty()) << unmeasured.function;
  }
}

TEST(CostMeasurementsTest, EachMeasurementRunsASampleUnderTheFake) {
  FakeReaper reaper;
  AddReaperActions(&reaper);
  FakeMeasuringEnvironment environment(&reaper);
  std::vector<CostMeasurement> measurements = GetCostMeasurements();
  const int count = static_cast<int>(measurements.size());
  MeasuringSurface surface(&environment, std::move(measurements),
                           {.samples = 1, .window_runs = 1, .settle_runs = 0});
  for (int run = 0; run < 100000 && !environment.IsFinished(); ++run) {
    surface.Run();
    reaper.EndEntryPoint();
  }
  ASSERT_TRUE(environment.IsFinished());

  // Each measurement's line, after the header's 4.
  const std::vector<std::string_view> lines =
      absl::StrSplit(environment.GetResults(), '\n', absl::SkipEmpty());
  EXPECT_THAT(lines, SizeIs(count + 4));
}

}  // namespace
}  // namespace jpr
