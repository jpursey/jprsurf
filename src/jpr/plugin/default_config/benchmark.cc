// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// JPRSurf's own time, under the fake REAPER, for the default config's steady
// states and the gestures of a session at the surface (see "Benchmarks" in
// docs/testing_and_profiling.md). The benchmark target runs this, which isn't
// a ctest test, as its times depend on the machine and its load.
//
// It takes the directory to write to as its only argument (beside gtest's
// flags). Each benchmark writes its profile there, named for it, and the
// summary, summary.txt, has one line for each benchmark run.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "absl/time/time.h"
#include "gb/base/init_logging.h"
#include "gb/base/local_time.h"
#include "gb/file/local_file_protocol.h"
#include "gtest/gtest.h"
#include "jpr/common/file_system.h"
#include "jpr/common/reaper_profiler.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/large_project.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/build_info.h"
#include "jpr/plugin/default_config/default_config_test.h"

namespace jpr {
namespace {

using Button = FakeXTouch::Button;
using StripButton = FakeXTouch::StripButton;

// The name the profiles and the summary are headed with.
constexpr char kName[] = "JPRSurf";

// Where the profiles and the summary are written.
std::filesystem::path g_output_dir;

// The summary's line for each benchmark, in the order they ran.
std::string g_summary_lines;

// Returns a summary line, with `name` and the rest of the columns given.
std::string FormatSummaryLine(std::string_view name, std::string_view runs,
                              std::string_view per_run,
                              std::string_view actions,
                              std::string_view per_action) {
  return absl::StrFormat("%-40s %6s %11s %7s %14s\n", name, runs, per_run,
                         actions, per_action);
}

// Registers g_output_dir as "benchmark" in GetFileSystem(). The fake REAPER
// resets the file system, so it is registered for each benchmark, after its
// fake is created, and again for the summary, once they have all run. Returns
// false if it couldn't.
bool RegisterOutputDir() {
  return GetFileSystem()->Register(gb::LocalFileProtocol::Create(g_output_dir),
                                   "benchmark");
}

// Writes the summary to g_output_dir, and prints it. Returns false if it
// couldn't be written.
bool WriteSummary() {
  const std::string summary = absl::StrCat(
      kName, " benchmark\n", "Build:    ", kBuild, "\n", "Date:     ",
      absl::FormatTime("%Y-%m-%d %H:%M", absl::Now(), gb::GetLocalTimeZone()),
      "\n\n",
      FormatSummaryLine("Benchmark", "Runs", "JPRSurf/run", "Actions",
                        "JPRSurf/action"),
      g_summary_lines);
  std::fputs(summary.c_str(), stdout);

  if (!RegisterOutputDir() ||
      !GetFileSystem()->WriteFile("benchmark:/summary.txt", summary)) {
    std::fprintf(stderr, "Failed to write summary.txt in %s.\n",
                 g_output_dir.string().c_str());
    return false;
  }
  return true;
}

//==============================================================================
// Benchmark
//
// A benchmark of the default config on the large project (see
// AddLargeProject()), with the profiler on. Each sets up the state it starts
// from, and then times either a steady state (TimeIdle() or TimeRuns()) or an
// action (TimeActions()). An action's time is beyond the idle time of its runs
// (see "Benchmarks" in docs/testing_and_profiling.md).
//==============================================================================

class Benchmark : public DefaultConfigTest {
 protected:
  static constexpr int kBus = 0;     // T1, with 31 receives.
  static constexpr int kFolder = 4;  // T5, with 9 tracks.

  // The runs a steady state is timed for, and the actions an action is timed
  // for.
  static constexpr int kRuns = 100 * FakeReaper::kRunsPerSecond;
  static constexpr int kActions = 100;

  Benchmark();

  // Fails the benchmark if it timed nothing.
  void TearDown() override;

  // Times kRuns runs with nothing happening.
  void TimeIdle();

  // Times kRuns calls of `run`, which makes one run.
  void TimeRuns(absl::FunctionRef<void()> run);

  // Times kActions actions, made by calling `act` and `undo` in turn, where
  // `undo` leaves the surface as `act` found it, so each pair does the same
  // work. A `toggle` does the same work each time it is made, such as by
  // undoing itself. The profile is of the actions, after
  // kRuns idle runs that time the state they start from.
  void TimeActions(absl::FunctionRef<void()> toggle);
  void TimeActions(absl::FunctionRef<void()> act,
                   absl::FunctionRef<void()> undo);

 private:
  // Starts timing, leaving out everything before it, such as setting up.
  void Start();

  // Makes kRuns runs with nothing happening.
  void RunIdle();

  // Ends timing, adding the benchmark's line to the summary, and writes its
  // profile. With `idle_per_run`, the benchmark timed kActions actions, and the
  // line has JPRSurf's time per action, beyond `idle_per_run` for each run.
  void Finish(std::optional<absl::Duration> idle_per_run = std::nullopt);

