// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/default_spec.h"

#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/scene/surface_spec.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;

TEST(DefaultSpecTest, IsValid) {
  std::vector<std::string> errors;
  EXPECT_TRUE(ValidateSurfaceSpec(GetDefaultSpec(), &errors));
  EXPECT_THAT(errors, IsEmpty());
}

}  // namespace
}  // namespace jpr
