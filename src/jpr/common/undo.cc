// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/undo.h"

#include "absl/time/time.h"
#include "jpr/common/reaper_api.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

// Returns true if `project` is open in one of REAPER's project tabs.
bool IsProjectOpen(ReaProject* project) {
  for (int i = 0;; ++i) {
    ReaProject* const open_project = EnumProjects(i, nullptr, 0);
    if (open_project == nullptr) {
      return false;
    }
    if (open_project == project) {
      return true;
    }
  }
}

}  // namespace

ContinuousUndo* ContinuousUndo::s_instance_ = nullptr;

ContinuousUndo& ContinuousUndo::Get() {
  if (s_instance_ == nullptr) {
    s_instance_ = new ContinuousUndo();
  }
  return *s_instance_;
}

const TestReset ContinuousUndo::s_test_reset_([] {
  delete s_instance_;
  s_instance_ = nullptr;
});

void ContinuousUndo::OnChange(std::string_view description,
                              int undo_state_flags) {
  ReaProject* const project = EnumProjects(-1, nullptr, 0);

  // Pending changes that stopped kDelay ago were already flushed by Update(),
  // which is the only thing that moves the time forward.
  if (project_ != nullptr &&
      (project != project_ || description != description_ ||
       undo_state_flags != undo_state_flags_)) {
    Flush();
  }
  if (project_ == nullptr) {
    project_ = project;
    description_ = description;
    undo_state_flags_ = undo_state_flags;
  }
  last_change_time_secs_ = time_secs_;
}

void ContinuousUndo::Update(const RunTime& time) {
  time_secs_ = time.precise_secs;
  if (project_ != nullptr &&
      absl::Seconds(time_secs_ - last_change_time_secs_) >= kDelay) {
    Flush();
  }
}

void ContinuousUndo::Flush() {
  ReaProject* const project = project_;
  if (project == nullptr) {
    return;
  }
  project_ = nullptr;

  // A closed project's changes went with it.
  if (IsProjectOpen(project)) {
    Undo_OnStateChangeEx2(project, description_.c_str(), undo_state_flags_, -1);
  }
}

}  // namespace jpr
