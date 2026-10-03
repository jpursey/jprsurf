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

// Returns how long Settle() runs: a run past the time a press is held back in
// case it is a double press. A button's outputs aren't held back after an
// input, as a fader's are.
absl::Duration GetSettleTime() {
  return absl::Seconds(Control::kDoublePressWindowSecs) +
         FakeReaper::GetRunTime();
}

}  // namespace

SurfaceTest::SurfaceTest(bool extender)
    : xtouch_(reaper_, FakeXTouch::Type::kFull, "X-Touch") {
  if (extender) {
    xtouch_ext_.emplace(reaper_, FakeXTouch::Type::kExtender, "X-Touch-Ext");
  }
  AddReaperActions(&reaper_);
}

SurfaceTest::~SurfaceTest() {
  surface_.reset();
  if (Plugin::GetInstance() != nullptr) {
    Plugin::Unload();
  }
}

void SurfaceTest::AddSurface() {
  ASSERT_TRUE(Plugin::Load(nullptr, reaper_.GetPluginInfo(), {}));
  surface_ = reaper_.AddSurface();
  ASSERT_NE(surface_, nullptr);
  surface_->SetTrackListChange();
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

void SurfaceTest::Settle() { surface_->RunFor(GetSettleTime()); }

}  // namespace jpr
