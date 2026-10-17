// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/call_cost.h"

#include <iterator>
#include <string_view>

#include "absl/log/check.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

namespace {

using Kind = CallCost::Kind;

constexpr CallCost kCheap = {.kind = Kind::kCheap};
constexpr CallCost kModerate = {.kind = Kind::kModerate};

constexpr CallCost Refresh(int repeat_key_args) {
  return {.kind = Kind::kRefresh, .repeat_key_args = repeat_key_args};
}

// A refresh a deferred refresh puts off: every setter the extension calls. A
// track's mute, solo, rec arm, selection, volume, pan, and the automation
// override also call back to the surface at the end of the deferred refresh
// (see "Seen in traces" in docs/testing_and_profiling.md). A route's setters
// call back during the call, but their refresh still waits for the end.
constexpr CallCost DeferrableRefresh(int repeat_key_args) {
  return {.kind = Kind::kRefresh,
          .deferrable = true,
          .repeat_key_args = repeat_key_args};
}

struct NamedCost {
  std::string_view function;
  CallCost cost;
};

// Every function on the API list, in its order. A cost that grows with the
// project says how.
constexpr NamedCost kCosts[] = {
    // 86ns on 128 tracks, and 315ns on 508.
    {"AnyTrackSolo", kCheap},
    // A scan of the project's tracks and items: 98ns on 128 tracks with one
    // item, 399ns on 508, and about 0.5us on a real project of 81 tracks.
    {"CountSelectedMediaItems", kModerate},
    {"CountSelectedTracks", kCheap},
    {"CountSelectedTracks2", kCheap},
    {"CountTracks", kCheap},
    {"CreateMIDIInput", kModerate},
    {"CreateMIDIOutput", kModerate},
    {"CSurf_OnPanChangeEx", DeferrableRefresh(1)},
    {"CSurf_OnVolumeChangeEx", DeferrableRefresh(1)},
    {"EnumProjects", kCheap},
    {"format_timestr_pos", kCheap},
    {"GetCursorPosition", kCheap},
    {"GetGlobalAutomationOverride", kCheap},
    {"GetMasterTrack", kCheap},
    {"GetMediaTrackInfo_Value", kCheap},
    {"GetMIDIInputName", kModerate},
    {"GetMIDIOutputName", kModerate},
    {"GetNumMIDIInputs", kModerate},
    {"GetNumMIDIOutputs", kModerate},
    {"GetParentTrack", kCheap},
    {"GetPlayPosition", kCheap},
    {"GetPlayState", kCheap},
    {"GetSelectedTrack", kCheap},
    {"GetSelectedTrack2", kCheap},
    // The extension only sets the name, which takes 4us, and refreshes nothing.
    {"GetSetMediaTrackInfo_String", kModerate},
    {"GetSetTrackSendInfo", kCheap},
    {"GetToggleCommandState", kCheap},
    {"GetTrack", kCheap},
    {"GetTrackColor", kCheap},
    {"GetTrackGUID", kCheap},
    {"GetTrackNumSends", kCheap},
    {"GetTrackReceiveUIMute", kCheap},
    {"GetTrackReceiveUIVolPan", kCheap},
    {"GetTrackSendUIMute", kCheap},
    {"GetTrackSendUIVolPan", kCheap},
    {"GetTrackState", kCheap},
    {"GetTrackUIVolPan", kCheap},
    {"guidToString", kCheap},
    {"IsProjectDirty", kCheap},
    {"kbd_getTextFromCmd", kCheap},
    {"Main_OnCommand", {.kind = Kind::kCommand, .repeat_key_args = 1}},
    {"mkpanstr", kCheap},
    {"mkvolstr", kCheap},
    {"NamedCommandLookup", kCheap},
    {"PreventUIRefresh", kCheap},
    {"SetGlobalAutomationOverride", DeferrableRefresh(0)},
    {"SetOnlyTrackSelected", DeferrableRefresh(0)},
    {"SetTrackSelected", DeferrableRefresh(1)},
    {"SetTrackSendUIPan", DeferrableRefresh(2)},
    {"SetTrackSendUIVol", DeferrableRefresh(2)},
    {"SetTrackUIMute", DeferrableRefresh(1)},
    // Its refresh at the end of a deferred refresh: 4.5ms on 28 tracks, 4.9ms
    // on 128, and 6.6ms on 508.
    {"SetTrackUIRecArm", DeferrableRefresh(1)},
    {"SetTrackUISolo", DeferrableRefresh(1)},
    {"ShowConsoleMsg", Refresh(0)},
    {"stringToGuid", kModerate},
    {"time_precise", kCheap},
    // With the undo point REAPER adds for it.
    {"ToggleTrackSendUIMute", DeferrableRefresh(2)},
    {"Track_GetPeakInfo", kCheap},
    {"Undo_CanRedo2", kCheap},
    {"Undo_OnStateChangeEx", {.kind = Kind::kUndoPoint}},
    {"Undo_OnStateChangeEx2", {.kind = Kind::kUndoPoint}},
};

// Returns true if kCosts has exactly the functions on the API list, in order.
constexpr bool MatchesTheList() {
  const int count = static_cast<int>(std::size(kCosts));
  if (count != static_cast<int>(std::size(kReaperApiNames))) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (kCosts[i].function != kReaperApiNames[i]) {
      return false;
    }
  }
  return true;
}
static_assert(MatchesTheList(),
              "kCosts must have every function on the API list, in its order, "
              "and no others");

}  // namespace

CallCost GetCallCost(std::string_view function) {
  const int count = static_cast<int>(std::size(kCosts));
  int index = 0;
  while (index < count && kCosts[index].function != function) {
    ++index;
  }
  CHECK(index < count) << function << " isn't on the REAPER API list";
  return kCosts[index].cost;
}

}  // namespace jpr
