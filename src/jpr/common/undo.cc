// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/undo.h"

#include "absl/time/clock.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

ContinuousUndo& ContinuousUndo::Get() {
  static absl::NoDestructor<ContinuousUndo> instance;
  return *instance;
}

void ContinuousUndo::OnChange(std::string_view description,
                              int undo_state_flags) {
  const absl::Time now = absl::Now();

  // Checking the time here as well as in Update() ensures pending changes are
  // never merged into a later series of changes, even if Update() is not
  // called in between.
  if (pending_ &&
      (description != description_ || undo_state_flags != undo_state_flags_ ||
       now - last_change_time_ >= kDelay)) {
    Flush();
  }
  if (!pending_) {
    pending_ = true;
    description_ = description;
    undo_state_flags_ = undo_state_flags;
  }
  last_change_time_ = now;
}

void ContinuousUndo::Update(absl::Time now) {
  if (pending_ && now - last_change_time_ >= kDelay) {
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
