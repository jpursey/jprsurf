// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track.h"

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest-spi.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track_cache.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;
using ::testing::Optional;

// Records what a track reports to its listeners, for as long as it exists.
class TestListener final : public TrackListener {
 public:
  explicit TestListener(Track* track) : track_(track) {
    track_->Subscribe(this);
  }
  TestListener(const TestListener&) = delete;
  TestListener& operator=(const TestListener&) = delete;
  ~TestListener() override { track_->Unsubscribe(this); }

  void OnTrackChanged(Track* track) override { ++change_count; }
  void OnTrackMeterChanged(Track* track, double peak) override {
    peaks.push_back(peak);
  }
  void OnTrackRoutesChanged(Track* track) override { ++routes_change_count; }

  int change_count = 0;
  int routes_change_count = 0;
  std::vector<double> peaks;

 private:
  Track* const track_;
};

class TrackTest : public ::testing::Test {
 protected:
  // Refreshes the cache, and returns the cached Track for `track`.
  Track* Refresh(FakeTrack* track) {
    TrackCache::Get().Refresh();
    return GetCachedTrack(track);
  }

  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
};

TEST_F(TrackTest, ReadsREAPERsValues) {
  FakeTrack* fake = project_.AddTrack("Drums");
  fake->color = 0x01030201;
  fake->volume = 0.5;
  fake->pan = -0.25;
  fake->selected = true;
  fake->solo = true;

  Track* track = Refresh(fake);
  ASSERT_NE(track, nullptr);
  EXPECT_TRUE(track->Exists());
  EXPECT_EQ(track->GetName(), "Drums");
  EXPECT_EQ(track->GetColor(), (Color{1, 2, 3}));
  EXPECT_EQ(track->GetVolume(), 0.5);
  EXPECT_EQ(track->GetPan(), -0.25);
  EXPECT_TRUE(track->GetSelected());
  EXPECT_FALSE(track->GetMute());
  EXPECT_TRUE(track->GetSolo());
  EXPECT_FALSE(track->GetRecArm());
}

TEST_F(TrackTest, NoColorIsWhite) {
  Track* track = Refresh(project_.AddTrack("Drums"));
  EXPECT_EQ(track->GetColor(), (Color{255, 255, 255}));
}

TEST_F(TrackTest, SetsNameVolumeAndPan) {
  FakeTrack* fake = project_.AddTrack("Drums");
  Track* track = Refresh(fake);
  TestListener listener(track);

  track->SetName("Kit");
  track->SetVolume(0.25);
  track->SetPan(0.5);
  EXPECT_EQ(fake->name, "Kit");
  EXPECT_EQ(fake->volume, 0.25);
  EXPECT_EQ(fake->pan, 0.5);
  EXPECT_EQ(track->GetName(), "Kit");
  EXPECT_EQ(track->GetVolume(), 0.25);
  EXPECT_EQ(track->GetPan(), 0.5);
  EXPECT_EQ(listener.change_count, 3);

  // Setting the same values again changes nothing.
  track->SetVolume(0.25);
  track->SetPan(0.5);
  EXPECT_EQ(listener.change_count, 3);
}

TEST_F(TrackTest, MuteSoloAndRecArmEachAddAnUndoPoint) {
  FakeTrack* fake = project_.AddTrack("Drums");
  Track* track = Refresh(fake);

  track->SetMute(true);
  track->SetSolo(true);
  track->SetRecArm(true);
  EXPECT_TRUE(fake->mute);
  EXPECT_TRUE(fake->solo);
  EXPECT_TRUE(fake->rec_arm);
  EXPECT_TRUE(track->GetMute());
  EXPECT_THAT(
      project_.GetUndoPoints(),
      ElementsAre(Field(&FakeUndoPoint::name, "JPR:Toggle Mute"),
                  Field(&FakeUndoPoint::name, "JPR:Toggle Solo"),
                  Field(&FakeUndoPoint::name, "JPR:Toggle Record Arm")));

  // Setting a value REAPER already has adds no undo point.
  track->SetMute(true);
  EXPECT_EQ(project_.GetUndoPoints().size(), 3);
}

TEST_F(TrackTest, SetFollowsREAPERsValueNotTheCachedOne) {
  FakeTrack* fake = project_.AddTrack("Drums");
  Track* track = Refresh(fake);

  // Muted in REAPER, but not yet refreshed.
  fake->mute = true;
  track->SetMute(true);
  EXPECT_TRUE(track->GetMute());
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());
}

