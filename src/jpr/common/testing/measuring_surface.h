// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/time/time.h"
#include "jpr/common/testing/cost_measurement.h"
#include "jpr/common/testing/fake_project.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// MeasuringEnvironment
//
// What MeasuringSurface needs from where it runs: REAPER's test install, or the
// fake REAPER in tests (FakeMeasuringEnvironment), which fakes its clocks too.
//==============================================================================

class MeasuringEnvironment {
 public:
  virtual ~MeasuringEnvironment() = default;

  // Opens the project `build` makes as the current project, in place of the
  // one open.
  virtual void OpenProject(absl::FunctionRef<void(FakeProject&)> build) = 0;

  // Returns the wall time, from any fixed point.
  virtual absl::Duration ReadTime() = 0;

  // Returns how long the calling thread has run for, not counting the time it
  // waited or was switched out.
  virtual absl::Duration ReadThreadTime() = 0;

  // Adds `text` to the results, which come a part at a time, as each
  // measurement is done, so a run that stops part way keeps what it measured.
  virtual void Report(std::string_view text) = 0;

  // Called once, from the surface's run, when every measurement is done and
  // reported. The surface does nothing after this.
  virtual void Finish() = 0;
};

//==============================================================================
// MeasuringSurface
//
// A control surface whose runs measure what REAPER's calls cost (see
// docs/worklog/measure_reaper_costs.md), one measurement after another, each on
// the project it builds, a run at a time. It runs on REAPER's UI thread, where
// REAPER does its own work between runs, so for each measurement it times:
// - The call: the wall time inside it, as a call that waits holds up the UI
//   thread as much as one that works. A read's time is per call. Inside a
//   deferred refresh, the end of the deferred refresh is timed apart.
// - Later work, for a change: the UI thread's time from the start of the run
//   that made the call through the runs after it (a window), less the call,
//   against the median of windows with no call, which alternate with them.
// Each is reported as the median of its samples, with the 10th and 90th
// percentiles.
//==============================================================================

class MeasuringSurface final : public IReaperControlSurface {
 public:
  struct Options {
    // How many samples each measurement takes, at least 1.
    int samples = 20;

    // How many runs a window of later work spans, at least 1. 13 covers the
    // undo point REAPER adds for a fader about 250ms after it moves.
    int window_runs = 13;

    // How many runs to wait after opening a project before measuring.
    int settle_runs = 30;
  };

  // Measures `measurements`, at least one, in order, from the surface's runs.
  // `environment` must outlive this.
  MeasuringSurface(MeasuringEnvironment* environment,
                   std::vector<CostMeasurement> measurements,
                   const Options& options);
  MeasuringSurface(const MeasuringSurface&) = delete;
  MeasuringSurface& operator=(const MeasuringSurface&) = delete;
  ~MeasuringSurface() override;

  // IReaperControlSurface
  const char* GetTypeString() override { return "MEASURING"; }
  const char* GetDescString() override { return "Measuring surface"; }
  const char* GetConfigString() override { return ""; }
  void Run() override;

 private:
  // What a run does.
  enum class StepKind {
    kOpen,      // Opens the measurement's project, and prepares it.
    kWait,      // Nothing.
    kRead,      // Times a read.
    kCall,      // Makes a change, starting a window.
    kUndo,      // Undoes the change, starting a window that isn't measured.
    kBaseline,  // Starts a window with no call.
  };

  struct Step {
    int measurement = 0;
    StepKind kind = StepKind::kWait;
  };

  // A change's window.
  struct Window {
    int run = 0;  // The run that made the change, which starts the window.

    // The UI thread's time making the change, and ending the deferred refresh
    // it was made in, which isn't later work.
    absl::Duration thread_time;
  };

  // A measurement's samples.
  struct Samples {
    std::vector<absl::Duration> calls;  // Per call, for a read.
    std::vector<absl::Duration> ends;   // Of the deferred refresh, if in one.
    std::vector<Window> windows;
    std::vector<int> baseline_runs;  // The runs starting a window with no call.
  };

  // Adds the steps that measure measurement `index`.
  void AddSteps(int index);
  void AddWindow(int index, StepKind kind);

  void OpenProject(int index);
  void TimeRead(int index);
  void TimeCall(int index, int run);

  // Returns the UI thread's time in the window starting at `run`.
  absl::Duration GetWindowTime(int run) const;

  // Reports the results of measurement `index`.
  void Report(int index);

  MeasuringEnvironment* const environment_;
  const Options options_;
  std::vector<CostMeasurement> measurements_;
  std::vector<Samples> samples_;  // One for each measurement.
  std::vector<Step> steps_;       // One for each run, in order.

  // The UI thread's time at the start of each run, and the run after the last
  // step, which ends its window.
  std::vector<absl::Duration> thread_times_;
};

}  // namespace jpr
