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

TEST(CallCostTest, TrackSettersAreBatchedRefreshesOfTheTrack) {
  const CallCost cost = GetCallCost("SetTrackUIMute");
  EXPECT_EQ(cost.kind, Kind::kRefresh);
  EXPECT_TRUE(cost.batched);
  EXPECT_EQ(cost.repeat_key_args, 1);
}

}  // namespace
}  // namespace jpr
