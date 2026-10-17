// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/large_project.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::SizeIs;

class LargeProjectTest : public ::testing::Test {
 protected:
  LargeProjectTest() { AddLargeProject(project_.get()); }

  FakeTrack* Track(std::string_view name) const {
    FakeTrack* track = project_->FindTrackByName(name);
    EXPECT_NE(track, nullptr) << name;
    return track;
  }

  // Returns the names of the tracks the sends from `name` go to, in order.
  std::vector<std::string> SendsFrom(std::string_view name) const {
    std::vector<std::string> destinations;
    for (const FakeRoute* send : project_->GetSends(Track(name))) {
      destinations.push_back(send->destination->name);
    }
    return destinations;
  }

  std::unique_ptr<FakeProject> project_ = FakeProject::Create();
};

TEST_F(LargeProjectTest, Has128Tracks) {
  EXPECT_EQ(project_->GetTrackCount(), 128);
  EXPECT_EQ(project_->GetTrack(127)->name, "T16.9");
}

TEST_F(LargeProjectTest, BusesAndThenFoldersAreAtTheTop) {
  for (const char* name : {"T1", "T4", "T5", "T16"}) {
    EXPECT_EQ(project_->GetParentTrack(Track(name)), nullptr) << name;
  }
  EXPECT_FALSE(project_->IsFolder(Track("T4")));
  EXPECT_TRUE(project_->IsFolder(Track("T5")));
}

TEST_F(LargeProjectTest, FoldersHaveAFolderInTheSecond) {
  EXPECT_EQ(project_->GetParentTrack(Track("T16.9")), Track("T16"));
  EXPECT_EQ(project_->GetParentTrack(Track("T6.1")), Track("T6"));
  EXPECT_EQ(project_->GetParentTrack(Track("T6.1.4")), Track("T6.1"));
  EXPECT_EQ(project_->GetParentTrack(Track("T6.2")), Track("T6"));
  EXPECT_FALSE(project_->IsFolder(Track("T5.1")));
}

TEST_F(LargeProjectTest, EachBusReceivesFromEveryFourthTrack) {
  for (const char* name : {"T1", "T2", "T3", "T4"}) {
    EXPECT_THAT(project_->GetReceives(Track(name)), SizeIs(31)) << name;
    EXPECT_THAT(SendsFrom(name), IsEmpty()) << name;
  }
  EXPECT_THAT(SendsFrom("T5.1"), ElementsAre("T2"));
  EXPECT_THAT(SendsFrom("T5.2"), ElementsAre("T3"));
}

TEST_F(LargeProjectTest, FoldersSendToEachTrackDirectlyInThem) {
  EXPECT_THAT(SendsFrom("T5"),
              ElementsAre("T1", "T5.1", "T5.2", "T5.3", "T5.4", "T5.5", "T5.6",
                          "T5.7", "T5.8", "T5.9"));
  EXPECT_THAT(SendsFrom("T6.1"),
              ElementsAre("T4", "T6.1.1", "T6.1.2", "T6.1.3", "T6.1.4"));
  EXPECT_THAT(project_->GetReceives(Track("T6.1.1")), SizeIs(1));
}

TEST(LargeProjectScaleTest, ScalesByItsFolders) {
  EXPECT_EQ(GetLargeProjectTrackCount(12), 128);
  for (int folders : {2, 50}) {
    std::unique_ptr<FakeProject> project = FakeProject::Create();
    AddLargeProject(project.get(), folders);
    EXPECT_EQ(project->GetTrackCount(), GetLargeProjectTrackCount(folders));
    EXPECT_THAT(project->GetReceives(project->GetTrack(0)),
                SizeIs(GetLargeProjectBusReceiveCount(folders)));
  }
}

}  // namespace
}  // namespace jpr
