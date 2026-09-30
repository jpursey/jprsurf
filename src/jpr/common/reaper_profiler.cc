// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_profiler.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "jpr/common/local_time.h"

namespace jpr {

namespace {

// The profiler's budget per run is the larger of these.
constexpr absl::Duration kBudgetPerRun = absl::Microseconds(20);
constexpr double kBudgetFraction = 0.01;

}  // namespace

ReaperProfiler::ReaperProfiler(std::filesystem::path path)
    : path_(std::move(path)),
      start_time_(absl::Now()),
      profiler_({.budget_per_frame = kBudgetPerRun,
                 .budget_fraction = kBudgetFraction}) {}

ReaperProfiler::~ReaperProfiler() { WriteSnapshot(); }

void ReaperProfiler::WriteSnapshot() const {
  if (path_.empty()) {
    LOG(ERROR) << "There is nowhere to write the profile.";
    return;
  }
  const absl::Time end_time = absl::Now();
  const std::string snapshot = absl::StrCat(
      "JPRSurf profile\n", "Date:     ",
      absl::FormatTime("%Y-%m-%d %H:%M", start_time_, GetLocalTimeZone()), ", ",
      absl::ToInt64Seconds(end_time - start_time_), "s\n\n",
      profiler_.GetReport());

  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path_.c_str(), L"w") != 0 || file == nullptr) {
    LOG(ERROR) << "Failed to open " << path_.string()
               << " to write the profile.";
    return;
  }
  const bool written = (std::fwrite(snapshot.data(), 1, snapshot.size(),
                                    file) == snapshot.size());
  if (std::fclose(file) != 0 || !written) {
    LOG(ERROR) << "Failed to write the profile to " << path_.string();
    return;
  }
  LOG(INFO) << "Wrote the profile to " << path_.string();
}

}  // namespace jpr
