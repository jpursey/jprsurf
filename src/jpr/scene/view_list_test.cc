// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_list.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/anchor.h"
#include "jpr/common/control_surface.h"
#include "jpr/common/testing/cached_track.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/route_properties.h"
#include "jpr/scene/route_reference.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_property.h"
#include "jpr/scene/view_reference.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::SizeIs;

// The project is T1 to T5, with T3 a folder of T3.1, T3.2, and T3.3. Its
// routes, in the order they were added:
// - T2 and T4 send to T3.
// - T1 sends to T2, T3, and T4.
// - T5, T4, and T2 send to T1.
//
// So T3 only has receives, and T1, T2, and T4 have both. A track's sends are in
// the order of the tracks they go to, as REAPER's are: T2's and T4's send to T1
// comes first.
//
// The scene has two track references for lists to be bound to:
// "user:folder", which falls back to the master track, and "user:track". A
// third, "user:revealed", is for a list to reveal. The root view is enabled.
//
// Each test runs the list's actions itself, between runs, as a mapping runs
// them during one.
class ViewListTest : public SceneTest {
 protected:
  ViewListTest()
      : top_(project_.AddTracks(5)),
        folder_(project_.AddTracks(3, top_[2])),
        folder_reference_(scene_.AddTrackReference(
            "user:folder", {.fallback = std::string(Scene::kMasterTrack)})),
        track_reference_(scene_.AddTrackReference("user:track")),
        revealed_reference_(scene_.AddTrackReference("user:revealed")) {
    project_.AddSend(top_[1], top_[2]);
    project_.AddSend(top_[3], top_[2]);
    project_.AddSend(top_[0], top_[1]);
    project_.AddSend(top_[0], top_[2]);
    project_.AddSend(top_[0], top_[3]);
    project_.AddSend(top_[4], top_[0]);
    project_.AddSend(top_[3], top_[0]);
    project_.AddSend(top_[1], top_[0]);
    scene_.GetRootView()->Enable();
  }

  // Adds a view bound to `reference`, with the list, and returns it, or null
  // if it fails to be added.
  View* TryAddList(std::string_view reference, const View::ListConfig& list,
                   std::string_view name = "List") {
    return scene_.GetRootView()->AddChildView(
        name, {.subject = View::ReferenceSubject{std::string(reference)},
               .list = list});
  }

  // Adds an enabled view, as TryAddList() does, with `item_count` enabled list
  // item views.
  View* AddList(std::string_view reference, const View::ListConfig& list,
                int item_count = 2, std::string_view name = "List") {
    View* view = TryAddList(reference, list, name);
    EXPECT_NE(view, nullptr) << name;
    if (view == nullptr) {
      return nullptr;
    }
    view->Enable();
    for (int i = 0; i < item_count; ++i) {
      view->AddChildView(absl::StrCat("Item", i),
                         {.subject = View::ListItemSubject{}})
          ->Enable();
    }
    return view;
  }

  // Adds a list of the child tracks of "user:folder", which reveals the
  // reference `reveal`, if it is set.
  View* AddChildTracks(int item_count = 2, std::string reveal = "") {
    return AddList("user:folder",
                   {.items = View::ChildTracks{.reveal = std::move(reveal)}},
                   item_count);
  }

  // Adds a list of the routes of "user:track".
  View* AddRoutes(std::string_view name = "List",
                  View::RouteTypeRule rule =
                      View::RouteTypeRule::kSendsUnlessOnlyReceives) {
    return AddList("user:track",
                   {.items = View::Routes{.route_type_rule = rule}}, 2, name);
  }

  // Sets the reference to `track`, and runs, which lays out the lists bound to
  // it.
  void SetReference(TrackReference* reference, FakeTrack* track) {
    reference->Set(GetCachedTrack(track));
    surface_->Run();
  }

  // Returns what each of the list's items shows: its track's name, or for a
  // route, the name of the track at its other end, or "" if it shows nothing.
  static std::vector<std::string> GetShown(const View* list) {
    std::vector<std::string> shown;
    for (int i = 0; i < list->GetChildViewCount(); ++i) {
      shown.push_back(GetShownBy(list->GetChildViewAt(i)));
    }
    return shown;
  }
  static std::string GetShownBy(const View* item) {
    const ViewReference* subject = item->GetSubject();
    if (subject->GetKind() == SubjectKind::kRoute) {
      const TrackRoute* route =
          static_cast<const RouteReference*>(subject)->GetRoute();
      return route != nullptr ? std::string(route->other_track->GetName()) : "";
    }
    const Track* track =
        static_cast<const TrackReference*>(subject)->GetTrack();
    return track != nullptr ? std::string(track->GetName()) : "";
  }