TEST_F(TrackTest, SelectionAddsNoUndoPoint) {
  FakeTrack* fake = project_.AddTrack("Drums");
  Track* track = Refresh(fake);
  track->SetSelected(true);
  EXPECT_TRUE(fake->selected);
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());
}

TEST_F(TrackTest, SelectOnlyUnselectsEveryOtherTrack) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  bass->selected = true;
  project_.GetMasterTrack()->selected = true;

  Refresh(drums)->SelectOnly();
  EXPECT_TRUE(drums->selected);
  EXPECT_FALSE(bass->selected);
  EXPECT_FALSE(project_.GetMasterTrack()->selected);
}

TEST_F(TrackTest, DeletedTrackHasNoValues) {
  FakeTrack* fake = project_.AddTrack("Drums");
  fake->mute = true;
  Track* track = Refresh(fake);
  TestListener listener(track);

  project_.DeleteTrack(fake);
  TrackCache::Get().Refresh();
  EXPECT_FALSE(track->Exists());
  EXPECT_EQ(track->GetName(), "");
  EXPECT_FALSE(track->GetMute());
  EXPECT_EQ(listener.change_count, 1);

  // Setting a deleted track's values does nothing.
  track->SetMute(true);
  track->SetVolume(0.5);
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());
}

TEST_F(TrackTest, RefreshMeterReportsTheLouderChannel) {
  FakeTrack* fake = project_.AddTrack("Drums");
  fake->peak = {0.25, 0.5};
  Track* track = Refresh(fake);
  TestListener listener(track);

  track->RefreshMeter();
  fake->peak = {0.75, 0.5};
  track->RefreshMeter();
  EXPECT_THAT(listener.peaks, ElementsAre(0.5, 0.75));
}

//------------------------------------------------------------------------------
// Routes
//------------------------------------------------------------------------------

TEST_F(TrackTest, RefreshReadsRoutes) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bus = project_.AddTrack("Bus");
  FakeTrack* reverb = project_.AddTrack("Reverb");
  project_.AddHardwareOutput(drums);
  project_.AddSend(drums, bus)->volume = 0.5;
  project_.AddSend(drums, reverb)->pan = -0.25;
  project_.AddSend(bus, reverb)->mute = true;
  TrackCache::Get().Refresh();

  // Hardware outputs aren't routes between tracks, so they aren't listed.
  EXPECT_THAT(
      GetCachedTrack(drums)->GetSends(),
      ElementsAre(TrackRoute{.other_track = GetCachedTrack(bus), .volume = 0.5},
                  TrackRoute{.other_track = GetCachedTrack(reverb),
                             .volume = 1.0,
                             .pan = -0.25}));
  EXPECT_THAT(GetCachedTrack(drums)->GetReceives(), IsEmpty());
  EXPECT_THAT(GetCachedTrack(reverb)->GetReceives(),
              ElementsAre(TrackRoute{.other_track = GetCachedTrack(drums),
                                     .volume = 1.0,
                                     .pan = -0.25},
                          TrackRoute{.other_track = GetCachedTrack(bus),
                                     .volume = 1.0,
                                     .mute = true}));
}

TEST_F(TrackTest, RefreshNotifiesWhenRoutesChange) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bus = project_.AddTrack("Bus");
  TrackCache::Get().Refresh();
  TestListener drums_listener(GetCachedTrack(drums));
  TestListener bus_listener(GetCachedTrack(bus));

  FakeRoute* send = project_.AddSend(drums, bus);
  TrackCache::Get().Refresh();
  EXPECT_EQ(GetCachedTrack(drums)->GetSends().size(), 1);
  EXPECT_EQ(GetCachedTrack(bus)->GetReceives().size(), 1);
  EXPECT_EQ(drums_listener.routes_change_count, 1);
  EXPECT_EQ(bus_listener.routes_change_count, 1);

  // A refresh with no change to the routes notifies no one.
  TrackCache::Get().Refresh();
  EXPECT_EQ(drums_listener.routes_change_count, 1);

  project_.DeleteRoute(send);
  TrackCache::Get().Refresh();
  EXPECT_THAT(GetCachedTrack(drums)->GetSends(), IsEmpty());
  EXPECT_THAT(GetCachedTrack(bus)->GetReceives(), IsEmpty());
  EXPECT_EQ(drums_listener.routes_change_count, 2);
  EXPECT_EQ(bus_listener.routes_change_count, 2);
}

