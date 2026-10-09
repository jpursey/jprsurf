// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/testing/action_ids.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_track.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/default_config/default_config_test.h"

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
class GlobalTest : public DefaultConfigTest {
 protected:
  static constexpr int kTrackCount = 4;

  GlobalTest() {
    tracks_ = AddTracks(kTrackCount);
    AddSurface();
  }

  // Taps `button` while holding `modifier`.
  void TapWith(Button modifier, Button button) {
    xtouch_.Press(modifier);
    surface_->Run();
    Tap(xtouch_, button);
    xtouch_.Release(modifier);
    surface_->Run();
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
// Buttons that run an action
//------------------------------------------------------------------------------

struct ButtonAction {
  Button button;
  std::optional<Button> modifier;  // Held while the button is tapped.
  int action;
  std::string test_name;
};

class ButtonActionTest : public GlobalTest,
                         public WithParamInterface<ButtonAction> {};

INSTANTIATE_TEST_SUITE_P(
    Global, ButtonActionTest,
    Values(
        // Transport
        ButtonAction{Button::kStop, {}, 1016, "Stop"},
        ButtonAction{Button::kPlay, {}, 40073, "PlayPause"},
        ButtonAction{Button::kRecord, {}, 1013, "Record"},
        ButtonAction{Button::kCycle, {}, 1068, "Repeat"},
        ButtonAction{Button::kClick, {}, 40364, "Metronome"},
        ButtonAction{Button::kSolo, {}, 40745, "SoloInFront"},
        // Utility
        ButtonAction{Button::kUndo, {}, 40029, "Undo"},
        ButtonAction{Button::kUndo, Button::kShift, 40030, "ShiftRedo"},
        ButtonAction{Button::kSave, {}, 40026, "SaveProject"},
        ButtonAction{Button::kSave, Button::kShift, 41895,
                     "ShiftSaveNewVersion"},
        ButtonAction{Button::kCancel, {}, 40289, "UnselectAllItems"},
        ButtonAction{Button::kCancel, Button::kShift, 40020,
                     "ShiftRemoveTimeSelection"},
        ButtonAction{Button::kEnter, {}, 40214, "InsertMidiItem"},
        ButtonAction{Button::kEnter, Button::kShift, 40142,
                     "ShiftInsertEmptyItem"},
        ButtonAction{Button::kEnter, Button::kControl, 40013,
                     "ControlInsertClickSource"}),
    [](const TestParamInfo<ButtonAction>& info) {
      return info.param.test_name;
    });

TEST_P(ButtonActionTest, PressingRunsTheAction) {
  if (GetParam().modifier.has_value()) {
    TapWith(*GetParam().modifier, GetParam().button);
  } else {
    Tap(xtouch_, GetParam().button);
  }
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(GetParam().action));
}

// Solo has Solo in front when tapped, and unsolos every track when held.
TEST_F(GlobalTest, HoldingSoloUnsolosAllTracks) {
  LongPress(xtouch_, Button::kSolo);
  EXPECT_THAT(reaper_.GetCommandsRun(), ElementsAre(40340));
}

//------------------------------------------------------------------------------
// Modifiers
//
// Shift, Option, Control, and Alt are lit while held.
//------------------------------------------------------------------------------

struct Modifier {
  Button button;
  std::string test_name;
};

class ModifierTest : public GlobalTest, public WithParamInterface<Modifier> {};

INSTANTIATE_TEST_SUITE_P(Global, ModifierTest,
                         Values(Modifier{Button::kShift, "Shift"},
                                Modifier{Button::kOption, "Option"},
                                Modifier{Button::kControl, "Control"},
                                Modifier{Button::kAlt, "Alt"}),
                         [](const TestParamInfo<Modifier>& info) {
                           return info.param.test_name;
                         });

TEST_P(ModifierTest, LightIsLitWhileHeld) {
  EXPECT_EQ(xtouch_.GetLight(GetParam().button), Light::kOff);
  xtouch_.Press(GetParam().button);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(GetParam().button), Light::kOn);

  xtouch_.Release(GetParam().button);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(GetParam().button), Light::kOff);
}

//------------------------------------------------------------------------------
// Lights that show an action's toggle state
//------------------------------------------------------------------------------

struct ToggleLight {
  Button button;
  int action;
  std::string test_name;
};

class ToggleLightTest : public GlobalTest,
                        public WithParamInterface<ToggleLight> {};

