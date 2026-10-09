// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/undo.h"

#include "absl/time/time.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/testing/fake_track.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::IsEmpty;

// Drums, with a send to Bus, whose volume and pan the tests change after each
// OnChange(), as Track does. REAPER only adds an undo point for a change.
class ContinuousUndoTest : public ::testing::Test {
 protected:
  ContinuousUndoTest() {
    FakeTrack* drums = project_.AddTrack("Drums");
    project_.AddSend(drums, project_.AddTrack("Bus"));
    drums_ = ToMediaTrack(drums);
  }

  // Advances the fake's clock by `duration`, and starts a run then.
  void RunAfter(absl::Duration duration) {
    reaper_.AdvanceTime(duration);
    undo_.Update(RunTime::Now());
  }

  void SetSendVolume(double volume) {
    SetTrackSendUIVol(drums_, 0, volume, /*isend=*/0);
  }
  void SetSendPan(double pan) {
    SetTrackSendUIPan(drums_, 0, pan, /*isend=*/0);
  }

  FakeReaper reaper_;
  FakeProject& project_ = reaper_.GetProject();
  ContinuousUndo& undo_ = ContinuousUndo::Get();
  MediaTrack* drums_ = nullptr;
};

TEST_F(ContinuousUndoTest, AddsOneUndoPointOnceChangesStop) {
  undo_.Update(RunTime::Now());
  undo_.OnChange("Adjust send volume", UNDO_STATE_TRACKCFG);
  SetSendVolume(0.5);
  RunAfter(absl::Milliseconds(400));
  undo_.OnChange("Adjust send volume", UNDO_STATE_TRACKCFG);
  SetSendVolume(0.25);

  // The last change was less than kDelay ago.
  RunAfter(absl::Milliseconds(400));
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());

  RunAfter(ContinuousUndo::kDelay - absl::Milliseconds(400));
  EXPECT_THAT(
      project_.GetUndoPoints(),
      ElementsAre(AllOf(Field(&FakeUndoPoint::name, "Adjust send volume"),
                        Field(&FakeUndoPoint::flags, UNDO_STATE_TRACKCFG))));

  // Nothing is pending after it.
  RunAfter(ContinuousUndo::kDelay);
  EXPECT_EQ(project_.GetUndoPoints().size(), 1);
}

TEST_F(ContinuousUndoTest, ADifferentChangeAddsThePendingUndoPointFirst) {
  undo_.Update(RunTime::Now());
  undo_.OnChange("Adjust send volume", UNDO_STATE_TRACKCFG);
  SetSendVolume(0.5);
  undo_.OnChange("Adjust send pan", UNDO_STATE_TRACKCFG);
  SetSendPan(0.5);
  EXPECT_THAT(project_.GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, "Adjust send volume")));

  undo_.OnChange("Adjust send pan", UNDO_STATE_ALL);
  SetSendPan(0.25);
  EXPECT_THAT(project_.GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, "Adjust send volume"),
                          Field(&FakeUndoPoint::name, "Adjust send pan")));
}

TEST_F(ContinuousUndoTest, FlushAddsThePendingUndoPointNow) {
  undo_.Flush();
  EXPECT_THAT(project_.GetUndoPoints(), IsEmpty());

  undo_.Update(RunTime::Now());
  undo_.OnChange("Adjust send volume", UNDO_STATE_TRACKCFG);
  SetSendVolume(0.5);
  undo_.Flush();
  EXPECT_THAT(project_.GetUndoPoints(),
              ElementsAre(Field(&FakeUndoPoint::name, "Adjust send volume")));

  RunAfter(ContinuousUndo::kDelay);
  EXPECT_EQ(project_.GetUndoPoints().size(), 1);
}

}  // namespace
}  // namespace jpr