TEST_F(TrackTest, DeletingATrackRemovesItsRoutes) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bus = project_.AddTrack("Bus");
  FakeTrack* reverb = project_.AddTrack("Reverb");
  project_.AddSend(drums, bus);
  project_.AddSend(drums, reverb);
  Track* bus_track = Refresh(bus);

  project_.DeleteTrack(bus);
  TrackCache::Get().Refresh();
  EXPECT_THAT(
      GetCachedTrack(drums)->GetSends(),
      ElementsAre(Field(&TrackRoute::other_track, GetCachedTrack(reverb))));
  EXPECT_THAT(bus_track->GetReceives(), IsEmpty());
}

TEST_F(TrackTest, RefreshRoutesRereadsTheirValues) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bus = project_.AddTrack("Bus");
  FakeRoute* send = project_.AddSend(drums, bus);
  Track* track = Refresh(drums);
  TestListener listener(track);

  send->volume = 0.5;
  send->mute = true;
  track->RefreshRoutes();
  EXPECT_EQ(track->GetSends()[0].volume, 0.5);
  EXPECT_TRUE(track->GetSends()[0].mute);
  EXPECT_EQ(listener.routes_change_count, 1);

  // Each end of the route rereads its own values.
  EXPECT_EQ(GetCachedTrack(bus)->GetReceives()[0].volume, 1.0);
  GetCachedTrack(bus)->RefreshRoutes();
  EXPECT_EQ(GetCachedTrack(bus)->GetReceives()[0].volume, 0.5);

  // Rereading values that didn't change notifies no one.
  track->RefreshRoutes();
  EXPECT_EQ(listener.routes_change_count, 1);
}

TEST_F(TrackTest, SetsSendsAfterHardwareOutputs) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bus = project_.AddTrack("Bus");
  FakeTrack* reverb = project_.AddTrack("Reverb");
  FakeRoute* output = project_.AddHardwareOutput(drums);
  FakeRoute* to_bus = project_.AddSend(drums, bus);
  FakeRoute* to_reverb = project_.AddSend(drums, reverb);
  Track* track = Refresh(drums);
  TestListener listener(track);

  track->SetRouteVolume(TrackRouteType::kSend, 1, 0.5);
  track->SetRoutePan(TrackRouteType::kSend, 1, -0.25);
  track->SetRouteMute(TrackRouteType::kSend, 1, true);
  EXPECT_EQ(to_reverb->volume, 0.5);
  EXPECT_EQ(to_reverb->pan, -0.25);
  EXPECT_TRUE(to_reverb->mute);
  EXPECT_EQ(output->volume, 1.0);
  EXPECT_EQ(to_bus->volume, 1.0);
  EXPECT_EQ(track->GetSends()[1],
            (TrackRoute{.other_track = GetCachedTrack(reverb),
                        .volume = 0.5,
                        .pan = -0.25,
                        .mute = true}));
  EXPECT_EQ(listener.routes_change_count, 3);

  // Setting a route the track doesn't have does nothing.
  track->SetRouteVolume(TrackRouteType::kSend, 2, 0.5);
  EXPECT_EQ(listener.routes_change_count, 3);
}

TEST_F(TrackTest, SetsReceives) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bus = project_.AddTrack("Bus");
  FakeTrack* reverb = project_.AddTrack("Reverb");
  FakeRoute* from_drums = project_.AddSend(drums, reverb);
  FakeRoute* from_bus = project_.AddSend(bus, reverb);
  Track* track = Refresh(reverb);

  track->SetRouteVolume(TrackRouteType::kReceive, 1, 0.5);
  track->SetRoutePan(TrackRouteType::kReceive, 1, -0.25);
  track->SetRouteMute(TrackRouteType::kReceive, 1, true);
  EXPECT_EQ(from_bus->volume, 0.5);
  EXPECT_EQ(from_bus->pan, -0.25);
  EXPECT_TRUE(from_bus->mute);
  EXPECT_EQ(from_drums->volume, 1.0);
  EXPECT_FALSE(from_drums->mute);
}