  // Returns the route type a routes list shows, "Send" or "Recv".
  static std::string GetRouteTypeName(const View* list) {
    ViewProperty* name = list->GetProperty(View::kChildRouteTypeName);
    EXPECT_NE(name, nullptr);
    return name != nullptr ? name->GetText() : "";
  }

  // Runs the view's action property `name`.
  static void RunAction(View* view, std::string_view name) {
    ViewProperty* action = view->GetProperty(name);
    ASSERT_NE(action, nullptr) << name;
    action->RunAction();
  }

  FakeProject& project_ = reaper_.GetProject();
  std::vector<FakeTrack*> top_;
  std::vector<FakeTrack*> folder_;
  TrackReference* const folder_reference_;
  TrackReference* const track_reference_;
  TrackReference* const revealed_reference_;
};

//==============================================================================
// Properties
//==============================================================================

TEST_F(ViewListTest, EachListHasThePropertiesThatActOnIt) {
  View* plain = scene_.GetRootView()->AddChildView("Plain");
  View* child_tracks = AddChildTracks();
  View* routes = AddRoutes("Routes");

  EXPECT_EQ(plain->GetProperty(View::kChildInc), nullptr);
  for (View* list : {child_tracks, routes}) {
    for (std::string_view name :
         {View::kChildDec, View::kChildInc, View::kBankDec, View::kBankInc}) {
      EXPECT_NE(list->GetProperty(name), nullptr) << name;
    }
  }

  EXPECT_NE(child_tracks->GetProperty(View::kTrackParent), nullptr);
  EXPECT_NE(child_tracks->GetProperty(View::kTrackRoot), nullptr);
  EXPECT_EQ(child_tracks->GetProperty(View::kChildRouteToggle), nullptr);
  View* track_item = child_tracks->GetChildViewAt(0);
  EXPECT_NE(track_item->GetProperty(View::kParentTrackChild), nullptr);
  EXPECT_NE(track_item->GetProperty(View::kParentTrackParent), nullptr);
  EXPECT_NE(track_item->GetProperty(View::kParentTrackRoot), nullptr);
  EXPECT_EQ(track_item->GetProperty(View::kParentRouteOtherTrack), nullptr);

  EXPECT_NE(routes->GetProperty(View::kChildRouteToggle), nullptr);
  EXPECT_NE(routes->GetProperty(View::kChildRouteTypeName), nullptr);
  EXPECT_EQ(routes->GetProperty(View::kTrackParent), nullptr);
  View* route_item = routes->GetChildViewAt(0);
  EXPECT_NE(route_item->GetProperty(View::kParentRouteOtherTrack), nullptr);
  EXPECT_EQ(route_item->GetProperty(View::kParentTrackChild), nullptr);
}

TEST_F(ViewListTest, NavigatingNeedsAWritableReference) {
  View* child_tracks =
      AddList(Scene::kMasterTrack, {.items = View::ChildTracks{}}, 2, "Tracks");
  View* routes =
      AddList(Scene::kMasterTrack, {.items = View::Routes{}}, 2, "Routes");

  EXPECT_NE(child_tracks->GetProperty(View::kChildInc), nullptr);
  EXPECT_EQ(child_tracks->GetProperty(View::kTrackParent), nullptr);
  EXPECT_EQ(child_tracks->GetProperty(View::kTrackRoot), nullptr);
  EXPECT_EQ(
      child_tracks->GetChildViewAt(0)->GetProperty(View::kParentTrackChild),
      nullptr);
  EXPECT_EQ(
      routes->GetChildViewAt(0)->GetProperty(View::kParentRouteOtherTrack),
      nullptr);
}

TEST_F(ViewListTest, ActionsSeeTheSubjectChangedEarlierInTheRun) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);

  // The list lays out for T3 before it scrolls.
  track_reference_->Set(GetCachedTrack(top_[2]));
  RunAction(list, View::kChildInc);
  EXPECT_EQ(GetRouteTypeName(list), "Recv");
  EXPECT_THAT(GetShown(list), ElementsAre("T4", "T1"));
}

