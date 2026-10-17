// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/functional/function_ref.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/measuring_surface.h"

namespace jpr {

//==============================================================================
// FakeMeasuringEnvironment
//
// A MeasuringSurface's environment in the fake REAPER: projects open in the
// fake, and the clocks move only as the test moves them, so a test decides
// what each call and run costs.
//==============================================================================

class FakeMeasuringEnvironment final : public MeasuringEnvironment {
 public:
  // `reaper` must outlive this.
  explicit FakeMeasuringEnvironment(FakeReaper* reaper) : reaper_(reaper) {}
  FakeMeasuringEnvironment(const FakeMeasuringEnvironment&) = delete;
  FakeMeasuringEnvironment& operator=(const FakeMeasuringEnvironment&) = delete;

  // The UI thread works for `time`, which moves both clocks.
  void Work(absl::Duration time) {
    time_ += time;
    thread_time_ += time;
  }

  // The UI thread waits for `time`, which moves only the wall clock.
  void Wait(absl::Duration time) { time_ += time; }

  int GetProjectsOpened() const { return projects_opened_; }
  bool IsFinished() const { return finished_; }

  // Returns everything reported, in order.
  const std::string& GetResults() const { return results_; }

  // MeasuringEnvironment
  void OpenProject(absl::FunctionRef<void(FakeProject&)> build) override {
    build(reaper_->NewProject());
    ++projects_opened_;
  }
  absl::Duration ReadTime() override { return time_; }
  absl::Duration ReadThreadTime() override { return thread_time_; }
  void Report(std::string_view text) override {
    absl::StrAppend(&results_, text);
  }
  void Finish() override { finished_ = true; }

 private:
  FakeReaper* const reaper_;
  absl::Duration time_;
  absl::Duration thread_time_;
  int projects_opened_ = 0;
  std::string results_;
  bool finished_ = false;
};

}  // namespace jpr