  const std::string name_;

  // The profile is of the benchmark only (see Start()), and is written when
  // Finish() destroys it. It is created before the surface, so it times the
  // MIDI ports the surface opens, and after the fixture's CallCounter, as its
  // hooks go over the counter's.
  std::optional<ReaperProfiler> profiler_;
};

Benchmark::Benchmark()
    : name_(::testing::UnitTest::GetInstance()->current_test_info()->name()) {
  EXPECT_TRUE(RegisterOutputDir());
  profiler_.emplace(ReaperProfiler::Options{
      .path = absl::StrCat("benchmark:/", name_, ".txt"),
      .name = kName,
      .build = kBuild});
  AddLargeProject(&reaper_.GetProject());
  AddSurface();
}

void Benchmark::TearDown() {
  if (profiler_.has_value() && !HasFailure()) {
    ADD_FAILURE() << "The benchmark timed nothing.";
  }
  DefaultConfigTest::TearDown();
}

void Benchmark::TimeIdle() {
  Start();
  RunIdle();
  Finish();
}

void Benchmark::TimeRuns(absl::FunctionRef<void()> run) {
  Start();
  for (int i = 0; i < kRuns; ++i) {
    run();
  }
  Finish();
}

void Benchmark::TimeActions(absl::FunctionRef<void()> toggle) {
  TimeActions(toggle, toggle);
}

void Benchmark::TimeActions(absl::FunctionRef<void()> act,
                            absl::FunctionRef<void()> undo) {
  Start();
  RunIdle();
  const absl::Duration idle_per_run = profiler_->GetRunSplit().extension;

  Start();
  for (int i = 0; i < kActions / 2; ++i) {
    act();
    undo();
  }
  Finish(idle_per_run);
}

void Benchmark::Start() { profiler_->Reset(); }

void Benchmark::RunIdle() {
  for (int i = 0; i < kRuns; ++i) {
    surface_->Run();
  }
}

void Benchmark::Finish(std::optional<absl::Duration> idle_per_run) {
  const ReaperProfiler::RunSplit split = profiler_->GetRunSplit();
  std::string actions = "-";
  std::string per_action = "-";
  if (idle_per_run.has_value()) {
    actions = absl::StrCat(kActions);
    per_action = FormatRunTime((split.extension - *idle_per_run) * split.runs /
                               kActions);
  }
  absl::StrAppend(
      &g_summary_lines,
      FormatSummaryLine(name_, absl::StrCat(split.runs),
                        FormatRunTime(split.extension), actions, per_action));
  profiler_.reset();
}

//------------------------------------------------------------------------------
// Steady states
//------------------------------------------------------------------------------

TEST_F(Benchmark, IdleAtTheTop) { TimeIdle(); }

TEST_F(Benchmark, IdleInAFolder) {
  DoublePressSelect(kFolder);
  TimeIdle();
}

// The timecode shows the play position, which moves on each run, as it does
// in REAPER.
TEST_F(Benchmark, IdleWhilePlaying) {
  FakeProject& project = reaper_.GetProject();
  project.SetPlayState(1);
  RunUntilShown();
  TimeRuns([&] {
    project.SetPlayPosition(project.GetPlayPosition() +
                            absl::ToDoubleSeconds(FakeReaper::GetRunTime()));
    surface_->Run();
  });
}

TEST_F(Benchmark, IdleInSendReceiveModeOnABus) {
  EnterSendMode(kBus);
  TimeIdle();
}

//------------------------------------------------------------------------------
// Actions
//
// Each kind of gesture in a session at the surface. A gesture on a strip is on
// the X-Touch's first strip (strip 8, T9) unless it needs another.
//------------------------------------------------------------------------------

// Holding the button on T7, on the extender, and tapping it on T10, on the
// X-Touch, which sets T7 to T10 one way, then the other.
TEST_F(Benchmark, MutingARange) {
  TimeActions([&] { PressRange(StripButton::kMute, 6, 9); });
}

TEST_F(Benchmark, SoloingARange) {
  TimeActions([&] { PressRange(StripButton::kSolo, 6, 9); });
}

TEST_F(Benchmark, ArmingARange) {
  TimeActions([&] { PressRange(StripButton::kRec, 6, 9); });
}

TEST_F(Benchmark, SelectingARange) {
  TimeActions([&] { SelectRange(0, 11); }, [&] { SelectRange(4, 7); });
}

TEST_F(Benchmark, TappingMute) {
  TimeActions([&] { Tap(xtouch_, StripButton::kMute, 0); });
}

TEST_F(Benchmark, TappingSolo) {
  TimeActions([&] { Tap(xtouch_, StripButton::kSolo, 0); });
}

TEST_F(Benchmark, TappingRecordArm) {
  TimeActions([&] { Tap(xtouch_, StripButton::kRec, 0); });
}

TEST_F(Benchmark, TappingSelect) {
  TimeActions([&] { TapSelect(8); }, [&] { TapSelect(9); });
}

TEST_F(Benchmark, GoingIntoAFolderAndBack) {
  TimeActions([&] { DoublePressSelect(kFolder); },
              [&] { Tap(xtouch_, Button::kGlobal); });
}

TEST_F(Benchmark, EnteringSendReceiveModeAndBack) {
  TapSelect(kBus);
  TimeActions([&] { Tap(xtouch_, Button::kAssignSend); },
              [&] { Tap(xtouch_, Button::kAssignTrack); });
}

// On the bus, whose receives are on strips 0-14 (the extender first).
TEST_F(Benchmark, TappingARouteMute) {
  EnterSendMode(kBus);
  TimeActions([&] { Tap(*xtouch_ext_, StripButton::kMute, 0); });
}

// Includes the undo point added once the route stops moving.
TEST_F(Benchmark, MovingARouteFader) {
  EnterSendMode(kBus);
  TimeActions(
      [&] {
        MoveFader(*xtouch_ext_, 0, 0);
        WaitForUndo();
      },
      [&] {
        MoveFader(*xtouch_ext_, 0, kFader0dB);
        WaitForUndo();
      });
}

// Includes the undo point added once the route stops moving.
TEST_F(Benchmark, TurningARoutePot) {
  EnterSendMode(kBus);
  TimeActions(
      [&] {
        xtouch_ext_->TurnPot(0, 21);
        RunUntilShown();
        WaitForUndo();
      },
      [&] {
        xtouch_ext_->TurnPot(0, -21);
        RunUntilShown();
        WaitForUndo();
      });
}

TEST_F(Benchmark, PickingWithSendHeldAndBack) {
  TimeActions([&] { PickWithSendHeld(kFolder); },
              [&] { Tap(xtouch_, Button::kAssignTrack); });
}

TEST_F(Benchmark, GoingIntoAFolderAndHoldingGlobal) {
  TimeActions([&] { DoublePressSelect(kFolder); },
              [&] { LongPress(xtouch_, Button::kGlobal); });
}

TEST_F(Benchmark, BankChange) {
  TimeActions([&] { Tap(xtouch_, Button::kBankRight); },
              [&] { Tap(xtouch_, Button::kBankLeft); });
}

TEST_F(Benchmark, Channel) {
  TimeActions([&] { Tap(xtouch_, Button::kChannelRight); },
              [&] { Tap(xtouch_, Button::kChannelLeft); });
}

// On the eight tracks a range selects.
TEST_F(Benchmark, SettingTheSelectedTracksAutomationMode) {
  SelectRange(0, 7);
  TimeActions([&] { Tap(xtouch_, Button::kAutoWrite); },
              [&] { Tap(xtouch_, Button::kAutoRead); });
}

TEST_F(Benchmark, TurningTheAutomationOverrideOnAndOff) {
  TimeActions([&] { Tap(xtouch_, Button::kAutoGroup); });
}

TEST_F(Benchmark, ForwardAndRewind) {
  TimeActions([&] { Tap(xtouch_, Button::kForward); },
              [&] { Tap(xtouch_, Button::kRewind); });
}

TEST_F(Benchmark, TogglingCycle) {
  TimeActions([&] { Tap(xtouch_, Button::kCycle); });
}

TEST_F(Benchmark, SteppingTheTimecodeMode) {
  TimeActions([&] { Tap(xtouch_, Button::kShowTimeBeats); });
}

//------------------------------------------------------------------------------
// Other gestures and events
//------------------------------------------------------------------------------

TEST_F(Benchmark, MovingAFader) {
  TimeActions([&] { MoveFader(xtouch_, 0, 0); },
              [&] { MoveFader(xtouch_, 0, kFader0dB); });
}

TEST_F(Benchmark, TurningAPot) {
  TimeActions(
      [&] {
        xtouch_.TurnPot(0, 21);
        RunUntilShown();
      },
      [&] {
        xtouch_.TurnPot(0, -21);
        RunUntilShown();
      });
}

TEST_F(Benchmark, TrackListRefresh) {
  TimeActions([&] {
    surface_->SetTrackListChange();
    RunUntilShown();
  });
}

}  // namespace
}  // namespace jpr

int main(int argc, char** argv) {
  // Only errors go to stderr, rather than everything the surface logs.
  gb::InitLogging();
  ::testing::InitGoogleTest(&argc, argv);
  if (argc != 2) {
    std::fprintf(stderr, "Usage: %s [gtest flags] <output directory>\n",
                 argv[0]);
    return 1;
  }
  jpr::g_output_dir = argv[1];
  std::error_code error;
  std::filesystem::create_directories(jpr::g_output_dir, error);
  if (error) {
    std::fprintf(stderr, "Failed to create %s: %s\n", argv[1],
                 error.message().c_str());
    return 1;
  }
  const int result = RUN_ALL_TESTS();
  if (!jpr::WriteSummary()) {
    return 1;
  }
  return result;
}
