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

// A refresh a deferred refresh puts off: mute, solo, rec arm, selection,
// volume, pan, and the automation override, whose calls back to the surface
// come at the end of the deferred refresh (see "Seen in traces" in
// docs/testing_and_profiling.md).
constexpr CallCost DeferrableRefresh(int repeat_key_args) {
  return {.kind = Kind::kRefresh,
          .deferrable = true,
          .repeat_key_args = repeat_key_args};
}

struct NamedCost {
  std::string_view function;
  CallCost cost;
};

// Every function on the API list, in its order.
constexpr NamedCost kCosts[] = {
    {"AnyTrackSolo", kCheap},
    {"CountSelectedMediaItems", kModerate},
    {"CountSelectedTracks", kCheap},
    {"CountSelectedTracks2", kCheap},
    {"CountTracks", kCheap},
    {"CreateMIDIInput", kModerate},
    {"CreateMIDIOutput", kModerate},
    {"CSurf_OnPanChangeEx", DeferrableRefresh(1)},
    {"CSurf_OnVolumeChangeEx", DeferrableRefresh(1)},
    {"EnumProjects", kCheap},
    {"format_timestr_pos", kModerate},
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
    {"GetSetMediaTrackInfo_String", kCheap},
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
    {"guidToString", kModerate},
    {"IsProjectDirty", kCheap},
    {"kbd_getTextFromCmd", kModerate},
    {"Main_OnCommand", {.kind = Kind::kCommand, .repeat_key_args = 1}},
    {"mkpanstr", kModerate},
    {"mkvolstr", kModerate},
    {"NamedCommandLookup", kModerate},
    {"PreventUIRefresh", kCheap},
    {"SetGlobalAutomationOverride", DeferrableRefresh(0)},
    {"SetOnlyTrackSelected", DeferrableRefresh(0)},
    {"SetTrackSelected", DeferrableRefresh(1)},
    {"SetTrackSendUIPan", Refresh(2)},
    {"SetTrackSendUIVol", Refresh(2)},
    {"SetTrackUIMute", DeferrableRefresh(1)},
    {"SetTrackUIRecArm", DeferrableRefresh(1)},
    {"SetTrackUISolo", DeferrableRefresh(1)},
    {"ShowConsoleMsg", Refresh(0)},
    {"stringToGuid", kModerate},
    {"time_precise", kCheap},
    {"ToggleTrackSendUIMute", Refresh(2)},
    {"Track_GetPeakInfo", kCheap},
    {"Undo_CanRedo2", kCheap},
    {"Undo_OnStateChangeEx", {.kind = Kind::kUndoPoint}},
    {"Undo_OnStateChangeEx2", {.kind = Kind::kUndoPoint}},
};

constexpr std::string_view kListed[] = {
#define JPR_LISTED_NAME(name) #name,
    JPR_REAPER_API(JPR_LISTED_NAME)
#undef JPR_LISTED_NAME
};

// Returns true if kCosts has exactly the functions on the API list, in order.
constexpr bool MatchesTheList() {
  const int count = static_cast<int>(std::size(kCosts));
  if (count != static_cast<int>(std::size(kListed))) {
    return false;
  }
  for (int i = 0; i < count; ++i) {
    if (kCosts[i].function != kListed[i]) {
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
