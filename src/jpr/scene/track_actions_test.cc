// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_actions.h"

#include <string>
#include <string_view>
#include <vector>

#include "absl/functional/function_ref.h"
#include "gb/test/log_error_guard.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/anchor.h"
#include "jpr/common/deferred_refresh.h"
#include "jpr/common/modifiers.h"
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
using ::testing::IsEmpty;

constexpr Modifiers kCtrlShift = kModCtrl | kModShift;

constexpr std::string_view kTestValueNames[] = {"input", "output_stereo",
                                                "none"};

// A value of the first three record modes, by their numbers, which are the only
// modes the tests give tracks.
class TestValue final : public TrackValue {
 public:
  constexpr TestValue() : TrackValue(kTestValueNames) {}

  int Get(const Track& track) const override {
    return static_cast<int>(track.GetRecMode());
  }
  void Set(TrackBatch& batch, Track* track, int value,
           TrackGrouping grouping) const override {
    batch.SetRecMode(track, static_cast<TrackRecMode>(value), grouping);
  }
  std::vector<Track*> GetGroup(Track* track) const override {
    return TrackCache::Get().GetRecArmGroup(track);
  }
};

constexpr TestValue kTestValue;

// The project is T1, T2 (a folder of T2.1 and T2.2), T3, and T4, all on the
// surface. Each action is run as one call REAPER makes into the surface, so
// the fake checks it on its own (such as that a change to several tracks is one
// batch).
//
// The cache's tracks are refreshed when the project is built. After that, only
// the track an action is on is refreshed before it, as the view that shows it
// does each run, so every other track's cached values may be stale, as they
// may be on the surface.
class TrackActionsTest : public ::testing::Test {
 protected:
  TrackActionsTest()
      : top_(project_.AddTracks(4)), folder_(project_.AddTracks(2, top_[1])) {
    TrackCache::Get().Refresh();
  }

  // Each runs the action on `track`, with `modifiers` held (see Act()).
  void Select(FakeTrack* track, Modifiers modifiers = 0) {
    Act(track, modifiers, [this](Track* cached) { actions_.UiSelect(cached); });
  }
  void Toggle(FakeTrack* track, TrackBoolProperty property,
              Modifiers modifiers = 0) {
    Act(track, modifiers, [this, property](Track* cached) {
      actions_.UiToggle(cached, property);
    });
  }
  void Mute(FakeTrack* track, Modifiers modifiers = 0) {
    Toggle(track, TrackBoolProperty::kMute, modifiers);
  }
  void PressValue(FakeTrack* track, const EnumeratedPress& press,
                  Modifiers modifiers = 0,
                  const Anchor<Track>* anchor = nullptr) {
    Act(track, modifiers, [this, &press, anchor](Track* cached) {
      actions_.UiPress(cached, kTestValue, press, anchor);
    });
  }

  // Runs `action` on `track`'s cached track, with `modifiers` held, as a run
  // would: in a deferred refresh, and then the end of the entry point.
  void Act(FakeTrack* track, Modifiers modifiers,
           absl::FunctionRef<void(Track*)> action) {
    Track* cached = GetCachedTrack(track);
    cached->Refresh();
    SetModifiers(modifiers, true);
    {
      DeferredRefresh deferred_refresh;
      action(cached);
    }
    SetModifiers(modifiers, false);
    reaper_.EndEntryPoint();
  }

  // Hides `track` in the mixer, so it isn't on the surface.
  void Hide(FakeTrack* track) {
    track->show_in_mixer = false;
    TrackCache::Get().RefreshFilters();
  }

  // Returns the names of the tracks with `value` on, in order.
  std::vector<std::string> GetTracksWith(bool FakeTrack::*value) {
    std::vector<std::string> names;
    for (int i = 0; i < project_.GetTrackCount(); ++i) {
      if (project_.GetTrack(i)->*value) {
        names.push_back(project_.GetTrack(i)->name);
      }
    }
    return names;
  }

  // Returns every track's record mode, in order: T1, T2, T2.1, T2.2, T3, T4.
  std::vector<int> GetRecModes() {
    std::vector<int> modes;
    for (int i = 0; i < project_.GetTrackCount(); ++i) {
      modes.push_back(project_.GetTrack(i)->rec_mode);
    }
    return modes;
  }

