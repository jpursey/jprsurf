// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/spec_line.h"

#include "gtest/gtest.h"

namespace jpr {
namespace {

TEST(SpecLineTest, ErrorStartsWithTheLineThenTheText) {
  EXPECT_EQ(GetSpecError({.line = {12}, .text = "xtouch"},
                         "type \"XTouh\" isn't a device type"),
            "line 12: xtouch: type \"XTouh\" isn't a device type");
}

TEST(SpecLineTest, ErrorLeavesOutWhatIsMissing) {
  EXPECT_EQ(GetSpecError({.text = "xtouch"}, "is required"),
            "xtouch: is required");
  EXPECT_EQ(GetSpecError({.line = {12}}, "device \"xtouch\" is repeated"),
            "line 12: device \"xtouch\" is repeated");
  EXPECT_EQ(GetSpecError({}, "device \"xtouch\" is repeated"),
            "device \"xtouch\" is repeated");
}

TEST(SpecLineTest, LinesCompareEqual) {
  EXPECT_EQ(SpecLine{1}, SpecLine{2});
  EXPECT_EQ(SpecLine{1}, SpecLine{});
}

}  // namespace
}  // namespace jpr
