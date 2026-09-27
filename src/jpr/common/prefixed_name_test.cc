// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/prefixed_name.h"

#include <string_view>

#include "gtest/gtest.h"

namespace jpr {
namespace {

inline constexpr std::string_view kPrefix = "name:";
inline constexpr std::string_view kEmptyPrefix = "";

static_assert(StringLiteral("mute").View() == "mute");
static_assert(StringLiteral("").View().empty());

static_assert(kPrefixedName<kPrefix, "mute"> == "name:mute");
static_assert(kPrefixedName<kPrefix, "can_redo"> == "name:can_redo");
static_assert(kPrefixedName<kPrefix, ""> == "name:");
static_assert(kPrefixedName<kEmptyPrefix, "mute"> == "mute");
static_assert(kPrefixedName<kEmptyPrefix, ""> == "");

TEST(PrefixedNameTest, MatchesAtRuntime) {
  EXPECT_EQ((kPrefixedName<kPrefix, "mute">), "name:mute");
  EXPECT_EQ((kPrefixedName<kEmptyPrefix, "mute">), "mute");
}

TEST(PrefixedNameTest, SameNameIsSameStorage) {
  EXPECT_EQ((kPrefixedName<kPrefix, "mute">.data()),
            (kPrefixedName<kPrefix, "mute">.data()));
}

}  // namespace
}  // namespace jpr
