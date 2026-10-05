// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_properties.h"

#include <string_view>
#include <vector>

#include "gb/test/log_error_guard.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

// The properties read the track cache, which the test refreshes itself, as a
// control surface would.
class TrackPropertiesTest : public ::testing::Test {
 protected:
  TrackPropertiesTest() : tracks_(project_.AddTracks(3)) {
    TrackCache::Get().Refresh();
  }

  // Returns the cache's track for the fake's track.
  static Track* GetTrack(FakeTrack* track) {
    return TrackCache::Get().GetTrack(ToMediaTrack(track));
  }

  gb::LogErrorGuard log_error_guard_;  // First, so it outlives the rest.
  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> tracks_;
  TrackActions actions_{TrackFilter::kMcp};
  bool changed_ = false;
};

TEST_F(TrackPropertiesTest, ReadsTheTrack) {
  FakeTrack* track = tracks_[1];
  track->color = 0x01302010;
  track->volume = 0.5;
  track->pan = -0.25;
  track->selected = true;
  track->mute = true;
  track->solo = true;
  track->rec_arm = true;
  GetTrack(track)->Refresh();
  TrackProperties properties(&actions_, GetTrack(track));

  EXPECT_EQ(properties.GetProperty(TrackProperties::kName)->GetText(), "T2");
  EXPECT_EQ(properties.GetProperty(TrackProperties::kColor)->GetColor(),
            (Color{0x10, 0x20, 0x30}));
  for (std::string_view name :
       {TrackProperties::kVolume, TrackProperties::kUiVolume}) {
    EXPECT_EQ(properties.GetProperty(name)->GetVolume(), 0.5) << name;
  }
  for (std::string_view name :
       {TrackProperties::kPan, TrackProperties::kUiPan}) {
    EXPECT_EQ(properties.GetProperty(name)->GetPan(), -0.25) << name;
  }
  for (std::string_view name :
       {TrackProperties::kSelected, TrackProperties::kUiSelected,
        TrackProperties::kMute, TrackProperties::kUiMute,
        TrackProperties::kSolo, TrackProperties::kUiSolo,
        TrackProperties::kRecArm, TrackProperties::kUiRecArm}) {
    EXPECT_TRUE(properties.GetProperty(name)->GetBool()) << name;
  }
}

TEST_F(TrackPropertiesTest, PropertiesAreCreatedOnce) {
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));

  ViewProperty* mute = properties.GetProperty(TrackProperties::kMute);
  ASSERT_NE(mute, nullptr);
  EXPECT_EQ(mute->GetName(), TrackProperties::kMute);
  EXPECT_EQ(properties.GetProperty(TrackProperties::kMute), mute);
  EXPECT_NE(properties.GetProperty(TrackProperties::kUiMute), mute);
  EXPECT_EQ(properties.GetProperty("track:nothing"), nullptr);
}

TEST_F(TrackPropertiesTest, PlainPropertiesSetOnlyTheirTrack) {
  tracks_[0]->group = 1;
  tracks_[1]->group = 1;
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));

  properties.GetProperty(TrackProperties::kName)->SetText("Drums");
  properties.GetProperty(TrackProperties::kVolume)->SetVolume(0.5);
  properties.GetProperty(TrackProperties::kPan)->SetPan(0.25);
  properties.GetProperty(TrackProperties::kSelected)->SetBool(true);
  properties.GetProperty(TrackProperties::kMute)->SetBool(true);
  properties.GetProperty(TrackProperties::kSolo)->SetBool(true);
  properties.GetProperty(TrackProperties::kRecArm)->SetBool(true);

  EXPECT_EQ(tracks_[0]->name, "Drums");
  EXPECT_EQ(tracks_[0]->volume, 0.5);
  EXPECT_EQ(tracks_[0]->pan, 0.25);
  EXPECT_TRUE(tracks_[0]->selected);
  EXPECT_TRUE(tracks_[0]->mute);
  EXPECT_TRUE(tracks_[0]->solo);
  EXPECT_TRUE(tracks_[0]->rec_arm);
  EXPECT_EQ(tracks_[1]->volume, 1.0);
  EXPECT_EQ(tracks_[1]->pan, 0.0);
  EXPECT_FALSE(tracks_[1]->mute);
  EXPECT_FALSE(tracks_[1]->solo);
  EXPECT_FALSE(tracks_[1]->rec_arm);
}