//==============================================================================
// Child tracks
//==============================================================================

TEST_F(ViewListTest, ShowsTheChildTracksOnTheSurface) {
  project_.ShowInMixer(top_[1], false);
  View* list = AddChildTracks(4);
  AddSurface();
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T3", "T4", "T5"));

  // Items past the end of the list show nothing.
  SetReference(folder_reference_, top_[2]);
  EXPECT_THAT(GetShown(list), ElementsAre("T3.1", "T3.2", "T3.3", ""));
}

TEST_F(ViewListTest, ScrollsByOneOrABankInRange) {
  View* list = AddChildTracks();
  AddSurface();
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));

  RunAction(list, View::kChildInc);
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));
  RunAction(list, View::kBankInc);
  EXPECT_THAT(GetShown(list), ElementsAre("T4", "T5"));
  RunAction(list, View::kChildInc);
  EXPECT_THAT(GetShown(list), ElementsAre("T4", "T5"));
  RunAction(list, View::kBankDec);
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));
  RunAction(list, View::kBankDec);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
  RunAction(list, View::kChildDec);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
}

TEST_F(ViewListTest, ABankIsTheBankSizeIfItHasOne) {
  View* list =
      AddList("user:folder", {.items = View::ChildTracks{}, .bank_size = 3});
  AddSurface();

  RunAction(list, View::kBankInc);
  EXPECT_THAT(GetShown(list), ElementsAre("T4", "T5"));
  RunAction(list, View::kBankDec);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
}

TEST_F(ViewListTest, ANewSubjectShowsItsListFromTheStart) {
  View* list = AddChildTracks();
  AddSurface();
  RunAction(list, View::kBankInc);

  SetReference(folder_reference_, top_[2]);
  EXPECT_THAT(GetShown(list), ElementsAre("T3.1", "T3.2"));
}

TEST_F(ViewListTest, TheTrackListChangingKeepsThePositionInRange) {
  View* list = AddChildTracks();
  AddSurface();
  RunAction(list, View::kChildInc);
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));

  // The surface polls for a track shown or hidden.
  project_.ShowInMixer(top_[0], false);
  surface_->RunFor(ControlSurface::kVisibilityInterval +
                   FakeReaper::GetRunTime());
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));

  project_.DeleteTrack(top_[4]);
  surface_->SetTrackListChange();
  surface_->Run();
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));

  project_.DeleteTrack(top_[3]);
  surface_->SetTrackListChange();
  surface_->Run();
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));
}

TEST_F(ViewListTest, ItemsReleaseTheirAnchorsWhenTheListMoves) {
  View* list = AddChildTracks();
  AddSurface();
  Anchor<Track> anchor;
  list->GetChildViewAt(0)->SetAnchor(anchor.Hold(GetCachedTrack(top_[0])));
  ASSERT_TRUE(anchor.IsHeld());

  RunAction(list, View::kChildInc);
  EXPECT_FALSE(anchor.IsHeld());
}

TEST_F(ViewListTest, NavigatesIntoAFolder) {
  View* list = AddChildTracks(3);
  AddSurface();

  // T1 has no child tracks.
  RunAction(list->GetChildViewAt(0), View::kParentTrackChild);
  EXPECT_EQ(folder_reference_->GetTrack(), TrackCache::Get().GetMasterTrack());

  RunAction(list->GetChildViewAt(2), View::kParentTrackChild);
  EXPECT_EQ(folder_reference_->GetTrack(), GetCachedTrack(top_[2]));
  EXPECT_THAT(GetShown(list), ElementsAre("T3.1", "T3.2", "T3.3"));
}

TEST_F(ViewListTest, NavigatesUpWithTheFolderCentered) {
  View* list = AddChildTracks(3);
  AddSurface();

  // The master track has no parent.
  RunAction(list, View::kTrackParent);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2", "T3"));

  SetReference(folder_reference_, top_[2]);
  RunAction(list, View::kTrackParent);
  EXPECT_EQ(folder_reference_->GetTrack(), TrackCache::Get().GetMasterTrack());
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3", "T4"));

  SetReference(folder_reference_, top_[2]);
  RunAction(list->GetChildViewAt(0), View::kParentTrackParent);
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3", "T4"));
}

