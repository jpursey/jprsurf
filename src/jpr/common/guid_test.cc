// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/guid.h"

#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"
#include "sdk/reaper_plugin.h"

namespace jpr {
namespace {

// Returns a GUID with every field set.
GUID MakeTestGuid() {
  GUID guid = {};
  guid.Data1 = 0x01234567;
  guid.Data2 = 0x89AB;
  guid.Data3 = 0xCDEF;
  for (int i = 0; i < 8; ++i) {
    guid.Data4[i] = static_cast<unsigned char>(0x10 * i + 1);
  }
  return guid;
}

TEST(GuidTest, FormatsAsREAPERDoes) {
  EXPECT_EQ(FormatGuid(MakeTestGuid()),
            "{01234567-89AB-CDEF-0111-213141516171}");
}

TEST(GuidTest, RoundTripsThroughREAPERsText) {
  FakeReaper reaper;
  const GUID guid = MakeTestGuid();
  const Guid text(guid);
  EXPECT_FALSE(text.IsEmpty());
  EXPECT_TRUE(text.ToGUID() == guid);
  EXPECT_EQ(Guid(&guid), text);
}

TEST(GuidTest, NullIsEmpty) {
  FakeReaper reaper;
  const Guid guid(nullptr);
  EXPECT_TRUE(guid.IsEmpty());
  EXPECT_TRUE(guid.ToGUID() == GUID{});
  EXPECT_EQ(guid, Guid());
}

TEST(GuidTest, ComparesByValue) {
  FakeReaper reaper;
  GUID other = MakeTestGuid();
  other.Data1 = 1;
  EXPECT_NE(Guid(MakeTestGuid()), Guid(other));
  EXPECT_EQ(Guid(MakeTestGuid()), Guid(MakeTestGuid()));
}

}  // namespace
}  // namespace jpr
