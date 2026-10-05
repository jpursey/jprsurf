// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>

#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/plugin/testing/surface_test.h"

namespace jpr {

//==============================================================================
// DefaultConfigTest
//
// A fixture for tests of the config PluginSurface builds: an X-Touch, with an
// X-Touch Extender to its left.
//
// Strips are numbered across the surface: 0-7 on the extender, and 8-15 on
// the X-Touch, or 0-7 on the X-Touch alone.
//==============================================================================

class DefaultConfigTest : public SurfaceTest {
 protected:
  // Where the X-Touch's faders show 0dB, give or take a position for rounding.
  static constexpr int kFader0dB = 12720;

  // The ring mode a strip shows a pan with (boost/cut), and the ring positions
  // for full left, center, and full right.
  static constexpr int kPanRing = 1;
  static constexpr int kLeftRing = 1;
  static constexpr int kCenterRing = 6;
  static constexpr int kRightRing = 11;

  // With an extender to the left of the X-Touch, unless `extender` is false,
  // for the X-Touch alone.
  explicit DefaultConfigTest(bool extender = true);

  // Returns the X-Touch `strip` is on, and its strip there.
  FakeXTouch& GetXTouch(int strip);
  static int GetXTouchStrip(int strip) { return strip % 8; }

  // Returns the name `strip` shows, and its bottom line, without the spaces
  // after them.
  std::string GetName(int strip);
  std::string GetBottomLine(int strip);

  // Returns the light of `button` on `strip`.
  FakeXTouch::Light GetLight(FakeXTouch::StripButton button, int strip);

  // Taps select on `strip`, which selects only its track.
  void TapSelect(int strip);

  // Holds select on `first`, and taps it on `last`, which selects the tracks
  // from one to the other.
  void SelectRange(int first, int last);

  std::optional<FakeXTouch> xtouch_ext_;  // Strips 0-7, if there is one.
  FakeXTouch xtouch_;                     // Strips 8-15, or 0-7 alone.
};

}  // namespace jpr
