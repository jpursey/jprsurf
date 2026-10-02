// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "jpr/common/reset_test_key.h"
#include "jpr/common/runner.h"

namespace jpr {

// Creates REAPER undo points for continuous changes, such as moving a fader or
// turning a knob, which REAPER does not create undo points for itself.
//
// A series of changes with the same description (for instance, moving one or
// more send faders) becomes a single undo point, created once the changes have
// stopped for kDelay. A change with a different description first creates the
// undo point for the pending changes, as REAPER does for track volume and pan
// changes from a control surface.
//
// As the undo point is created after the changes, it also includes any other
// change made in the meantime that doesn't create its own undo point. Code that
// is about to create its own undo point should call Flush() first, so the
// pending changes aren't included in it.
class ContinuousUndo final {
 public:
  // How long changes must stop before their undo point is created.
  static constexpr absl::Duration kDelay = absl::Milliseconds(500);

  // Returns the singleton instance.
  static ContinuousUndo& Get();

  // For tests only (see ResetCommonState()): destroys the instance, dropping
  // any pending changes without creating their undo point, so the next Get()
  // creates a new one.
  static void Reset(ResetTestKey);

  ContinuousUndo(const ContinuousUndo&) = delete;
  ContinuousUndo& operator=(const ContinuousUndo&) = delete;
  ~ContinuousUndo() = default;

  // Records a change to include in an undo point with the given description
  // and undo state flags (see Undo_OnStateChangeEx). The change's time is the
  // time last passed to Update().
  //
  // If a change is pending with a different description or flags, the pending
  // undo point is created first.
  void OnChange(std::string_view description, int undo_state_flags);

  // Creates the pending undo point, if the changes stopped at least kDelay
  // before `time`, and makes `time` the time of the changes that follow. This
  // must be called at the start of every run, before anything in the run
  // makes a change (see ControlSurface::Run()).
  void Update(const RunTime& time);

  // Creates the pending undo point immediately, if there is one.
  void Flush();

 private:
  // The instance, created by the first Get(). It is never destroyed, apart from
  // by Reset().
  static ContinuousUndo* s_instance_;

  ContinuousUndo() = default;

  // The current run's time, in seconds (see Update()).
  double time_ = 0.0;

  bool pending_ = false;
  std::string description_;
  int undo_state_flags_ = 0;
  double last_change_time_ = 0.0;
};

}  // namespace jpr
