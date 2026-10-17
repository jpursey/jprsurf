// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/testing/surface_test.h"

#include "absl/time/time.h"
#include "jpr/common/testing/reaper_actions.h"
#include "jpr/device/control.h"
#include "jpr/plugin/plugin.h"

namespace jpr {

namespace {

// Returns how long LongPress() holds a button: a run past a long press.
absl::Duration GetLongPressTime() {
  return absl::Seconds(Control::kLongPressDurationSecs) +
         FakeReaper::GetRunTime();
}

}  // namespace

SurfaceTest::SurfaceTest() { AddReaperActions(&reaper_); }

void SurfaceTest::TearDown() { RemoveSurface(); }

void SurfaceTest::AddSurface() {
  ASSERT_TRUE(Plugin::Load(nullptr, reaper_.GetPluginInfo(), {}));
  surface_ = reaper_.AddSurface();
  ASSERT_NE(surface_, nullptr);
  surface_->SetTrackListChange();
  RunUntilShown();
}

void SurfaceTest::RemoveSurface() {
  surface_.reset();
  if (Plugin::GetInstance() != nullptr) {
    Plugin::Unload();
  }
}

void SurfaceTest::RunUntilShown() {
  surface_->Run();
  surface_->Run();
}

//------------------------------------------------------------------------------
// Presses
//------------------------------------------------------------------------------

void SurfaceTest::Tap(FakeXTouch& xtouch, FakeXTouch::Button button) {
  xtouch.Press(button);
  surface_->Run();
  xtouch.Release(button);
  Settle();
}

void SurfaceTest::Tap(FakeXTouch& xtouch, FakeXTouch::StripButton button,
                      int strip) {
  xtouch.Press(button, strip);
  surface_->Run();
  xtouch.Release(button, strip);
  Settle();
}

void SurfaceTest::DoublePress(FakeXTouch& xtouch,
                              FakeXTouch::StripButton button, int strip) {
  xtouch.Press(button, strip);
  surface_->Run();
  xtouch.Release(button, strip);
  surface_->Run();
  Tap(xtouch, button, strip);
}

void SurfaceTest::LongPress(FakeXTouch& xtouch, FakeXTouch::Button button) {
  xtouch.Press(button);
  surface_->RunFor(GetLongPressTime());
  xtouch.Release(button);
  Settle();
}

void SurfaceTest::Hold(FakeXTouch& xtouch, FakeXTouch::StripButton button,
                       int strip) {
  xtouch.Press(button, strip);
  surface_->RunFor(GetLongPressTime());
}

void SurfaceTest::Settle() {
  // A press held back in case it is a double press is acted on in the last run
  // of the double press window.
  surface_->RunFor(absl::Seconds(Control::kDoublePressWindowSecs));
  RunUntilShown();
}

//------------------------------------------------------------------------------
// Moves
//------------------------------------------------------------------------------

void SurfaceTest::MoveFader(FakeXTouch& xtouch, int fader, int position) {
  xtouch.TouchFader(fader);
  xtouch.MoveFader(fader, position);
  surface_->Run();
  xtouch.ReleaseFader(fader);
  RunUntilShown();
}

bool SurfaceTest::IsTouched(FakeTrack* track, TrackTouchType type) {
  return surface_->GetTouchState(ToMediaTrack(track),
                                 type == TrackTouchType::kPan ? 1 : 0);
}

void SurfaceTest::RunPlaying() {
  FakeProject& project = reaper_.GetProject();
  project.SetPlayPosition(project.GetPlayPosition() +
                          absl::ToDoubleSeconds(FakeReaper::GetRunTime()));
  surface_->Run();
}

void SurfaceTest::AskEveryTouch() {
  // The questions go to the surface itself, rather than through surface_,
  // which would end an entry point after each, so a benchmark times only the
  // answers. A question changes nothing, so it has no entry point to check.
  IReaperControlSurface* surface = surface_->GetWrapped();
  FakeProject& project = reaper_.GetProject();
  surface->GetTouchState(ToMediaTrack(project.GetMasterTrack()), 0);
  surface->GetTouchState(ToMediaTrack(project.GetMasterTrack()), 1);
  for (int i = 0; i < project.GetTrackCount(); ++i) {
    surface->GetTouchState(ToMediaTrack(project.GetTrack(i)), 0);
    surface->GetTouchState(ToMediaTrack(project.GetTrack(i)), 1);
  }
}

}  // namespace jpr