TEST_F(TrackTest, RouteMuteFollowsREAPERsValueNotTheCachedOne) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeRoute* send = project_.AddSend(drums, project_.AddTrack("Bus"));
  Track* track = Refresh(drums);

  // Muted in REAPER, but not yet refreshed.
  send->mute = true;
  track->SetRouteMute(TrackRouteType::kSend, 0, true);
  EXPECT_TRUE(send->mute);
  EXPECT_TRUE(track->GetSends()[0].mute);
}

TEST_F(TrackTest, RouteMuteKeepsPendingChangesOutOfItsUndoPoint) {
  FakeTrack* drums = project_.AddTrack("Drums");
  project_.AddSend(drums, project_.AddTrack("Bus"));
  Track* track = Refresh(drums);

  // REAPER adds no undo point for a route's volume, so one is added once the
  // changes stop (see ContinuousUndo).
  track->SetRouteVolume(TrackRouteType::kSend, 0, 0.5);
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());

  // Toggling mute adds REAPER's own undo point, so the volume's is added first.
  track->SetRouteMute(TrackRouteType::kSend, 0, true);
  EXPECT_THAT(
      project_.GetUndoPoints(),
      ElementsAre(Field(&FakeUndoPoint::name, "JPR: Adjust send volume"),
                  Field(&FakeUndoPoint::name, "Toggle send mute")));
}

//------------------------------------------------------------------------------
// TrackBatch
//------------------------------------------------------------------------------

TEST_F(TrackTest, BatchAddsOneUndoPoint) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  TrackCache::Get().Refresh();
  {
    TrackBatch batch;
    batch.SetMute(GetCachedTrack(drums), true);
    batch.SetMute(GetCachedTrack(bass), true);
  }
  EXPECT_TRUE(drums->mute);
  EXPECT_TRUE(bass->mute);
  EXPECT_THAT(project_.GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, "JPR:Toggle Mute")));
}

TEST_F(TrackTest, BatchOfSeveralPropertiesAddsOneUndoPoint) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  TrackCache::Get().Refresh();
  {
    TrackBatch batch;
    batch.SetSolo(GetCachedTrack(drums), true);
    batch.SetRecArm(GetCachedTrack(bass), true);
    batch.SetSelected(GetCachedTrack(bass), true);
  }
  EXPECT_TRUE(drums->solo);
  EXPECT_TRUE(bass->rec_arm);
  EXPECT_TRUE(bass->selected);
  EXPECT_THAT(project_.GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, "JPR:Change Tracks")));
}

TEST_F(TrackTest, BatchOfSelectionAloneAddsNoUndoPoint) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* bass = project_.AddTrack("Bass");
  TrackCache::Get().Refresh();
  {
    TrackBatch batch;
    batch.SetSelected(GetCachedTrack(drums), true);
    batch.SetSelected(GetCachedTrack(bass), true);
  }
  EXPECT_TRUE(drums->selected);
  EXPECT_TRUE(bass->selected);
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());
}

TEST_F(TrackTest, GroupingChangesTheGroup) {
  FakeTrack* drums = project_.AddTrack("Drums");
  FakeTrack* kick = project_.AddTrack("Kick");
  drums->group = 1;
  kick->group = 1;
  TrackCache::Get().Refresh();

  GetCachedTrack(drums)->SetMute(true);
  GetCachedTrack(drums)->SetVolume(0.5);
  EXPECT_FALSE(kick->mute);
  EXPECT_EQ(kick->volume, 1.0);

  GetCachedTrack(drums)->SetSolo(true, TrackGrouping::kGrouped);
  GetCachedTrack(drums)->SetPan(0.25, TrackGrouping::kGrouped);
  EXPECT_TRUE(kick->solo);
  EXPECT_EQ(kick->pan, 0.25);
}

// The fake checks the test's own calls when it is destroyed.
TEST(TrackBatchTest, ChangingSeveralTracksWithoutABatchFailsTheTest) {
  EXPECT_NONFATAL_FAILURE(
      {
        FakeReaper reaper;
        FakeTrack* drums = reaper.GetProject().AddTrack("Drums");
        FakeTrack* bass = reaper.GetProject().AddTrack("Bass");
        TrackCache::Get().Refresh();
        GetCachedTrack(drums)->SetMute(true);
        GetCachedTrack(bass)->SetMute(true);
      },
      "in one TrackBatch");
}

//------------------------------------------------------------------------------
// TrackRange
//------------------------------------------------------------------------------

