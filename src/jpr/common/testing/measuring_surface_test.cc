// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/measuring_surface.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gb/base/function_hook.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/cost_measurement.h"
#include "jpr/common/testing/fake_measuring_environment.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::EndsWith;
using ::testing::HasSubstr;
using ::testing::IsEmpty;
using ::testing::Not;

// The environment a test's hooks add work to.
FakeMeasuringEnvironment* g_environment = nullptr;

// Stands in for PreventUIRefresh(), so ending a deferred refresh takes kEnd of
// work.
struct EndWorkHook {
  static constexpr absl::Duration kEnd = absl::Microseconds(200);

  template <typename Function>
  void Call(Function original, int prevent_count) {
    original(prevent_count);
    if (prevent_count < 0) {
      g_environment->Work(kEnd);
    }
  }
};

class MeasuringSurfaceTest : public ::testing::Test {
 protected:
  // The UI thread's work between runs, with no call.
  static constexpr absl::Duration kIdle = absl::Microseconds(50);

  static constexpr MeasuringSurface::Options kOptions = {
      .samples = 5, .window_runs = 3, .settle_runs = 2};

  MeasuringSurfaceTest() { g_environment = &environment_; }
  ~MeasuringSurfaceTest() override { g_environment = nullptr; }

  // Returns a change that works for `call_us`, and leaves `later_us` of work
  // for the UI thread before the next run.
  CostMeasurement MakeChange(std::string name, int call_us, int later_us) {
    return MakeChange(std::move(name), [this, call_us, later_us] {
      environment_.Work(absl::Microseconds(call_us));
      later_ += absl::Microseconds(later_us);
      call_runs_.push_back(run_);
    });
  }

  // Returns a change that makes `call`.
  CostMeasurement MakeChange(std::string name,
                             absl::AnyInvocable<void()> call) {
    return {.name = std::move(name), .call = std::move(call)};
  }

  // Creates the surface for `measurements`.
  void AddSurface(std::vector<CostMeasurement> measurements) {
    surface_ = std::make_unique<MeasuringSurface>(
        &environment_, std::move(measurements), kOptions);
  }

  // Runs the surface once, as REAPER would, after kIdle of work, and any later
  // work.
  void Run() {
    environment_.Work(kIdle + std::exchange(later_, absl::ZeroDuration()));
    surface_->Run();
    reaper_.EndEntryPoint();
  }

  // Creates the surface, and runs it until it finishes. Returns how many runs
  // it took.
  int Measure(std::vector<CostMeasurement> measurements) {
    AddSurface(std::move(measurements));
    for (run_ = 0; run_ < 10000 && !environment_.IsFinished(); ++run_) {
      Run();
    }
    return run_;
  }
  int Measure(CostMeasurement measurement) {
    std::vector<CostMeasurement> measurements;
    measurements.push_back(std::move(measurement));
    return Measure(std::move(measurements));
  }

  FakeReaper reaper_;
  FakeMeasuringEnvironment environment_{&reaper_};
  std::unique_ptr<MeasuringSurface> surface_;
  int run_ = 0;
  absl::Duration later_;
  std::vector<int> call_runs_;  // The runs each call was made in.
};

TEST_F(MeasuringSurfaceTest, TimesAChangeAndItsLaterWork) {
  Measure(MakeChange("Change", 100, 500));
  EXPECT_THAT(environment_.GetResults(),
              EndsWith("\nChange: call 100.0us (100.0us to 100.0us), "
                       "later 500.0us (500.0us to 500.0us)\n"));
}

TEST_F(MeasuringSurfaceTest, OpensTheProjectThenAlternatesCallAndBaseline) {
  // Opening, settling, then each sample's call window and baseline window,
  // and the run that ends the last window.
  EXPECT_EQ(Measure(MakeChange("Change", 100, 0)), 1 + 2 + 5 * (3 + 3) + 1);
  EXPECT_EQ(environment_.GetProjectsOpened(), 1);
  EXPECT_THAT(call_runs_, ElementsAre(3, 9, 15, 21, 27));
}

TEST_F(MeasuringSurfaceTest, TimesTheCallsWaitsButNotInLaterWork) {
  Measure(MakeChange("Waits", [this] {
    environment_.Wait(absl::Microseconds(1000));
    environment_.Work(absl::Microseconds(100));
  }));
  EXPECT_THAT(environment_.GetResults(),
              EndsWith("\nWaits: call 1.1ms (1.1ms to 1.1ms), "
                       "later 0ns (0ns to 0ns)\n"));
}

TEST_F(MeasuringSurfaceTest, ReportsTheMedianAnd10thAnd90thPercentiles) {
  Measure(MakeChange("Varies", [this, call_us = 0]() mutable {
    call_us += 10;
    environment_.Work(absl::Microseconds(call_us));
  }));
  EXPECT_THAT(environment_.GetResults(),
              HasSubstr("\nVaries: call 30.0us (10.0us to 50.0us)"));
}

