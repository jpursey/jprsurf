// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/xtouch_config/xtouch_config_test.h"

#include <string>

#include "absl/strings/ascii.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/common/undo.h"

namespace jpr {

const char* GetConfigFilePath() { return JPR_CONFIG_FILE; }

XTouchConfigTest::XTouchConfigTest(bool extender)
    : SurfaceTest(GetConfigFilePath()),
      xtouch_(reaper_, FakeXTouch::Type::kFull, "X-Touch") {
  if (extender) {
    xtouch_ext_.emplace(reaper_, FakeXTouch::Type::kExtender, "X-Touch-Ext");
  }
}

FakeXTouch& XTouchConfigTest::GetXTouch(int strip) {
  return xtouch_ext_.has_value() && strip < 8 ? *xtouch_ext_ : xtouch_;
}

std::string XTouchConfigTest::GetName(int strip) {
  return std::string(absl::StripTrailingAsciiWhitespace(
      GetXTouch(strip).GetScribble(GetXTouchStrip(strip), 0)));
}

std::string XTouchConfigTest::GetBottomLine(int strip) {
  return std::string(absl::StripTrailingAsciiWhitespace(
      GetXTouch(strip).GetScribble(GetXTouchStrip(strip), 1)));
}

FakeXTouch::Light XTouchConfigTest::GetLight(FakeXTouch::StripButton button,
                                             int strip) {
  return GetXTouch(strip).GetLight(button, GetXTouchStrip(strip));
}

void XTouchConfigTest::TapWith(FakeXTouch::Button modifier,
                               FakeXTouch::Button button) {
  xtouch_.Press(modifier);
  surface_->Run();
  Tap(xtouch_, button);
  xtouch_.Release(modifier);
  surface_->Run();
}

void XTouchConfigTest::PressRange(FakeXTouch::StripButton button, int first,
                                  int last) {
  GetXTouch(first).Press(button, GetXTouchStrip(first));
  surface_->Run();
  Tap(GetXTouch(last), button, GetXTouchStrip(last));
  GetXTouch(first).Release(button, GetXTouchStrip(first));
  surface_->Run();
}

void XTouchConfigTest::TapSelect(int strip) {
  Tap(GetXTouch(strip), FakeXTouch::StripButton::kSelect,
      GetXTouchStrip(strip));
}

void XTouchConfigTest::DoublePressSelect(int strip) {
  DoublePress(GetXTouch(strip), FakeXTouch::StripButton::kSelect,
              GetXTouchStrip(strip));
}

void XTouchConfigTest::SelectRange(int first, int last) {
  Hold(GetXTouch(first), FakeXTouch::StripButton::kSelect,
       GetXTouchStrip(first));
  TapSelect(last);
  GetXTouch(first).Release(FakeXTouch::StripButton::kSelect,
                           GetXTouchStrip(first));
  surface_->Run();
}

void XTouchConfigTest::EnterSendMode(int strip) {
  TapSelect(strip);
  Tap(xtouch_, FakeXTouch::Button::kAssignSend);
}

void XTouchConfigTest::PickWithSendHeld(int strip) {
  xtouch_.Press(FakeXTouch::Button::kAssignSend);
  surface_->Run();
  TapSelect(strip);
  xtouch_.Release(FakeXTouch::Button::kAssignSend);
  RunUntilShown();
}

void XTouchConfigTest::WaitForUndo() {
  surface_->RunFor(ContinuousUndo::kDelay + FakeReaper::GetRunTime());
}

}  // namespace jpr