  // Sets the track's record mode in REAPER, which reports it as REAPER does
  // (see Track::OnRecModeChanged()), so its cached mode is stale until it is
  // refreshed.
  static void SetRecMode(FakeTrack* track, int mode) {
    track->rec_mode = mode;
    GetCachedTrack(track)->OnRecModeChanged();
  }

  // The last touched track's name, or empty if there is none.
  static std::string GetLastTouched() {
    Track* track = TrackCache::Get().GetLastTouchedTrack();
    return track != nullptr ? std::string(track->GetName()) : "";
  }
  static void SetLastTouched(FakeTrack* track) {
    TrackCache::Get().SetLastTouchedTrack(GetCachedTrack(track));
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> top_;     // T1, T2, T3, T4.
  std::vector<FakeTrack*> folder_;  // T2.1, T2.2.
  TrackActions actions_{TrackFilter::kMcp};
};

//==============================================================================
// Volume and pan
//==============================================================================

TEST_F(TrackActionsTest, VolumeAndPanChangeTheGroup) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  actions_.UiSetVolume(GetCachedTrack(top_[0]), 0.5);
  actions_.UiSetPan(GetCachedTrack(top_[0]), 0.25);
  EXPECT_EQ(top_[0]->volume, 0.5);
  EXPECT_EQ(top_[0]->pan, 0.25);
  EXPECT_EQ(top_[2]->volume, 0.5);
  EXPECT_EQ(top_[2]->pan, 0.25);
  EXPECT_EQ(top_[1]->volume, 1.0);
}

TEST_F(TrackActionsTest, CtrlVolumeAndPanChangeOnlyTheTrack) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  SetModifiers(kModCtrl, true);
  actions_.UiSetVolume(GetCachedTrack(top_[0]), 0.5);
  actions_.UiSetPan(GetCachedTrack(top_[0]), 0.25);
  EXPECT_EQ(top_[0]->volume, 0.5);
  EXPECT_EQ(top_[0]->pan, 0.25);
  EXPECT_EQ(top_[2]->volume, 1.0);
  EXPECT_EQ(top_[2]->pan, 0.0);
}

//==============================================================================
// Select
//==============================================================================

TEST_F(TrackActionsTest, SelectSelectsOnlyTheTrack) {
  top_[2]->selected = true;
  Select(top_[0]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T1"));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, SelectingTheOnlySelectedTrackUnselectsIt) {
  Select(top_[0]);
  Select(top_[0]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), IsEmpty());

  // One of several selected tracks is selected alone.
  top_[0]->selected = true;
  top_[2]->selected = true;
  Select(top_[0]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T1"));
}

TEST_F(TrackActionsTest, CtrlSelectTogglesTheTrackAlone) {
  Select(top_[0]);
  Select(top_[2], kModCtrl);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T1", "T3"));
  EXPECT_EQ(GetLastTouched(), "T3");

  // Unselecting it leaves the last touched track as it was.
  Select(top_[0], kModCtrl);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T3"));
  EXPECT_EQ(GetLastTouched(), "T3");
}

TEST_F(TrackActionsTest, ShiftSelectSelectsTheRangeWithTheSameParent) {
  folder_[0]->selected = true;
  SetLastTouched(top_[0]);
  Select(top_[2], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected),
              ElementsAre("T1", "T2", "T3"));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, CtrlShiftSelectSelectsEveryTrackInTheRange) {
  SetLastTouched(top_[3]);
  Select(folder_[0], kCtrlShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected),
              ElementsAre("T2.1", "T2.2", "T3", "T4"));
  EXPECT_EQ(GetLastTouched(), "T4");
}

