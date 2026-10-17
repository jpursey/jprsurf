// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/measuring_surface.h"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/time.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/reaper_profiler.h"

namespace jpr {
namespace {

// Returns the time `fraction` of the way through `times`, by rank: 0.5 is the
// median. `times` must not be empty.
absl::Duration GetPercentile(std::vector<absl::Duration> times,
                             double fraction) {
  std::sort(times.begin(), times.end());
  const int count = static_cast<int>(times.size());
  const int index =
      std::clamp(static_cast<int>(fraction * count), 0, count - 1);
  return times[index];
}

// Returns the median of `times`, with the 10th and 90th percentiles.
std::string FormatSpread(const std::vector<absl::Duration>& times) {
  return absl::StrCat(FormatRunTime(GetPercentile(times, 0.5)), " (",
                      FormatRunTime(GetPercentile(times, 0.1)), " to ",
                      FormatRunTime(GetPercentile(times, 0.9)), ")");
}

}  // namespace

MeasuringSurface::MeasuringSurface(MeasuringEnvironment* environment,
                                   std::vector<CostMeasurement> measurements,
                                   const Options& options)
    : environment_(environment),
      options_(options),
      measurements_(std::move(measurements)),
      samples_(measurements_.size()) {
  CHECK(!measurements_.empty());
  CHECK_GE(options_.samples, 1);
  CHECK_GE(options_.window_runs, 1);
  for (int i = 0; i < static_cast<int>(measurements_.size()); ++i) {
    const CostMeasurement& measurement = measurements_[i];
    CHECK(!measurement.read != !measurement.call)
        << measurement.name << " must be a read or a change, not both";
    AddSteps(i);
  }

  // Growing these in a run would add to the time measured.
  thread_times_.reserve(steps_.size() + 1);
  for (Samples& samples : samples_) {
    samples.calls.reserve(options_.samples);
    samples.ends.reserve(options_.samples);
    samples.windows.reserve(options_.samples);
    samples.baseline_runs.reserve(options_.samples);
  }
}

MeasuringSurface::~MeasuringSurface() = default;

void MeasuringSurface::AddSteps(int index) {
  steps_.push_back({.measurement = index, .kind = StepKind::kOpen});
  for (int i = 0; i < options_.settle_runs; ++i) {
    steps_.push_back({.measurement = index, .kind = StepKind::kWait});
  }
  const CostMeasurement& measurement = measurements_[index];
  for (int i = 0; i < options_.samples; ++i) {
    if (measurement.read) {
      steps_.push_back({.measurement = index, .kind = StepKind::kRead});
      continue;
    }
    AddWindow(index, StepKind::kCall);
    if (measurement.undo) {
      AddWindow(index, StepKind::kUndo);
    }
    AddWindow(index, StepKind::kBaseline);
  }
}

void MeasuringSurface::AddWindow(int index, StepKind kind) {
  steps_.push_back({.measurement = index, .kind = kind});
  for (int i = 1; i < options_.window_runs; ++i) {
    steps_.push_back({.measurement = index, .kind = StepKind::kWait});
  }
}

void MeasuringSurface::Run() {
  const int run = static_cast<int>(thread_times_.size());
  const int step_count = static_cast<int>(steps_.size());
  if (run > step_count) {
    return;
  }
  thread_times_.push_back(environment_->ReadThreadTime());

  // A measurement is done once the run after its last step starts.
  if (run == 0) {
    environment_->Report(absl::StrFormat(
        "REAPER's costs: the median of %d samples, with the 10th and 90th "
        "percentiles.\n"
        "- call: the time inside the call, per call for a read.\n"
        "- end: the time ending the deferred refresh the call was made in.\n"
        "- later: REAPER's work on its UI thread in the %d runs from the "
        "call, beyond the same runs with no call.\n\n",
        options_.samples, options_.window_runs));
  } else if (run == step_count ||
             steps_[run].measurement != steps_[run - 1].measurement) {
    Report(steps_[run - 1].measurement);
  }
  if (run == step_count) {
    environment_->Finish();
    return;
  }

  const Step& step = steps_[run];
  switch (step.kind) {
    case StepKind::kOpen:
      OpenProject(step.measurement);
      break;
    case StepKind::kWait:
      break;
    case StepKind::kRead:
      TimeRead(step.measurement);
      break;
    case StepKind::kCall:
      TimeCall(step.measurement, run);
      break;
    case StepKind::kUndo:
      measurements_[step.measurement].undo();
      break;
    case StepKind::kBaseline:
      samples_[step.measurement].baseline_runs.push_back(run);
      break;
  }
}

void MeasuringSurface::OpenProject(int index) {
  CostMeasurement& measurement = measurements_[index];
  environment_->OpenProject([&measurement](FakeProject& project) {
    if (measurement.build) {
      measurement.build(project);
    }
  });
  if (measurement.prepare) {
    measurement.prepare();
  }
}

void MeasuringSurface::TimeRead(int index) {
  const absl::Duration start = environment_->ReadTime();
  const int count = measurements_[index].read();
  const absl::Duration time = environment_->ReadTime() - start;
  CHECK_GE(count, 1) << measurements_[index].name << " made no calls";
  samples_[index].calls.push_back(time / count);
}

void MeasuringSurface::TimeCall(int index, int run) {
  CostMeasurement& measurement = measurements_[index];
  Samples& samples = samples_[index];
  const absl::Duration start_thread_time = environment_->ReadThreadTime();
  if (measurement.in_deferred_refresh) {
    PreventUIRefresh(1);
  }
  const absl::Duration start = environment_->ReadTime();
  measurement.call();
  const absl::Duration called = environment_->ReadTime();
  samples.calls.push_back(called - start);
  if (measurement.in_deferred_refresh) {
    PreventUIRefresh(-1);
    samples.ends.push_back(environment_->ReadTime() - called);
  }
  samples.windows.push_back(
      {.run = run,
       .thread_time = environment_->ReadThreadTime() - start_thread_time});
}

absl::Duration MeasuringSurface::GetWindowTime(int run) const {
  return thread_times_[run + options_.window_runs] - thread_times_[run];
}

void MeasuringSurface::Report(int index) {
  const CostMeasurement& measurement = measurements_[index];
  const Samples& samples = samples_[index];
  std::string text = measurement.name;
  if (measurement.in_deferred_refresh) {
    absl::StrAppend(&text, ", in a deferred refresh");
  }
  if (!measurement.condition.empty()) {
    absl::StrAppend(&text, ", ", measurement.condition);
  }
  absl::StrAppend(&text, ": call ", FormatSpread(samples.calls));
  if (!samples.ends.empty()) {
    absl::StrAppend(&text, ", end ", FormatSpread(samples.ends));
  }
  if (!samples.windows.empty()) {
    std::vector<absl::Duration> baselines;
    for (int run : samples.baseline_runs) {
      baselines.push_back(GetWindowTime(run));
    }
    const absl::Duration baseline = GetPercentile(baselines, 0.5);
    std::vector<absl::Duration> later;
    for (const Window& window : samples.windows) {
      later.push_back(GetWindowTime(window.run) - window.thread_time -
                      baseline);
    }
    absl::StrAppend(&text, ", later ", FormatSpread(later));
  }
  absl::StrAppend(&text, "\n");
  environment_->Report(text);
}

}  // namespace jpr
