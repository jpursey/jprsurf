// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

// The entry point of reaper_jpr_measure, the DLL that measures what REAPER's
// calls cost inside the test install of REAPER (see test_install.h, and
// docs/worklog/measure_reaper_costs.md). REAPER creates the MeasuringSurface
// at startup, which measures every cost (GetCostMeasurements()), or those
// JPR_MEASURE_FILTER names (see FilterCostMeasurements()), and writes the
// results to output.txt as each measurement is done.

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/functional/function_ref.h"
#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gb/thread/cpu_ticks.h"
#include "gb/thread/thread.h"
#include "jpr/common/testing/cost_measurement.h"
#include "jpr/common/testing/cost_measurements.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/measuring_surface.h"
#include "jpr/common/testing/test_install.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

// About 15 minutes for every measurement.
constexpr MeasuringSurface::Options kOptions = {
    .samples = 12, .window_runs = 13, .settle_runs = 15};

// Returns what JPR_MEASURE_FILTER is set to, or empty if it isn't.
std::string ReadFilter() {
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, "JPR_MEASURE_FILTER") != 0 ||
      value == nullptr) {
    return {};
  }
  std::string filter(value);
  free(value);
  return filter;
}

// The surface's environment in the test install, timed by the CPU's
// timestamp counter.
class ReaperEnvironment final : public MeasuringEnvironment {
 public:
  // Created on REAPER's UI thread. `problem` is why there is nothing to
  // measure, if there isn't.
  explicit ReaperEnvironment(std::string problem)
      : problem_(std::move(problem)),
        ticks_per_second_(gb::GetCpuTicksPerSecond()),
        start_ticks_(gb::ReadCpuTicks()),
        start_cycles_(gb::GetThisThreadCycles()) {}

  // MeasuringEnvironment
  void OpenProject(absl::FunctionRef<void(FakeProject&)> build) override {
    std::unique_ptr<FakeProject> project = FakeProject::Create();
    build(*project);
    if (!OpenProjectFile(*project)) {
      failed_ = true;
    }
  }
  absl::Duration ReadTime() override {
    return ToDuration(gb::ReadCpuTicks() - start_ticks_);
  }
  absl::Duration ReadThreadTime() override {
    return ToDuration(gb::GetThisThreadCycles() - start_cycles_);
  }
  void Report(std::string_view text) override {
    absl::StrAppend(&output_, text);
    if (!WriteRunFile("output.txt", output_)) {
      LOG(ERROR) << "Failed to write output.txt";
      failed_ = true;
    }
  }
  void Finish() override {
    if (!problem_.empty()) {
      Report(absl::StrCat("Nothing to measure: ", problem_, "\n"));
    }
    if (failed_) {
      Report("A project or the results couldn't be written (see log.txt).\n");
    }
    FinishRun(problem_.empty() && !failed_);
  }

 private:
  absl::Duration ToDuration(int64_t ticks) const {
    return absl::Seconds(static_cast<double>(ticks) / ticks_per_second_);
  }

  const std::string problem_;
  const double ticks_per_second_;
  const int64_t start_ticks_;
  const int64_t start_cycles_;
  std::string output_;
  bool failed_ = false;
};

// The surface's environment, which outlives it.
std::unique_ptr<ReaperEnvironment> g_environment;

IReaperControlSurface* CreateSurface(const char* type_string,
                                     const char* config_string,
                                     int* err_stats) {
  if (GetRunDir().empty()) {
    LOG(INFO) << "No run directory, so nothing to measure";
    return nullptr;
  }

  // With a problem, the surface measures nothing, and finishes in its first
  // run, failing.
  std::vector<CostMeasurement> measurements;
  std::string problem;
  if (!gb::GetThreadCapabilities().IsSet(gb::ThreadCapability::kCycles)) {
    problem = "this machine can't count a thread's cycles.";
  } else {
    measurements = FilterCostMeasurements(GetCostMeasurements(), ReadFilter());
    if (measurements.empty()) {
      problem = "no measurement matched JPR_MEASURE_FILTER.";
    }
  }
  LOG(INFO) << "Measuring " << measurements.size() << " costs";
  g_environment = std::make_unique<ReaperEnvironment>(std::move(problem));
  return new MeasuringSurface(g_environment.get(), std::move(measurements),
                              kOptions);
}

reaper_csurf_reg_t g_registration = {"MEASURING", "Measuring surface",
                                     &CreateSurface, nullptr};

}  // namespace
}  // namespace jpr

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* plugin_info) {
  return jpr::LoadTestInstall(plugin_info, &jpr::g_registration) ? 1 : 0;
}

}  // extern "C"