TEST_F(TrackActionsTest, RangesNeedBothEndsOnTheSurface) {
  Hide(top_[3]);
  Select(top_[0]);
  Select(top_[3], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T1"));

  SetLastTouched(top_[3]);
  Select(top_[2], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T1"));

  // Without a last touched track, there is no range.
  TrackCache::Get().SetLastTouchedTrack(nullptr);
  Select(top_[2], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T1"));
}

TEST_F(TrackActionsTest, RangesSelectOnlyTracksOnTheSurface) {
  top_[2]->selected = true;
  Hide(top_[2]);
  SetLastTouched(top_[0]);
  Select(top_[3], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected),
              ElementsAre("T1", "T2", "T4"));
}

//==============================================================================
// Mute, solo, and record arm
//==============================================================================

TEST_F(TrackActionsTest, ToggleSetsTheGroupToTheTracksNewValue) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  top_[2]->mute = true;
  Mute(top_[0]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T1", "T3"));
  EXPECT_EQ(GetLastTouched(), "T1");

  Mute(top_[0]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), IsEmpty());
}

TEST_F(TrackActionsTest, CtrlToggleTogglesTheTrackAlone) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  Mute(top_[0], kModCtrl);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T1"));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, SoloAndRecArmToggleAsMuteDoes) {
  Toggle(top_[0], TrackBoolProperty::kSolo);
  Toggle(top_[1], TrackBoolProperty::kRecArm);
  EXPECT_THAT(GetTracksWith(&FakeTrack::solo), ElementsAre("T1"));
  EXPECT_THAT(GetTracksWith(&FakeTrack::rec_arm), ElementsAre("T2"));
}

TEST_F(TrackActionsTest, AltTurnsEveryTrackOff) {
  top_[0]->mute = true;
  folder_[1]->mute = true;
  top_[3]->mute = true;
  Hide(top_[3]);
  SetLastTouched(top_[0]);
  Mute(top_[2], kModAlt);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), IsEmpty());
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, CtrlAltTurnsOnTheTrackAlone) {
  top_[0]->mute = true;
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);
  Mute(top_[2], kModCtrl | kModAlt);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T3"));
  EXPECT_EQ(GetLastTouched(), "T3");
}

TEST_F(TrackActionsTest, ShiftAltTurnsOnTheTrackAndItsGroup) {
  top_[0]->mute = true;
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);
  Mute(top_[2], kModShift | kModAlt);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T3", "T4"));
  EXPECT_EQ(GetLastTouched(), "T3");
}

TEST_F(TrackActionsTest, ShiftSetsTheRangeWithTheSameParentToTheLastTouched) {
  folder_[0]->mute = true;
  Mute(top_[0]);
  Mute(top_[3], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute),
              ElementsAre("T1", "T2", "T2.1", "T3", "T4"));
  EXPECT_EQ(GetLastTouched(), "T1");

  // The range takes the last touched track's value, whatever this track's is.
  // The last touched track's cached value is stale, so the range reads it.
  top_[0]->mute = false;
  Mute(top_[2], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T2.1", "T4"));
}

TEST_F(TrackActionsTest, CtrlShiftSetsEveryTrackInTheRangeAlone) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  Mute(top_[3]);
  Mute(folder_[1], kCtrlShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T2.2", "T3", "T4"));
  EXPECT_EQ(GetLastTouched(), "T4");
}

TEST_F(TrackActionsTest, RangesSetOnlyTracksOnTheSurface) {
  Hide(top_[2]);
  Mute(top_[0]);
  Mute(top_[3], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T1", "T2", "T4"));
}

TEST_F(TrackActionsTest, OptTogglesEachSelectedTrackAlone) {
  top_[0]->selected = true;
  top_[2]->selected = true;
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);

  // T3's cached value is stale, so the action reads it.
  top_[2]->mute = true;
  Mute(top_[0], kModOpt);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T1"));
  EXPECT_EQ(GetLastTouched(), "T1");

  // A track that isn't selected is toggled alone.
  JoinGroup(top_[1], 1);
  Mute(top_[1], kModOpt);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T1", "T2"));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, ActionsOnTheStubTrackDoNothing) {
  Track* stub = TrackCache::Get().GetStubTrack();
  actions_.UiSelect(stub);
  actions_.UiToggle(stub, TrackBoolProperty::kMute);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), IsEmpty());
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), IsEmpty());
}

//==============================================================================
// Enumerated values
//==============================================================================

constexpr EnumeratedPress kToggleNoneOfTwo = {
    .min = 0, .max = 2, .toggles = true};
constexpr EnumeratedPress kToggleOneOrTwo = {
    .min = 1, .max = 2, .toggles = true};

