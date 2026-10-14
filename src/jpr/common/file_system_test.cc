// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/file_system.h"

#include <memory>

#include "gb/file/memory_file_protocol.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/testing/fake_reaper.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;

TEST(FileSystemTest, TheFakeResetsItsProtocols) {
  {
    FakeReaper reaper;
    ASSERT_TRUE(GetFileSystem()->Register(
        std::make_unique<gb::MemoryFileProtocol>(), "profile"));
  }
  EXPECT_THAT(GetFileSystem()->GetProtocolNames(), IsEmpty());
}

}  // namespace
}  // namespace jpr