TEST_F(ViewListTest, NavigatesToTheRoot) {
  View* list = AddChildTracks(3);
  AddSurface();

  SetReference(folder_reference_, top_[2]);
  RunAction(list, View::kTrackRoot);
  EXPECT_EQ(folder_reference_->GetTrack(), TrackCache::Get().GetMasterTrack());
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2", "T3"));

  SetReference(folder_reference_, top_[2]);
  RunAction(list->GetChildViewAt(1), View::kParentTrackRoot);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2", "T3"));
}

//==============================================================================
// Reveal
//==============================================================================

TEST_F(ViewListTest, RevealsWhenItBecomesActive) {
  View* list = AddChildTracks(2, "user:revealed");
  list->Disable();
  AddSurface();
  revealed_reference_->Set(GetCachedTrack(top_[4]));

  list->Enable();
  EXPECT_THAT(GetShown(list), ElementsAre("T4", "T5"));
}

TEST_F(ViewListTest, RevealsByScrollingAsLittleAsPossible) {
  View* list = AddChildTracks(2, "user:revealed");
  AddSurface();

  SetReference(revealed_reference_, top_[3]);
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));
  SetReference(revealed_reference_, top_[2]);
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));
  SetReference(revealed_reference_, top_[1]);
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));
}

TEST_F(ViewListTest, RevealsOnlyWhenTheReferenceChanges) {
  View* list = AddChildTracks(2, "user:revealed");
  AddSurface();
  SetReference(revealed_reference_, top_[3]);

  RunAction(list, View::kBankDec);
  surface_->Run();
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
}

TEST_F(ViewListTest, RevealsATrackInAnotherFolderAsTheLastItem) {
  View* list = AddChildTracks(2, "user:revealed");
  AddSurface();

  SetReference(revealed_reference_, folder_[2]);
  EXPECT_EQ(folder_reference_->GetTrack(), GetCachedTrack(top_[2]));
  EXPECT_THAT(GetShown(list), ElementsAre("T3.2", "T3.3"));

  SetReference(revealed_reference_, top_[0]);
  EXPECT_EQ(folder_reference_->GetTrack(), TrackCache::Get().GetMasterTrack());
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
}

TEST_F(ViewListTest, RevealsNothingOffTheSurface) {
  project_.ShowInMixer(top_[4], false);
  View* list = AddChildTracks(2, "user:revealed");
  AddSurface();

  SetReference(revealed_reference_, top_[4]);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
  SetReference(revealed_reference_, project_.GetMasterTrack());
  EXPECT_THAT(GetShown(list), ElementsAre("T1", "T2"));
}

TEST_F(ViewListTest, RevealNeedsAnotherTrackReferenceAndAWritableOne) {
  EXPECT_EQ(TryAddList("user:folder",
                       {.items = View::ChildTracks{.reveal = "user:nothing"}}),
            nullptr);
  EXPECT_EQ(TryAddList(Scene::kMasterTrack,
                       {.items = View::ChildTracks{.reveal = "user:revealed"}}),
            nullptr);
  EXPECT_EQ(TryAddList("user:folder",
                       {.items = View::ChildTracks{.reveal = "user:folder"}}),
            nullptr);
  EXPECT_THAT(log_error_guard_.TakeMessages(), SizeIs(3));
}

//==============================================================================
// Routes
//==============================================================================

TEST_F(ViewListTest, ShowsTheTracksRoutes) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));

  RunAction(list, View::kChildInc);
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));

  // Items past the end of the list show nothing.
  SetReference(track_reference_, top_[4]);
  EXPECT_THAT(GetShown(list), ElementsAre("T1", ""));
}

TEST_F(ViewListTest, TheRouteTypeIsPickedByTheRule) {
  View* sends = AddRoutes("Sends", View::RouteTypeRule::kSends);
  View* receives = AddRoutes("Receives", View::RouteTypeRule::kReceives);
  View* either =
      AddRoutes("Either", View::RouteTypeRule::kSendsUnlessOnlyReceives);
  AddSurface();

  SetReference(track_reference_, top_[0]);
  EXPECT_THAT(GetShown(sends), ElementsAre("T2", "T3"));
  EXPECT_THAT(GetShown(receives), ElementsAre("T5", "T4"));
  EXPECT_THAT(GetShown(either), ElementsAre("T2", "T3"));

  SetReference(track_reference_, top_[2]);
  EXPECT_THAT(GetShown(sends), ElementsAre("", ""));
  EXPECT_THAT(GetShown(receives), ElementsAre("T2", "T4"));
  EXPECT_THAT(GetShown(either), ElementsAre("T2", "T4"));
}

