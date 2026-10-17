// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/call_cost.h"

#include "gtest/gtest.h"

namespace jpr {
namespace {

using Kind = CallCost::Kind;

// That the table has every function on the list, and no others, is checked
// when it compiles.

TEST(CallCostTest, FindsTheFunctionsCost) {
  EXPECT_EQ(GetCallCost("GetTrackState").kind, Kind::kCheap);
  EXPECT_EQ(GetCallCost("Undo_OnStateChangeEx").kind, Kind::kUndoPoint);
}

TEST(CallCostTest, TrackSettersAreDeferrableRefreshesOfTheTrack) {
  const CallCost cost = GetCallCost("SetTrackUIMute");
  EXPECT_EQ(cost.kind, Kind::kRefresh);
  EXPECT_TRUE(cost.deferrable);
  EXPECT_EQ(cost.repeat_key_args, 1);
}

TEST(CallCostTest, FadersPansAndTheOverrideAreDeferrable) {
  EXPECT_TRUE(GetCallCost("CSurf_OnVolumeChangeEx").deferrable);
  EXPECT_TRUE(GetCallCost("CSurf_OnPanChangeEx").deferrable);
  EXPECT_TRUE(GetCallCost("SetGlobalAutomationOverride").deferrable);
}

TEST(CallCostTest, RouteSettersAreDeferrableRefreshesOfTheRoute) {
  for (const char* function :
       {"SetTrackSendUIVol", "SetTrackSendUIPan", "ToggleTrackSendUIMute"}) {
    const CallCost cost = GetCallCost(function);
    EXPECT_EQ(cost.kind, Kind::kRefresh) << function;
    EXPECT_TRUE(cost.deferrable) << function;
    EXPECT_EQ(cost.repeat_key_args, 2) << function;
  }
}

TEST(CallCostTest, TextIsCheapButRenamingATrackIsModerate) {
  EXPECT_EQ(GetCallCost("mkvolstr").kind, Kind::kCheap);
  EXPECT_EQ(GetCallCost("format_timestr_pos").kind, Kind::kCheap);
  EXPECT_EQ(GetCallCost("GetSetMediaTrackInfo_String").kind, Kind::kModerate);
}

}  // namespace
}  // namespace jpr