INSTANTIATE_TEST_SUITE_P(Global, ToggleLightTest,
                         Values(ToggleLight{Button::kPlay, 1007, "Play"},
                                ToggleLight{Button::kRecord, 1013, "Record"},
                                ToggleLight{Button::kCycle, 1068, "Repeat"},
                                ToggleLight{Button::kClick, 40364, "Metronome"},
                                ToggleLight{Button::kSolo, 40745,
                                            "SoloInFront"}),
                         [](const TestParamInfo<ToggleLight>& info) {
                           return info.param.test_name;
                         });

TEST_P(ToggleLightTest, LightShowsTheAction) {
  EXPECT_EQ(xtouch_.GetLight(GetParam().button), Light::kOff);
  reaper_.SetToggleState(GetParam().action, 1);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(GetParam().button), Light::kOn);
  reaper_.SetToggleState(GetParam().action, 0);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(GetParam().button), Light::kOff);
}

//------------------------------------------------------------------------------
// Lights that show the project's state
//------------------------------------------------------------------------------

TEST_F(GlobalTest, SoloLedShowsAnyTrackSoloed) {
  EXPECT_EQ(xtouch_.GetLight(Led::kSolo), Light::kOff);
  tracks_[2]->solo = true;
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Led::kSolo), Light::kOn);
  tracks_[2]->solo = false;
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Led::kSolo), Light::kOff);
}

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

TEST_F(GlobalTest, SaveBlinksWhileTheProjectHasChanges) {
  EXPECT_EQ(xtouch_.GetLight(Button::kSave), Light::kOff);
  reaper_.GetProject().SetDirty(true);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kSave), Light::kBlinking);
  reaper_.GetProject().SetDirty(false);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kSave), Light::kOff);
}

TEST_F(GlobalTest, CancelLightShowsItemsAreSelected) {
  EXPECT_EQ(xtouch_.GetLight(Button::kCancel), Light::kOff);
  reaper_.GetProject().SetSelectedItemCount(2);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kCancel), Light::kOn);
  reaper_.GetProject().SetSelectedItemCount(0);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetLight(Button::kCancel), Light::kOff);
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

TEST_F(GlobalTest, TimecodeShowsTheEditCursor) {
  reaper_.GetProject().SetCursorPosition(3.5);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetTimecode(), "  2 4.00   ");
}

TEST_F(GlobalTest, TimecodeShowsThePlayPositionWhilePlaying) {
  reaper_.GetProject().SetCursorPosition(1.0);
  reaper_.GetProject().SetPlayPosition(3.5);
  reaper_.GetProject().SetPlayState(1);
  RunUntilShown();
  EXPECT_EQ(xtouch_.GetTimecode(), "  2 4.00   ");
}

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
      {43204, "    0.03.500", Light::kOff, Light::kOff},  // Time.
      {40370, " 00.00.03.15 ", Light::kOff, Light::kOn},  // Frames.
      {40369, "    154350", Light::kOff, Light::kOff},    // Samples.
      {40367, "  2 4.00   ", Light::kOn, Light::kOff},    // Beats.
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
  EXPECT_THAT(
      reaper_.GetCommandsRun(),
      ElementsAre(kFirstAutoModeAction + static_cast<int>(AutoMode::kWrite)));
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

TEST_F(GlobalTest, ModeLightsAreOffWithNothingSelected) {
  for (Button button :
       {Button::kAutoTrim, Button::kAutoRead, Button::kAutoTouch,
        Button::kAutoWrite, Button::kAutoLatch}) {
    EXPECT_EQ(xtouch_.GetLight(button), Light::kOff);
  }
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

TEST_F(GlobalTest, GroupTurnsTheOverrideOnAndOff) {
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoGroup), Light::kOff);
  Tap(xtouch_, Button::kAutoGroup);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kBypass));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoGroup), Light::kOn);

  Tap(xtouch_, Button::kAutoGroup);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kNone));
  EXPECT_EQ(xtouch_.GetLight(Button::kAutoGroup), Light::kOff);
}

TEST_F(GlobalTest, GroupRestoresTheLastOverride) {
  reaper_.GetProject().SetAutomationOverride(
      static_cast<int>(AutoOverride::kTouch));
  RunUntilShown();
  Tap(xtouch_, Button::kAutoGroup);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kNone));
  Tap(xtouch_, Button::kAutoGroup);
  EXPECT_EQ(reaper_.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kTouch));
}

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
