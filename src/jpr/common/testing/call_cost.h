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
// The categories come from the per-call self times in the profile snapshots in
// profiles/, of an 81-track project. A call whose cost grows with the project
// is weighed at the size of the large project the tests use. What a function
// costs on its own isn't measured yet, so the categories are conservative
// where the snapshots can't tell (see kRefresh).
//==============================================================================

struct CallCost {
  enum class Kind {
    // A read, under about 0.4us.
    kCheap,

    // Work that refreshes nothing, but is more than a read: text (about
    // 0.5-3us), reads that scan the project (counting selected items),
    // functions only called as the extension starts (opening MIDI ports), and
    // a batched refresh's own work inside a scope (about 20-200us).
    kModerate,

    // A change REAPER's UI shows, which refreshes it: about 2-17ms, much of
    // it in a redraw REAPER puts off to its own loop, which no call's time
    // shows. Every function that changes what REAPER's UI shows is one.
    kRefresh,

    // An undo point the extension adds: about 1.5ms.
    kUndoPoint,

    // Main_OnCommand(), whose cost is the action's.
    kCommand,
  };

  Kind kind = Kind::kCheap;

  // For a refresh: true if a PreventUIRefresh() scope puts the refresh off to
  // the end of the outermost scope, and the call itself is then moderate. The
  // call that ends the outermost scope is the refresh, whatever its own cost,
  // which a caller finds from the scope rather than from this table.
  bool batched = false;

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
