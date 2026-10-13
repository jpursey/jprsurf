// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_config/default_config_test.h"

#include <string>

#include "absl/strings/ascii.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/undo.h"

namespace jpr {

DefaultConfigTest::DefaultConfigTest(bool extender)
    : xtouch_(reaper_, FakeXTouch::Type::kFull, "X-Touch") {
  if (extender) {
    xtouch_ext_.emplace(reaper_, FakeXTouch::Type::kExtender, "X-Touch-Ext");
  }
}

FakeXTouch& DefaultConfigTest::GetXTouch(int strip) {
  return xtouch_ext_.has_value() && strip < 8 ? *xtouch_ext_ : xtouch_;
}

std::string DefaultConfigTest::GetName(int strip) {
  return std::string(absl::StripTrailingAsciiWhitespace(
      GetXTouch(strip).GetScribble(GetXTouchStrip(strip), 0)));
}

std::string DefaultConfigTest::GetBottomLine(int strip) {
  return std::string(absl::StripTrailingAsciiWhitespace(
      GetXTouch(strip).GetScribble(GetXTouchStrip(strip), 1)));
}

FakeXTouch::Light DefaultConfigTest::GetLight(FakeXTouch::StripButton button,
                                              int strip) {
  return GetXTouch(strip).GetLight(button, GetXTouchStrip(strip));
}

void DefaultConfigTest::TapWith(FakeXTouch::Button modifier,
                                FakeXTouch::Button button) {
  xtouch_.Press(modifier);
  surface_->Run();
  Tap(xtouch_, button);
  xtouch_.Release(modifier);
  surface_->Run();
}

void DefaultConfigTest::PressRange(FakeXTouch::StripButton button, int first,
                                   int last) {
  GetXTouch(first).Press(button, GetXTouchStrip(first));
  surface_->Run();
  Tap(GetXTouch(last), button, GetXTouchStrip(last));
  GetXTouch(first).Release(button, GetXTouchStrip(first));
  surface_->Run();
}

void DefaultConfigTest::TapSelect(int strip) {
  Tap(GetXTouch(strip), FakeXTouch::StripButton::kSelect,
      GetXTouchStrip(strip));
}

void DefaultConfigTest::DoublePressSelect(int strip) {
  DoublePress(GetXTouch(strip), FakeXTouch::StripButton::kSelect,
              GetXTouchStrip(strip));
}

void DefaultConfigTest::SelectRange(int first, int last) {
  Hold(GetXTouch(first), FakeXTouch::StripButton::kSelect,
       GetXTouchStrip(first));
  TapSelect(last);
  GetXTouch(first).Release(FakeXTouch::StripButton::kSelect,
                           GetXTouchStrip(first));
  surface_->Run();
}

void DefaultConfigTest::EnterSendMode(int strip) {
  TapSelect(strip);
  Tap(xtouch_, FakeXTouch::Button::kAssignSend);
}

void DefaultConfigTest::PickWithSendHeld(int strip) {
  xtouch_.Press(FakeXTouch::Button::kAssignSend);
  surface_->Run();
  TapSelect(strip);
  xtouch_.Release(FakeXTouch::Button::kAssignSend);
  RunUntilShown();
}

void DefaultConfigTest::WaitForUndo() {
  surface_->RunFor(ContinuousUndo::kDelay + FakeReaper::GetRunTime());
}

}  // namespace jpr
