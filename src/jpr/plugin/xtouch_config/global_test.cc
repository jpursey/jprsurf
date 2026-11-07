// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/action_ids.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/xtouch_config/xtouch_config_test.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::TestParamInfo;
using ::testing::Values;
using ::testing::WithParamInterface;

using Button = FakeXTouch::Button;
using Led = FakeXTouch::Led;
using Light = FakeXTouch::Light;
using StripButton = FakeXTouch::StripButton;

// Four tracks, on the extender's first strips, for the automation modes. The
// global controls are all on the X-Touch.
class GlobalTest : public XTouchConfigTest {
 protected:
  static constexpr int kTrackCount = 4;

  GlobalTest() {
    tracks_ = AddTracks(kTrackCount);
    AddSurface();
  }

  // Sets the automation mode of the track on `strip`, by selecting only it,
  // and tapping the mode's button.
  void SetMode(int strip, Button mode) {
    TapSelect(strip);
    Tap(xtouch_, mode);
  }

  std::vector<FakeTrack*> tracks_;
};

//------------------------------------------------------------------------------
// Modifiers
//
// Holding Shift or Control picks a button's other action.
//------------------------------------------------------------------------------

struct ModifiedAction {
  Button modifier;  // Held while the button is tapped.
  Button button;
  int action;
  std::string test_name;
};

class ModifiedActionTest : public GlobalTest,
                           public WithParamInterface<ModifiedAction> {};

INSTANTIATE_TEST_SUITE_P(
    Global, ModifiedActionTest,
    Values(ModifiedAction{Button::kShift, Button::kSave, 41895,
                          "ShiftSaveNewVersion"},
           ModifiedAction{Button::kShift, Button::kCancel, 40020,
                          "ShiftRemoveTimeSelection"},
           ModifiedAction{Button::kShift, Button::kEnter, 40142,
                          "ShiftInsertEmptyItem"},
           ModifiedAction{Button::kControl, Button::kEnter, 40013,
                          "ControlInsertClickSource"}),
    [](const TestParamInfo<ModifiedAction>& info) {
      return info.param.test_name;
    });

TEST_P(ModifiedActionTest, PressingRunsTheAction) {
  TapWith(GetParam().modifier, GetParam().button);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(GetParam().action));
}

//------------------------------------------------------------------------------
// Lights that show the project's state
//------------------------------------------------------------------------------

TEST_F(GlobalTest, UndoLightShowsThereIsARedo) {
  Tap(*xtouch_ext_, StripButton::kMute, 0);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kUndo), Light::kOff);
  Tap(xtouch_, Button::kUndo);
  RunUntilShown();
  EXPECT_FALSE(tracks_[0]->mute);
  EXPECT_EQ(xtouch_.GetLight(Button::kUndo), Light::kOn);
  TapWith(Button::kShift, Button::kUndo);
  RunUntilShown();
  EXPECT_TRUE(tracks_[0]->mute);
  EXPECT_EQ(xtouch_.GetLight(Button::kUndo), Light::kOff);
}

//------------------------------------------------------------------------------
// Rewind and Forward
//
// They move by a measure, or by the step Nudge or Marker picks, whose light is
// lit while it is picked.
//------------------------------------------------------------------------------

TEST_F(GlobalTest, RewindAndForwardMoveByMeasure) {
  Tap(xtouch_, Button::kRewind);
  Tap(xtouch_, Button::kForward);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(41041, 41040));
  EXPECT_EQ(xtouch_.GetLight(Button::kNudge), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Button::kMarker), Light::kOff);
}

TEST_F(GlobalTest, NudgeMovesByBeat) {
  Tap(xtouch_, Button::kNudge);
  EXPECT_EQ(xtouch_.GetLight(Button::kNudge), Light::kOn);
  Tap(xtouch_, Button::kRewind);
  Tap(xtouch_, Button::kForward);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(40230, 40231));
}

TEST_F(GlobalTest, MarkerMovesByMarker) {
  Tap(xtouch_, Button::kMarker);
  EXPECT_EQ(xtouch_.GetLight(Button::kMarker), Light::kOn);
  Tap(xtouch_, Button::kRewind);
  Tap(xtouch_, Button::kForward);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(40172, 40173));
}

TEST_F(GlobalTest, MarkerTurnsNudgeOff) {
  Tap(xtouch_, Button::kNudge);
  Tap(xtouch_, Button::kMarker);
  EXPECT_EQ(xtouch_.GetLight(Button::kNudge), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Button::kMarker), Light::kOn);
  Tap(xtouch_, Button::kForward);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(40173));
}

TEST_F(GlobalTest, TurningTheStepOffMovesByMeasure) {
  Tap(xtouch_, Button::kMarker);
  Tap(xtouch_, Button::kMarker);
  EXPECT_EQ(xtouch_.GetLight(Button::kMarker), Light::kOff);
  Tap(xtouch_, Button::kForward);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(41040));
}

//------------------------------------------------------------------------------
// Timecode
//
// The fake's project is at 120 BPM in 4/4, with 30 frames a second, so 3.5
// seconds is the fourth beat of the second measure, and frame 15. The ruler
// starts in Measure.Beats.
//------------------------------------------------------------------------------

