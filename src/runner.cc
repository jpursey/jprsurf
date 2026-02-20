// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "runner.h"

#include "reaper_plugin_functions.h"

namespace jpr {

RunHandle::RunHandle(RunHandle&& other)
    : registry_(std::exchange(other.registry_, nullptr)), id_(other.id_) {}

RunHandle& RunHandle::operator=(RunHandle&& other) {
  if (this != &other) {
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

RunHandle RunRegistry::AddRunnable(Runnable runnable) {
  int id = next_id_++;
  runnables_[id] = std::move(runnable);
  return RunHandle(this, id);
}

void RunRegistry::DoRun() {
  RunTime time;
  time.precise = time_precise();
  time.coarse = static_cast<unsigned>(time.precise * 1000.0);
  for (int id : cleared_runnables_) {
    runnables_.erase(id);
  }
  for (auto& [id, runnable] : runnables_) {
    if (!cleared_runnables_.contains(id)) {
      runnable(time);
    }
  }
}

}  // namespace jpr