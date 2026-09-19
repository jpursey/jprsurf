// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/numbered_name.h"

#include <string_view>

#include "gtest/gtest.h"

namespace jpr {
namespace {

inline constexpr std::string_view kPrefix = "name:";
inline constexpr std::string_view kEmptyPrefix = "";

static_assert(kNumberedName<kPrefix, 0> == "name:0");
static_assert(kNumberedName<kPrefix, 7> == "name:7");
static_assert(kNumberedName<kPrefix, 10> == "name:10");
static_assert(kNumberedName<kPrefix, 40029> == "name:40029");
static_assert(kNumberedName<kPrefix, 2147483647> == "name:2147483647");
static_assert(kNumberedName<kEmptyPrefix, 123> == "123");

TEST(NumberedNameTest, MatchesAtRuntime) {
  EXPECT_EQ((kNumberedName<kPrefix, 0>), "name:0");
  EXPECT_EQ((kNumberedName<kPrefix, 40029>), "name:40029");
  EXPECT_EQ((kNumberedName<kEmptyPrefix, 123>), "123");
}

TEST(NumberedNameTest, SameNameIsSameStorage) {
  EXPECT_EQ((kNumberedName<kPrefix, 42>.data()),
            (kNumberedName<kPrefix, 42>.data()));
}

}  // namespace
}  // namespace jpr