TEST_F(ViewListTest, TheToggleSwitchesTheRouteTypeFromTheStart) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);
  EXPECT_EQ(GetRouteTypeName(list), "Send");

  RunAction(list, View::kChildInc);
  RunAction(list, View::kChildRouteToggle);
  EXPECT_EQ(GetRouteTypeName(list), "Recv");
  EXPECT_THAT(GetShown(list), ElementsAre("T5", "T4"));

  RunAction(list, View::kChildRouteToggle);
  EXPECT_EQ(GetRouteTypeName(list), "Send");
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T3"));

  // T3 has no sends to switch to.
  SetReference(track_reference_, top_[2]);
  EXPECT_EQ(GetRouteTypeName(list), "Recv");
  RunAction(list, View::kChildRouteToggle);
  EXPECT_EQ(GetRouteTypeName(list), "Recv");
  EXPECT_THAT(GetShown(list), ElementsAre("T2", "T4"));
}

TEST_F(ViewListTest, CrossesARouteToShowTheRouteBack) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);

  // T1's send to T3 is T3's last receive.
  RunAction(list->GetChildViewAt(1), View::kParentRouteOtherTrack);
  EXPECT_EQ(track_reference_->GetTrack(), GetCachedTrack(top_[2]));
  EXPECT_EQ(GetRouteTypeName(list), "Recv");
  EXPECT_THAT(GetShown(list), ElementsAre("T4", "T1"));

  // T4's first send, to T1, is T1's middle receive, which is shown as the last
  // item.
  SetReference(track_reference_, top_[3]);
  RunAction(list->GetChildViewAt(0), View::kParentRouteOtherTrack);
  EXPECT_EQ(track_reference_->GetTrack(), GetCachedTrack(top_[0]));
  EXPECT_THAT(GetShown(list), ElementsAre("T5", "T4"));
}

TEST_F(ViewListTest, CrossingShowsTheOtherRouteTypeWhateverTheRule) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);
  RunAction(list, View::kChildInc);

  // T4 has sends, but shows the receive from T1, after the next run too.
  RunAction(list->GetChildViewAt(1), View::kParentRouteOtherTrack);
  surface_->Run();
  EXPECT_EQ(track_reference_->GetTrack(), GetCachedTrack(top_[3]));
  EXPECT_EQ(GetRouteTypeName(list), "Recv");
  EXPECT_THAT(GetShown(list), ElementsAre("T1", ""));

  // And back, to T1's send to T4, its last.
  RunAction(list->GetChildViewAt(0), View::kParentRouteOtherTrack);
  EXPECT_EQ(track_reference_->GetTrack(), GetCachedTrack(top_[0]));
  EXPECT_EQ(GetRouteTypeName(list), "Send");
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));
}

TEST_F(ViewListTest, CrossingAnItemWithNoRouteDoesNothing) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[4]);

  RunAction(list->GetChildViewAt(1), View::kParentRouteOtherTrack);
  EXPECT_EQ(track_reference_->GetTrack(), GetCachedTrack(top_[4]));
  EXPECT_EQ(GetRouteTypeName(list), "Send");
  EXPECT_THAT(GetShown(list), ElementsAre("T1", ""));
}

TEST_F(ViewListTest, FollowsTheRoutesChanging) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);

  project_.DeleteRoute(project_.GetSends(top_[0])[0]);
  surface_->SetTrackListChange();
  surface_->Run();
  EXPECT_THAT(GetShown(list), ElementsAre("T3", "T4"));
}

// REAPER doesn't report every change to a route's values.
TEST_F(ViewListTest, PollsTheValuesOfTheRoutes) {
  View* list = AddRoutes();
  AddSurface();
  SetReference(track_reference_, top_[0]);
  ViewProperty* volume =
      list->GetChildViewAt(0)->GetProperty(RouteProperties::kVolume);
  ASSERT_NE(volume, nullptr);

  project_.GetSends(top_[0])[0]->volume = 0.5;
  surface_->Run();
  EXPECT_EQ(volume->GetVolume(), 0.5);
}

}  // namespace
}  // namespace jpr
