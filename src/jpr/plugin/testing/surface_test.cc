// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/testing/surface_test.h"

#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "jpr/common/testing/fake_project.h"
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
// The project
//------------------------------------------------------------------------------

std::vector<FakeTrack*> SurfaceTest::AddTracks(int count, FakeTrack* folder) {
  FakeProject& project = reaper_.GetProject();
  const std::string prefix =
      folder != nullptr ? absl::StrCat(folder->name, ".") : "T";
  int number = 0;
  for (int i = 0; i < project.GetTrackCount(); ++i) {
    if (project.GetParentTrack(project.GetTrack(i)) == folder) {
      ++number;
    }
  }
  std::vector<FakeTrack*> tracks;
  tracks.reserve(count);
  for (int i = 0; i < count; ++i) {
    tracks.push_back(project.AddTrack(absl::StrCat(prefix, ++number), folder));
  }
  return tracks;
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

}  // namespace jpr