// The project is T1, T2 (a folder of T2.1 and T2.2), and T3.
class TrackRangeTest : public ::testing::Test {
 protected:
  TrackRangeTest()
      : top_(project_.AddTracks(3)), folder_(project_.AddTracks(2, top_[1])) {
    TrackCache::Get().Refresh();
  }

  static std::optional<TrackRange> Between(
      FakeTrack* from, FakeTrack* to, TrackFilter filter = TrackFilter::kAll,
      bool same_parent = false) {
    return TrackRange::Between(GetCachedTrack(from), GetCachedTrack(to), filter,
                               same_parent);
  }

  // Returns the names of the tracks in the range Between() returns, in order,
  // or nullopt if it returns none.
  std::optional<std::vector<std::string>> GetTracksBetween(
      FakeTrack* from, FakeTrack* to, TrackFilter filter = TrackFilter::kAll,
      bool same_parent = false) {
    std::optional<TrackRange> range = Between(from, to, filter, same_parent);
    if (!range.has_value()) {
      return std::nullopt;
    }
    std::vector<std::string> names;
    for (int i = 0; i < project_.GetTrackCount(); ++i) {
      if (range->Contains(GetCachedTrack(project_.GetTrack(i)))) {
        names.push_back(project_.GetTrack(i)->name);
      }
    }
    return names;
  }

  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> top_;
  std::vector<FakeTrack*> folder_;
};

TEST_F(TrackRangeTest, IncludesEveryTrackBetweenItsEndsInEitherOrder) {
  EXPECT_THAT(GetTracksBetween(folder_[0], top_[2]),
              Optional(ElementsAre("T2.1", "T2.2", "T3")));
  EXPECT_THAT(GetTracksBetween(top_[2], folder_[0]),
              Optional(ElementsAre("T2.1", "T2.2", "T3")));
  EXPECT_THAT(GetTracksBetween(top_[1], top_[1]), Optional(ElementsAre("T2")));
}

TEST_F(TrackRangeTest, AnEndNotInTheFilterMakesNoRange) {
  EXPECT_FALSE(TrackRange::Between(nullptr, GetCachedTrack(top_[0]),
                                   TrackFilter::kAll, false));
  EXPECT_FALSE(TrackRange::Between(GetCachedTrack(top_[0]), nullptr,
                                   TrackFilter::kAll, false));
  EXPECT_FALSE(Between(project_.GetMasterTrack(), top_[0]));

  project_.ShowInMixer(top_[2], false);
  TrackCache::Get().RefreshVisibility();
  EXPECT_FALSE(Between(top_[0], top_[2], TrackFilter::kMcp));
  EXPECT_FALSE(Between(top_[2], top_[0], TrackFilter::kMcp));
  EXPECT_TRUE(Between(top_[0], top_[2], TrackFilter::kTcp));
}

TEST_F(TrackRangeTest, SameParentIncludesOnlyTracksWithTheParentOfFrom) {
  EXPECT_THAT(GetTracksBetween(top_[0], top_[2], TrackFilter::kAll, true),
              Optional(ElementsAre("T1", "T2", "T3")));
  EXPECT_THAT(GetTracksBetween(folder_[0], top_[2], TrackFilter::kAll, true),
              Optional(ElementsAre("T2.1", "T2.2")));

  // From the top level, the folder's tracks are left out, even `to`.
  EXPECT_THAT(GetTracksBetween(top_[2], folder_[0], TrackFilter::kAll, true),
              Optional(ElementsAre("T3")));
}

TEST_F(TrackRangeTest, TracksNotInTheFilterAreLeftOut) {
  project_.ShowInMixer(folder_[0], false);
  TrackCache::Get().RefreshVisibility();

  EXPECT_THAT(GetTracksBetween(top_[1], top_[2], TrackFilter::kMcp),
              Optional(ElementsAre("T2", "T2.2", "T3")));
  EXPECT_THAT(GetTracksBetween(top_[1], top_[2], TrackFilter::kTcp),
              Optional(ElementsAre("T2", "T2.1", "T2.2", "T3")));

  std::optional<TrackRange> range = Between(top_[1], top_[2]);
  ASSERT_TRUE(range.has_value());
  EXPECT_FALSE(range->Contains(TrackCache::Get().GetMasterTrack()));
}

}  // namespace
}  // namespace jpr
