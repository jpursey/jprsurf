// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/undo.h"

#include "absl/time/time.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

ContinuousUndo& ContinuousUndo::Get() {
  static absl::NoDestructor<ContinuousUndo> instance;
  return *instance;
}

void ContinuousUndo::OnChange(std::string_view description,
                              int undo_state_flags) {
  // Pending changes that stopped kDelay ago were already flushed by Update(),
  // which is the only thing that moves the time forward.
  if (pending_ &&
      (description != description_ || undo_state_flags != undo_state_flags_)) {
    Flush();
  }
  if (!pending_) {
    pending_ = true;
    description_ = description;
    undo_state_flags_ = undo_state_flags;
  }
  last_change_time_ = time_;
}

void ContinuousUndo::Update(const RunTime& time) {
  time_ = time.precise;
  if (pending_ && absl::Seconds(time_ - last_change_time_) >= kDelay) {
    Flush();
  }
}

void ContinuousUndo::Flush() {
  if (!pending_) {
    return;
  }
  pending_ = false;
  Undo_OnStateChangeEx(description_.c_str(), undo_state_flags_, -1);
}

}  // namespace jpr