TEST_F(TrackActionsTest, PressSetsTheGroupToTheTracksNewValue) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  SetRecMode(top_[2], 1);
  PressValue(top_[0], kToggleNoneOfTwo);
  EXPECT_THAT(GetRecModes(), ElementsAre(2, 0, 0, 0, 2, 0));
  EXPECT_EQ(GetLastTouched(), "T1");

  PressValue(top_[0], kToggleNoneOfTwo);
  EXPECT_THAT(GetRecModes(), ElementsAre(0, 0, 0, 0, 0, 0));
}

TEST_F(TrackActionsTest, CtrlPressAppliesItToTheTrackAlone) {
  JoinGroup(top_[0], 1);
  JoinGroup(top_[2], 1);
  PressValue(top_[0], kToggleNoneOfTwo, kModCtrl);
  EXPECT_THAT(GetRecModes(), ElementsAre(2, 0, 0, 0, 0, 0));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, AltPressSetsEveryTrackPastTheMinToIt) {
  // Each of these tracks' cached modes is stale, so the action reads them.
  SetRecMode(top_[0], 2);
  SetRecMode(top_[1], 1);
  SetRecMode(folder_[1], 2);
  SetRecMode(top_[3], 2);
  Hide(top_[3]);
  SetLastTouched(top_[0]);

  // T3 is before the min, so it is left alone.
  PressValue(top_[2], kToggleOneOrTwo, kModAlt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 1, 0, 1, 0, 1));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, CtrlAltPressSetsTheTrackAloneToTheMax) {
  SetRecMode(top_[0], 2);
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);
  PressValue(top_[2], kToggleOneOrTwo, kModCtrl | kModAlt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 0, 0, 0, 2, 0));
  EXPECT_EQ(GetLastTouched(), "T3");
}

TEST_F(TrackActionsTest, ShiftAltPressSetsTheTrackAndItsGroupToTheMax) {
  // T3 already has the max, and its group still goes to it.
  SetRecMode(top_[0], 2);
  SetRecMode(top_[2], 2);
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);
  PressValue(top_[2], kToggleOneOrTwo, kModShift | kModAlt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 0, 0, 0, 2, 2));
  EXPECT_EQ(GetLastTouched(), "T3");
}

TEST_F(TrackActionsTest, AltPressSetsEachTrackOnce) {
  // Every track is past the min, and the press would raise T3 and T4, so
  // turning them down first, and up again, would repeat their setters, which
  // the counter fails.
  for (int i = 0; i < project_.GetTrackCount(); ++i) {
    SetRecMode(project_.GetTrack(i), 2);
  }
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);
  CallCounter counter(&reaper_);
  PressValue(top_[2], kToggleOneOrTwo, kModShift | kModAlt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 1, 1, 1, 2, 2));
  PressValue(top_[2], kToggleOneOrTwo, kModCtrl | kModAlt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 1, 1, 1, 2, 1));
}

TEST_F(TrackActionsTest, ShiftPressSetsTheRangeToTheLastTouchedValue) {
  SetRecMode(folder_[0], 1);
  PressValue(top_[0], kToggleNoneOfTwo);
  PressValue(top_[3], kToggleNoneOfTwo, kModShift);
  EXPECT_THAT(GetRecModes(), ElementsAre(2, 2, 1, 0, 2, 2));
  EXPECT_EQ(GetLastTouched(), "T1");

  // The last touched track's cached mode is stale, so the range reads it.
  SetRecMode(top_[0], 0);
  PressValue(top_[2], kToggleNoneOfTwo, kModShift);
  EXPECT_THAT(GetRecModes(), ElementsAre(0, 0, 1, 0, 0, 2));
}

TEST_F(TrackActionsTest, CtrlShiftPressSetsEveryTrackInTheRange) {
  PressValue(top_[3], kToggleNoneOfTwo);
  PressValue(folder_[1], kToggleNoneOfTwo, kCtrlShift);
  EXPECT_THAT(GetRecModes(), ElementsAre(0, 0, 0, 2, 2, 2));
  EXPECT_EQ(GetLastTouched(), "T4");
}

