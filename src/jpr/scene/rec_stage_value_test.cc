// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/rec_stage_value.h"

#include <string_view>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/deferred_refresh.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/call_counter.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::UnorderedElementsAre;

constexpr int kNone = static_cast<int>(TrackRecMode::kNone);
constexpr int kMidiOverdub = static_cast<int>(TrackRecMode::kMidiOverdub);

constexpr RecStageValue kRecStage;

// The project is T1 to T4, all refreshed as it is built, and the counter fails
// a set that sets a track twice.
class RecStageValueTest : public ::testing::Test {
 protected:
  RecStageValueTest() : tracks_(project_.AddTracks(4)) {
    TrackCache::Get().Refresh();
    for (FakeTrack* track : tracks_) {
      GetCachedTrack(track)->Refresh();
    }
  }

  // Sets the track's stage as one entry point, in a deferred refresh, as a
  // run would.
  void Set(Track* track, RecStageValue::Stage stage,
           TrackGrouping grouping = TrackGrouping::kNone) {
    {
      DeferredRefresh deferred_refresh;
      TrackBatch batch;
      kRecStage.Set(batch, track, stage, grouping);
    }
    reaper_.EndEntryPoint();
  }
  void Set(FakeTrack* track, RecStageValue::Stage stage,
           TrackGrouping grouping = TrackGrouping::kNone) {
    Set(GetCachedTrack(track), stage, grouping);
  }

  // Refreshes the track's cached track, record mode included, as the surface
  // does once REAPER reports a change to the mode.
  static void Refresh(FakeTrack* track) {
    Track* cached = GetCachedTrack(track);
    cached->OnRecModeChanged();
    cached->Refresh();
  }

  // Returns the track's stage, as read from its refreshed cached track.
  static int Get(FakeTrack* track) {
    Refresh(track);
    return kRecStage.Get(*GetCachedTrack(track));
  }

  // Returns every track's `field`, in order, such as its record arm.
  template <typename T>
  std::vector<T> GetEach(T FakeTrack::*field) const {
    std::vector<T> values;
    for (FakeTrack* track : tracks_) {
      values.push_back(track->*field);
    }
    return values;
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> tracks_;
  CallCounter counter_{&reaper_};
};

TEST_F(RecStageValueTest, NamesTheStages) {
  EXPECT_THAT(kRecStage.GetNames(), ElementsAre("off", "monitor", "record"));
  EXPECT_EQ(kRecStage.GetMaxValue(), RecStageValue::kRecord);
}

TEST_F(RecStageValueTest, ReadsTheArmAndTheMode) {
  // A disarmed track is off whatever its mode, and an armed one records in
  // any mode but none, including one TrackRecMode doesn't name.
  tracks_[0]->rec_mode = kNone;
  tracks_[1]->rec_arm = true;
  tracks_[1]->rec_mode = kNone;
  tracks_[2]->rec_arm = true;
  tracks_[2]->rec_mode = kMidiOverdub;
  tracks_[3]->rec_arm = true;
  tracks_[3]->rec_mode = 13;
  EXPECT_EQ(Get(tracks_[0]), RecStageValue::kOff);
  EXPECT_EQ(Get(tracks_[1]), RecStageValue::kMonitor);
  EXPECT_EQ(Get(tracks_[2]), RecStageValue::kRecord);
  EXPECT_EQ(Get(tracks_[3]), RecStageValue::kRecord);
}

TEST_F(RecStageValueTest, OffDisarmsAndLeavesTheMode) {
  tracks_[0]->rec_arm = true;
  tracks_[0]->rec_mode = kNone;
  tracks_[1]->rec_arm = true;
  tracks_[1]->rec_mode = kMidiOverdub;
  Set(tracks_[0], RecStageValue::kOff);
  Set(tracks_[1], RecStageValue::kOff);
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(false, false, false, false));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode),
              ElementsAre(kNone, kMidiOverdub, 0, 0));
}

