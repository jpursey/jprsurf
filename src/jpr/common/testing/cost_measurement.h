// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/any_invocable.h"
#include "jpr/common/testing/fake_project.h"

namespace jpr {

//==============================================================================
// CostMeasurement
//
// One thing whose cost in REAPER is measured, under one condition, such as
// SetTrackUIMute() inside a deferred refresh on 128 tracks. MeasuringSurface
// measures it (see docs/worklog/measure_reaper_costs.md).
//
// It is a read or a change, so exactly one of `read` and `call` is set. It acts
// only through REAPER's API, so it runs under the fake REAPER too.
//==============================================================================

struct CostMeasurement {
  // What is measured, such as the function's name, which the filter matches
  // (see FilterCostMeasurements()).
  std::string name;

  // The project it is measured on, such as "128 tracks", or empty if there is
  // nothing to say.
  std::string condition;

  // True to make the change inside a deferred refresh (a PreventUIRefresh()
  // scope), whose end is timed apart from the call.
  bool in_deferred_refresh = false;

  // If set, fills in the project the measurement starts from, which is
  // otherwise empty.
  absl::AnyInvocable<void(FakeProject&)> build;

  // If set, called once the project is open, before anything is measured, to
  // find what the measurement acts on (such as its tracks), or to start from
  // a state a project can't hold (such as playing).
  absl::AnyInvocable<void()> prepare;

  // A read: makes calls that change nothing, so they leave REAPER no later
  // work, and returns how many it made, at least 1, which its time is divided
  // by. Calls on different tracks show what a read costs better than the
  // same call over and over, which stays in the cache.
  absl::AnyInvocable<int()> read;

  // A change: makes one call, which REAPER may do more work for in the runs
  // after it (later work). Each call must change something, so a setter
  // alternates its value.
  absl::AnyInvocable<void()> call;

  // If set, called after the runs that measure a change, so the next call
  // changes something too: undoing a change that can't be made again until it
  // is undone (Transport: Play), or making what the next call acts on (the
  // change an undo point holds). Its own later work isn't measured.
  absl::AnyInvocable<void()> reset;
};

// Returns the measurements `filter` names, in their order: each of its names,
// separated by ':', matches a measurement's name exactly. An empty filter
// keeps every measurement.
std::vector<CostMeasurement> FilterCostMeasurements(
    std::vector<CostMeasurement> measurements, std::string_view filter);

}  // namespace jpr
