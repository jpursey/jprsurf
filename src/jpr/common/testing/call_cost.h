// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

namespace jpr {

//==============================================================================
// CallCost
//
// What a call to a function on the REAPER API list (see reaper_api.h) costs,
// in coarse categories, for bounding the calls the extension makes.
//
// The categories come from what each function costs in REAPER on its own, with
// the arguments the extension calls it with, as measure_in_reaper measures it
// (see docs/worklog/measure_reaper_costs.md): the time in the call, and
// REAPER's work on its UI thread after it. A call whose cost grows with the
// project is weighed at the size of the large project the tests use (128
// tracks), and says how it grows. A function added to the list is measured
// (see the measure-reaper-costs skill in .claude/skills/).
//==============================================================================

struct CallCost {
  enum class Kind {
    // A read, under about 0.4us: most are 5-100ns, and text about 70-260ns.
    kCheap,

    // Work that refreshes nothing, but is more than a read, up to about
    // 0.3ms: parsing a GUID (1.4us), renaming a track (4us), reads that scan
    // the project (counting selected items), functions only called as the
    // extension starts (opening MIDI ports), and a deferrable refresh's own
    // work inside a deferred refresh (2-300us).
    kModerate,

    // A change REAPER's UI shows, which refreshes it: about 0.2ms (a route's
    // volume) to 5ms (a rec arm) at the end of a deferred refresh, and as much
    // again in a redraw REAPER puts off to its own loop after the run (a
    // selection's 2.7ms), which no call's time shows.
    kRefresh,

    // An undo point the extension adds: about 1.4ms, most of it at the end of
    // the deferred refresh it is added in. It grows with the project: 1.2ms on
    // 28 tracks, and 2.4ms on 508.
    kUndoPoint,

    // Main_OnCommand(), whose cost is the action's: from 5us (unselecting
    // items) to 20ms (saving a new version of the project). Most of the
    // extension's take 1-3ms, and the automation modes as much again after.
    kCommand,
  };

  Kind kind = Kind::kCheap;

  // For a refresh: true if a deferred refresh (a PreventUIRefresh() scope)
  // puts the refresh off to the end of the outermost one, and the call itself
  // is then moderate. The call that ends the outermost deferred refresh is the
  // refresh, whatever its own cost, which a caller finds from the deferred
  // refresh rather than from this table. Deferrable setters share that
  // refresh: muting 128 tracks in one ends it in 1.7ms, against 0.8ms for one
  // track.
  bool deferrable = false;

  // For a refresh or a command: how many of its leading arguments, with the
  // function, identify a call, so a second call in a run with the same ones is
  // found as a repeat. They name what the call acts on, such as a track, or a
  // track and a route's index, and leave out the value it sets. Zero for a
  // call that acts on the same thing every time.
  int repeat_key_args = 0;
};

// Returns the cost of a call to `function`, which must be on the API list.
CallCost GetCallCost(std::string_view function);

}  // namespace jpr