TEST_F(RecStageValueTest, MonitorArmsWithNoRecordMode) {
  tracks_[0]->rec_mode = kMidiOverdub;
  tracks_[1]->rec_arm = true;
  Set(tracks_[0], RecStageValue::kMonitor);
  Set(tracks_[1], RecStageValue::kMonitor);
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(true, true, false, false));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode), ElementsAre(kNone, kNone, 0, 0));
  EXPECT_EQ(Get(tracks_[0]), RecStageValue::kMonitor);
  EXPECT_EQ(GetCachedTrack(tracks_[0])->GetReplacedRecMode(),
            TrackRecMode::kMidiOverdub);
}

TEST_F(RecStageValueTest, RecordRestoresTheReplacedModeOrInput) {
  // T1's mode was replaced by monitoring, and T2's was set to none in REAPER,
  // so it has none to restore. T3's mode isn't none, so it is kept.
  tracks_[0]->rec_mode = kMidiOverdub;
  Set(tracks_[0], RecStageValue::kMonitor);
  tracks_[1]->rec_mode = kNone;
  tracks_[2]->rec_mode = kMidiOverdub;
  for (FakeTrack* track : {tracks_[0], tracks_[1], tracks_[2]}) {
    Set(track, RecStageValue::kRecord);
  }
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(true, true, true, false));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode),
              ElementsAre(kMidiOverdub, 0, kMidiOverdub, 0));
  EXPECT_EQ(Get(tracks_[0]), RecStageValue::kRecord);
}

TEST_F(RecStageValueTest, RecordReadsREAPERsMode) {
  // The track's cached mode is stale, as it may be for a track the surface
  // doesn't show, so only REAPER's says it is none.
  tracks_[0]->rec_arm = true;
  tracks_[0]->rec_mode = kNone;
  Set(tracks_[0], RecStageValue::kRecord);
  EXPECT_EQ(tracks_[0]->rec_mode, 0);
}

TEST_F(RecStageValueTest, GroupingSetsEachTrackOfTheRecArmGroup) {
  // T1 and T2 share a group, and T1 and T4 are selected, so a grouped rec arm
  // of T1 reaches every track but T3. Each is set alone, and once, mode and
  // arm.
  JoinGroup(tracks_[0], 1);
  JoinGroup(tracks_[1], 1);
  tracks_[0]->selected = true;
  tracks_[3]->selected = true;
  tracks_[3]->rec_mode = kMidiOverdub;
  GetCachedTrack(tracks_[0])->Refresh();
  EXPECT_THAT(kRecStage.GetGroup(GetCachedTrack(tracks_[0])),
              UnorderedElementsAre(GetCachedTrack(tracks_[0]),
                                   GetCachedTrack(tracks_[1]),
                                   GetCachedTrack(tracks_[3])));

  Set(tracks_[0], RecStageValue::kMonitor, TrackGrouping::kGrouped);
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(true, true, false, true));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode),
              ElementsAre(kNone, kNone, 0, kNone));

  Set(tracks_[0], RecStageValue::kRecord, TrackGrouping::kGrouped);
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(true, true, false, true));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode),
              ElementsAre(0, 0, 0, kMidiOverdub));
}

TEST_F(RecStageValueTest, GroupingLeavesTheGroupIfTheTrackHasTheStage) {
  JoinGroup(tracks_[0], 1);
  JoinGroup(tracks_[1], 1);
  tracks_[0]->rec_arm = true;
  tracks_[0]->rec_mode = kNone;
  Refresh(tracks_[0]);
  Set(tracks_[0], RecStageValue::kMonitor, TrackGrouping::kGrouped);
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(true, false, false, false));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode), ElementsAre(kNone, 0, 0, 0));
}

TEST_F(RecStageValueTest, TheStubTrackIsNeverSet) {
  Set(TrackCache::Get().GetStubTrack(), RecStageValue::kMonitor,
      TrackGrouping::kGrouped);
  EXPECT_THAT(GetEach(&FakeTrack::rec_arm),
              ElementsAre(false, false, false, false));
  EXPECT_THAT(GetEach(&FakeTrack::rec_mode), ElementsAre(0, 0, 0, 0));
}

}  // namespace
}  // namespace jpr