// Each ui_ property runs the track action (see TrackActions, whose tests
// cover each action), which by default changes the track's group too, where a
// plain property changes only its own track.
TEST_F(TrackPropertiesTest, UiPropertiesRunTheTrackActions) {
  tracks_[0]->group = 1;
  tracks_[1]->group = 1;
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));

  properties.GetProperty(TrackProperties::kUiMute)->SetBool(true);
  properties.GetProperty(TrackProperties::kUiVolume)->SetVolume(0.5);
  properties.GetProperty(TrackProperties::kUiPan)->SetPan(0.25);
  EXPECT_TRUE(tracks_[1]->mute);
  EXPECT_EQ(tracks_[1]->volume, 0.5);
  EXPECT_EQ(tracks_[1]->pan, 0.25);
  EXPECT_FALSE(tracks_[2]->mute);
  EXPECT_EQ(tracks_[2]->volume, 1.0);
  EXPECT_EQ(tracks_[2]->pan, 0.0);
}

// Selecting changes the selection of more than one track, which is a change
// of its own (see the batching check in FakeReaper).
TEST_F(TrackPropertiesTest, UiSelectedRunsTheTrackAction) {
  tracks_[2]->selected = true;
  GetTrack(tracks_[2])->Refresh();
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));

  properties.GetProperty(TrackProperties::kUiSelected)->SetBool(true);
  EXPECT_TRUE(tracks_[0]->selected);
  EXPECT_FALSE(tracks_[2]->selected);
}

TEST_F(TrackPropertiesTest, ChangesNotifyWhenTheTrackIsRefreshed) {
  Track* track = GetTrack(tracks_[0]);
  TrackProperties properties(&actions_, track);
  ViewProperty* mute = properties.GetProperty(TrackProperties::kMute);
  mute->RegisterFlag(&changed_);

  track->Refresh();
  EXPECT_FALSE(changed_);

  tracks_[0]->mute = true;
  EXPECT_FALSE(mute->GetBool());
  track->Refresh();
  EXPECT_TRUE(changed_);
  EXPECT_TRUE(mute->GetBool());
  mute->UnregisterFlag(&changed_);
}

TEST_F(TrackPropertiesTest, MeterNotifiesOnceWhenSilent) {
  Track* track = GetTrack(tracks_[0]);
  TrackProperties properties(&actions_, track);
  ViewProperty* meter = properties.GetProperty(TrackProperties::kMeter);
  meter->RegisterFlag(&changed_);
  EXPECT_TRUE(properties.IsMeterWatched());

  tracks_[0]->peak = {0.5, 0.5};
  track->RefreshMeter();
  EXPECT_TRUE(changed_);
  EXPECT_EQ(meter->GetNormalized(), 0.5);

  tracks_[0]->peak = {0.0, 0.0};
  changed_ = false;
  track->RefreshMeter();
  EXPECT_TRUE(changed_);
  EXPECT_EQ(meter->GetNormalized(), 0.0);
  changed_ = false;
  track->RefreshMeter();
  EXPECT_FALSE(changed_);
  meter->UnregisterFlag(&changed_);
}

TEST_F(TrackPropertiesTest, IsFolderCountsOnlyChildrenOnTheSurface) {
  FakeTrack* child = project_.AddTracks(1, tracks_[0])[0];
  TrackCache::Get().Refresh();
  TrackProperties folder(&actions_, GetTrack(tracks_[0]));
  TrackProperties other(&actions_, GetTrack(tracks_[1]));
  EXPECT_TRUE(folder.GetProperty(TrackProperties::kTrackIsFolder)->GetBool());
  EXPECT_FALSE(other.GetProperty(TrackProperties::kTrackIsFolder)->GetBool());

  child->show_in_mixer = false;
  TrackCache::Get().RefreshVisibility();
  EXPECT_FALSE(folder.GetProperty(TrackProperties::kTrackIsFolder)->GetBool());
}

