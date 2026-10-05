// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_config/default_config_test.h"

#include <string>

#include "absl/strings/ascii.h"

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

void DefaultConfigTest::TapSelect(int strip) {
  Tap(GetXTouch(strip), FakeXTouch::StripButton::kSelect,
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

}  // namespace jpr