TEST_F(MeasuringSurfaceTest, TimesTheEndOfADeferredRefreshApart) {
  gb::FunctionHook<&PreventUIRefresh, EndWorkHook> hook;
  std::vector<bool> prevented;
  CostMeasurement measurement = MakeChange("Deferred", [this, &prevented] {
    prevented.push_back(reaper_.IsUIRefreshPrevented());
    environment_.Work(absl::Microseconds(100));
  });
  measurement.in_deferred_refresh = true;
  measurement.condition = "3 tracks";
  Measure(std::move(measurement));

  EXPECT_THAT(prevented, ElementsAre(true, true, true, true, true));
  EXPECT_FALSE(reaper_.IsUIRefreshPrevented());
  EXPECT_THAT(environment_.GetResults(),
              EndsWith("\nDeferred, in a deferred refresh, 3 tracks: "
                       "call 100.0us (100.0us to 100.0us), "
                       "end 200.0us (200.0us to 200.0us), "
                       "later 0ns (0ns to 0ns)\n"));
}

TEST_F(MeasuringSurfaceTest, TimesAReadPerCallInOneRunASample) {
  EXPECT_EQ(Measure({.name = "Read",
                     .read =
                         [this] {
                           environment_.Work(absl::Microseconds(70));
                           return 10;
                         }}),
            1 + 2 + 5 + 1);
  EXPECT_THAT(environment_.GetResults(),
              EndsWith("\nRead: call 7.0us (7.0us to 7.0us)\n"));
}

TEST_F(MeasuringSurfaceTest, ResetsAfterTheCallsWindowOutsideTheBaseline) {
  std::vector<int> reset_runs;
  CostMeasurement measurement = MakeChange("Reset", 100, 0);
  measurement.reset = [this, &reset_runs] {
    reset_runs.push_back(run_);
    environment_.Work(absl::Microseconds(300));
    later_ += absl::Microseconds(1000);
  };

  EXPECT_EQ(Measure(std::move(measurement)), 1 + 2 + 5 * (3 + 3 + 3) + 1);
  EXPECT_THAT(call_runs_, ElementsAre(3, 12, 21, 30, 39));
  EXPECT_THAT(reset_runs, ElementsAre(6, 15, 24, 33, 42));
  EXPECT_THAT(environment_.GetResults(),
              EndsWith("\nReset: call 100.0us (100.0us to 100.0us), "
                       "later 0ns (0ns to 0ns)\n"));
}

TEST_F(MeasuringSurfaceTest, PreparesOnceTheProjectIsOpenWithoutTimingIt) {
  int track_count = 0;
  CostMeasurement measurement = MakeChange("Prepared", 100, 0);
  measurement.build = [](FakeProject& project) { project.AddTracks(4); };
  measurement.prepare = [this, &track_count] {
    track_count = CountTracks(nullptr);
    environment_.Work(absl::Microseconds(5000));
  };
  Measure(std::move(measurement));

  EXPECT_EQ(track_count, 4);
  EXPECT_THAT(environment_.GetResults(),
              EndsWith("\nPrepared: call 100.0us (100.0us to 100.0us), "
                       "later 0ns (0ns to 0ns)\n"));
}

TEST_F(MeasuringSurfaceTest, MeasuresEachOnItsOwnProjectInOrder) {
  std::vector<int> track_counts;
  std::vector<CostMeasurement> measurements;
  for (int tracks : {3, 7}) {
    measurements.push_back(
        {.name = absl::StrCat("Tracks", tracks),
         .build = [tracks](FakeProject& project) { project.AddTracks(tracks); },
         .call =
             [&track_counts] {
               track_counts.push_back(CountTracks(nullptr));
             }});
  }
  Measure(std::move(measurements));

  EXPECT_EQ(environment_.GetProjectsOpened(), 2);
  EXPECT_THAT(track_counts, ElementsAre(3, 3, 3, 3, 3, 7, 7, 7, 7, 7));
  const std::string& results = environment_.GetResults();
  const int first = static_cast<int>(results.find("\nTracks3: call "));
  const int second = static_cast<int>(results.find("\nTracks7: call "));
  EXPECT_GE(first, 0);
  EXPECT_GT(second, first);
}

TEST_F(MeasuringSurfaceTest, ReportsEachMeasurementOnceItIsDone) {
  std::vector<CostMeasurement> measurements;
  measurements.push_back(MakeChange("First", 100, 0));
  measurements.push_back(MakeChange("Second", 100, 0));
  AddSurface(std::move(measurements));

  // The first measurement's runs, and the run that opens the second's
  // project.
  for (int run = 0; run <= 1 + 2 + 5 * (3 + 3); ++run) {
    Run();
  }
  EXPECT_THAT(environment_.GetResults(), HasSubstr("\nFirst: call "));
  EXPECT_THAT(environment_.GetResults(), Not(HasSubstr("Second")));
  EXPECT_FALSE(environment_.IsFinished());
}

TEST_F(MeasuringSurfaceTest, FinishesInItsFirstRunWithNothingToMeasure) {
  EXPECT_EQ(Measure(std::vector<CostMeasurement>()), 1);
  EXPECT_TRUE(environment_.IsFinished());
}

TEST_F(MeasuringSurfaceTest, AMeasurementMustBeAReadOrAChange) {
  std::vector<CostMeasurement> measurements;
  measurements.push_back({.name = "Neither"});
  EXPECT_DEATH(AddSurface(std::move(measurements)), "Neither");
}

TEST_F(MeasuringSurfaceTest, DoesNothingOnceFinished) {
  Measure(MakeChange("Change", 100, 0));
  const std::string results = environment_.GetResults();
  call_runs_.clear();

  surface_->Run();
  surface_->Run();
  EXPECT_EQ(environment_.GetResults(), results);
  EXPECT_THAT(call_runs_, IsEmpty());
}

}  // namespace
}  // namespace jpr
