// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/color.h"

#include <optional>

#include "gtest/gtest.h"

namespace jpr {
namespace {

TEST(ColorTest, FormatColor) {
  EXPECT_EQ(FormatColor({0, 0, 0}), "#000000");
  EXPECT_EQ(FormatColor({255, 128, 1}), "#ff8001");
}

TEST(ColorTest, ParseColor) {
  EXPECT_EQ(ParseColor("#000000"), (Color{0, 0, 0}));
  EXPECT_EQ(ParseColor("#ff8001"), (Color{255, 128, 1}));
  EXPECT_EQ(ParseColor("#FF8001"), (Color{255, 128, 1}));
}

TEST(ColorTest, ParseColorRejectsOtherText) {
  EXPECT_EQ(ParseColor(""), std::nullopt);
  EXPECT_EQ(ParseColor("#"), std::nullopt);
  EXPECT_EQ(ParseColor("ff8001"), std::nullopt);
  EXPECT_EQ(ParseColor("#ff800"), std::nullopt);
  EXPECT_EQ(ParseColor("#ff80011"), std::nullopt);
  EXPECT_EQ(ParseColor("#ff80zz"), std::nullopt);
  EXPECT_EQ(ParseColor("#0xff80"), std::nullopt);
  EXPECT_EQ(ParseColor("#+ff800"), std::nullopt);
  EXPECT_EQ(ParseColor("#-ff800"), std::nullopt);
  EXPECT_EQ(ParseColor("# ff800"), std::nullopt);
}

}  // namespace
}  // namespace jpr