TEST_F(TrackPropertiesTest, OnlyTheMasterTrackHasNoParent) {
  FakeTrack* child = project_.AddTracks(1, tracks_[0])[0];
  TrackCache::Get().Refresh();
  TrackProperties master(&actions_, TrackCache::Get().GetMasterTrack());
  TrackProperties top(&actions_, GetTrack(tracks_[0]));
  TrackProperties inner(&actions_, GetTrack(child));

  EXPECT_FALSE(master.GetProperty(TrackProperties::kTrackHasParent)->GetBool());
  EXPECT_TRUE(top.GetProperty(TrackProperties::kTrackHasParent)->GetBool());
  EXPECT_TRUE(inner.GetProperty(TrackProperties::kTrackHasParent)->GetBool());
}

TEST_F(TrackPropertiesTest, ExistsUntilTheTrackIsDeleted) {
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));
  TrackProperties stub(&actions_);
  ViewProperty* exists = properties.GetProperty(TrackProperties::kTrackExists);
  EXPECT_TRUE(exists->GetBool());
  EXPECT_FALSE(stub.GetProperty(TrackProperties::kTrackExists)->GetBool());

  exists->RegisterFlag(&changed_);
  project_.DeleteTrack(tracks_[0]);
  TrackCache::Get().Refresh();
  EXPECT_TRUE(changed_);
  EXPECT_FALSE(exists->GetBool());
  exists->UnregisterFlag(&changed_);
}

TEST_F(TrackPropertiesTest, HasRoutesForSendsAndReceives) {
  project_.AddSend(tracks_[0], tracks_[1]);
  TrackCache::Get().Refresh();
  TrackProperties source(&actions_, GetTrack(tracks_[0]));
  TrackProperties destination(&actions_, GetTrack(tracks_[1]));
  TrackProperties other(&actions_, GetTrack(tracks_[2]));
  ViewProperty* has_routes =
      other.GetProperty(TrackProperties::kTrackHasRoutes);
  EXPECT_TRUE(source.GetProperty(TrackProperties::kTrackHasRoutes)->GetBool());
  EXPECT_TRUE(
      destination.GetProperty(TrackProperties::kTrackHasRoutes)->GetBool());
  EXPECT_FALSE(has_routes->GetBool());

  has_routes->RegisterFlag(&changed_);
  project_.AddSend(tracks_[2], tracks_[0]);
  TrackCache::Get().Refresh();
  EXPECT_TRUE(changed_);
  EXPECT_TRUE(has_routes->GetBool());
  has_routes->UnregisterFlag(&changed_);
}

TEST_F(TrackPropertiesTest, SettingTheTrackMovesEveryProperty) {
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));
  ViewProperty* name = properties.GetProperty(TrackProperties::kName);
  name->RegisterFlag(&changed_);

  properties.SetTrack(GetTrack(tracks_[1]));
  EXPECT_TRUE(changed_);
  EXPECT_EQ(properties.GetTrack(), GetTrack(tracks_[1]));
  EXPECT_EQ(properties.GetProperty(TrackProperties::kName), name);
  EXPECT_EQ(name->GetText(), "T2");
  name->UnregisterFlag(&changed_);
}

TEST_F(TrackPropertiesTest, IsWatchedWhileAnyPropertyIs) {
  TrackProperties properties(&actions_, GetTrack(tracks_[0]));
  ViewProperty* mute = properties.GetProperty(TrackProperties::kMute);
  EXPECT_FALSE(properties.IsWatched());

  mute->RegisterFlag(&changed_);
  EXPECT_TRUE(properties.IsWatched());
  EXPECT_FALSE(properties.IsMeterWatched());
  mute->UnregisterFlag(&changed_);
  EXPECT_FALSE(properties.IsWatched());
}

}  // namespace
}  // namespace jpr