TEST_F(TrackActionsTest, OptPressAppliesItToEachSelectedTrackAlone) {
  top_[0]->selected = true;
  top_[2]->selected = true;
  JoinGroup(top_[2], 1);
  JoinGroup(top_[3], 1);

  // Each track steps on its own, and T3's cached mode is stale, so the action
  // reads it.
  const EnumeratedPress step = {.min = 0, .max = 2};
  SetRecMode(top_[2], 2);
  PressValue(top_[0], step, kModOpt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 0, 0, 0, 0, 0));
  EXPECT_EQ(GetLastTouched(), "T1");

  // A track that isn't selected is pressed alone.
  JoinGroup(top_[1], 1);
  PressValue(top_[1], step, kModOpt);
  EXPECT_THAT(GetRecModes(), ElementsAre(1, 1, 0, 0, 0, 0));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, AnAnchoredPressSetsTheRangeFromIt) {
  // The anchor track's cached mode is stale, so the range reads it, and the
  // modifiers are ignored.
  Anchor<Track> anchor;
  AnchorHold hold = anchor.Hold(GetCachedTrack(top_[0]));
  SetRecMode(top_[0], 2);
  SetLastTouched(top_[3]);
  PressValue(top_[2], kToggleOneOrTwo, kModShift, &anchor);
  EXPECT_THAT(GetRecModes(), ElementsAre(2, 2, 0, 0, 2, 0));
  EXPECT_EQ(GetLastTouched(), "T4");
}

TEST_F(TrackActionsTest, PressOnTheStubTrackDoesNothing) {
  actions_.UiPress(TrackCache::Get().GetStubTrack(), kTestValue,
                   kToggleNoneOfTwo, /*anchor=*/nullptr);
  EXPECT_THAT(GetRecModes(), ElementsAre(0, 0, 0, 0, 0, 0));
}

//==============================================================================
// Anchors
//==============================================================================

TEST_F(TrackActionsTest, AnAnchorActsOnTheRangeFromIt) {
  // The anchor track's cached value is stale, so the range reads it.
  top_[0]->mute = true;
  AnchorHold hold = actions_.GetAnchor(TrackAnchorAction::kMute)
                        .Hold(GetCachedTrack(top_[0]));
  SetLastTouched(top_[3]);

  // The range has the same parent as the anchor, and the modifiers, even
  // Shift's own range, are ignored.
  Mute(top_[2], kModShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T1", "T2", "T3"));
  EXPECT_EQ(GetLastTouched(), "T4");
  Mute(top_[3], kModAlt);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute),
              ElementsAre("T1", "T2", "T3", "T4"));
}

TEST_F(TrackActionsTest, AnAnchorSelectsTheRangeFromIt) {
  top_[3]->selected = true;
  AnchorHold hold = actions_.GetAnchor(TrackAnchorAction::kSelected)
                        .Hold(GetCachedTrack(top_[1]));
  SetLastTouched(top_[0]);
  Select(top_[2], kCtrlShift);
  EXPECT_THAT(GetTracksWith(&FakeTrack::selected), ElementsAre("T2", "T3"));
  EXPECT_EQ(GetLastTouched(), "T1");
}

TEST_F(TrackActionsTest, AnAnchorIsOnlyForItsAction) {
  AnchorHold hold = actions_.GetAnchor(TrackAnchorAction::kSolo)
                        .Hold(GetCachedTrack(top_[0]));
  Mute(top_[2]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T3"));
}

TEST_F(TrackActionsTest, AnAnchorOnTheTrackItselfActsAsUsual) {
  AnchorHold hold = actions_.GetAnchor(TrackAnchorAction::kMute)
                        .Hold(GetCachedTrack(top_[2]));
  Mute(top_[2]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T3"));
}

TEST_F(TrackActionsTest, AnAnchorOnADeletedTrackActsAsUsual) {
  AnchorHold hold = actions_.GetAnchor(TrackAnchorAction::kMute)
                        .Hold(GetCachedTrack(top_[0]));
  project_.DeleteTrack(top_[0]);
  TrackCache::Get().Refresh();
  Mute(top_[2]);
  EXPECT_THAT(GetTracksWith(&FakeTrack::mute), ElementsAre("T3"));
}

}  // namespace
}  // namespace jpr