// The ruler steps through beats, time, frames, and samples, and back, each
// shown as the ruler shows it, with the Beats or SMPTE light for its mode.
TEST_F(GlobalTest, TimeBeatsStepsThroughTheRulerModes) {
  struct RulerMode {
    int action;
    std::string timecode;
    Light beats;
    Light smpte;
  };
  const RulerMode kModes[] = {
      {kRulerMinutesSecondsMinimal, "    0.03.500", Light::kOff, Light::kOff},
      {kRulerFrames, " 00.00.03.15 ", Light::kOff, Light::kOn},
      {kRulerSamples, "    154350", Light::kOff, Light::kOff},
      {kRulerMeasuresBeats, "  2 4.00   ", Light::kOn, Light::kOff},
  };

  EXPECT_EQ(xtouch_.GetLight(Led::kBeats), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Led::kSmpte), Light::kOff);
  reaper_.GetProject().SetCursorPosition(3.5);

  for (const RulerMode& mode : kModes) {
    SCOPED_TRACE(mode.action);
    Tap(xtouch_, Button::kShowTimeBeats);
    EXPECT_EQ(xtouch_.GetTimecode(), mode.timecode);
    EXPECT_EQ(xtouch_.GetLight(Led::kBeats), mode.beats);
    EXPECT_EQ(xtouch_.GetLight(Led::kSmpte), mode.smpte);
  }
  EXPECT_THAT(reaper_.GetCommandsRun(),
              ElementsAre(kModes[0].action, kModes[1].action, kModes[2].action,
                          kModes[3].action));
}

//------------------------------------------------------------------------------
// Automation modes
//
// Without an override, each mode button sets the selected tracks' mode, and
// is lit while any of them is in it: solid if they all are, and blinking if
// only some are.
//
// The surface reads the selected tracks' modes again only when REAPER tells it
// they changed, so tests select tracks and set their modes as the user does,
// rather than by setting the fake's tracks.
//------------------------------------------------------------------------------

TEST_F(GlobalTest, ModeButtonSetsTheSelectedTracks) {
  SelectRange(1, 2);
  Tap(xtouch_, Button::kAutoWrite);
  EXPECT_THAT(reaper_.GetCommandsRun(),
              ElementsAre(GetAutoModeAction(AutoMode::kWrite)));
  EXPECT_EQ(tracks_[0]->auto_mode, static_cast<int>(AutoMode::kTrimRead));
  EXPECT_EQ(tracks_[1]->auto_mode, static_cast<int>(AutoMode::kWrite));
  EXPECT_EQ(tracks_[2]->auto_mode, static_cast<int>(AutoMode::kWrite));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoWrite), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoTrim), Light::kOff);
}

TEST_F(GlobalTest, ModeLightShowsTheSelectedTracks) {
  SetMode(1, Button::kAutoTouch);
  SetMode(0, Button::kAutoRead);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoRead), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoTouch), Light::kOff);

  TapSelect(1);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoRead), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoTouch), Light::kOn);
}

TEST_F(GlobalTest, ModeLightsBlinkForSomeOfTheSelectedTracks) {
  SetMode(0, Button::kAutoRead);
  SetMode(1, Button::kAutoLatch);
  SelectRange(0, 1);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoRead), Light::kBlinking);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoLatch), Light::kBlinking);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoTrim), Light::kOff);
}

// The master has no strip, so it is selected through REAPER's API, which tells
// the surface.
TEST_F(GlobalTest, ModeButtonSetsTheSelectedMaster) {
  FakeTrack* const master = reaper_.GetProject().GetMasterTrack();
  SetTrackSelected(ToMediaTrack(master), true);
  Tap(xtouch_, Button::kAutoTouch);
  EXPECT_EQ(master->auto_mode, static_cast<int>(AutoMode::kTouch));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoTouch), Light::kOn);
}

//------------------------------------------------------------------------------
// The global automation override
//
// Group turns the override on and off, and is lit while it is on. During an
// override, each mode button sets the override to its mode, or to Bypass if
// it already is, and is lit while the override is its mode. Latch blinks for
// Latch Preview, which has no button.
//------------------------------------------------------------------------------

TEST_F(GlobalTest, ModeButtonSetsTheOverride) {
  TapSelect(0);
  Tap(xtouch_, Button::kAutoGroup);
  Tap(xtouch_, Button::kAutoWrite);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kWrite));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoWrite), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoGroup), Light::kOn);
  EXPECT_THAT(reaper_.GetCommandsRun(), IsEmpty());
  EXPECT_EQ(tracks_[0]->auto_mode, static_cast<int>(AutoMode::kTrimRead));

  Tap(xtouch_, Button::kAutoWrite);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kBypass));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoWrite), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoGroup), Light::kOn);
}

TEST_F(GlobalTest, OverrideLightsIgnoreTheSelectedTracks) {
  SetMode(0, Button::kAutoRead);
  reaper_.GetProject().SetAutomationOverride(
      static_cast<int>(AutoOverride::kWrite));
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoRead), Light::kOff);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoWrite), Light::kOn);
}

TEST_F(GlobalTest, LatchBlinksForLatchPreview) {
  reaper_.GetProject().SetAutomationOverride(
      static_cast<int>(AutoOverride::kLatchPreview));
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoLatch), Light::kBlinking);
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoGroup), Light::kOn);

  Tap(xtouch_, Button::kAutoLatch);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kLatch));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoLatch), Light::kOn);
}

}  // namespace
}  // namespace jpr
