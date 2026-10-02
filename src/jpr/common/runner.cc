// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/runner.h"

#include <string_view>

#include "absl/strings/str_cat.h"
#include "gb/profile/profile_point.h"
#include "gb/profile/profile_timer.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

RunTime RunTime::Now() {
  const double precise = time_precise();
  return {.precise = precise,
          .coarse = static_cast<unsigned>(precise * 1000.0)};
}

RunHandle::RunHandle(RunHandle&& other)
    : registry_(std::exchange(other.registry_, nullptr)), id_(other.id_) {}

RunHandle& RunHandle::operator=(RunHandle&& other) {
  if (this != &other) {
    if (registry_ != nullptr) {
      registry_->cleared_runnables_.insert(id_);
    }
    registry_ = std::exchange(other.registry_, nullptr);
    id_ = other.id_;
  }
  return *this;
}

RunHandle::~RunHandle() {
  if (registry_ != nullptr) {
    registry_->cleared_runnables_.insert(id_);
  }
}

RunRegistry::RunRegistry(std::string_view name)
    : scope_(gb::ProfilePoint::Kind::kScope, absl::StrCat("Runner: ", name)),
      counter_(gb::ProfilePoint::Kind::kCounter,
               absl::StrCat(name, " runnables")) {}

RunHandle RunRegistry::AddRunnable(Runnable runnable) {
  int id = next_id_++;
  runnables_[id] = std::move(runnable);
  return RunHandle(this, id);
}

void RunRegistry::DoRun(const RunTime& time) {
  for (int id : cleared_runnables_) {
    runnables_.erase(id);
  }
  cleared_runnables_.clear();
  gb::ProfileTimer timer(scope_);
  int run_count = 0;
  for (auto& [id, runnable] : runnables_) {
    if (!cleared_runnables_.contains(id)) {
      runnable(time);
      ++run_count;
    }
  }
  counter_.Count(run_count);
}

}  // namespace jpr