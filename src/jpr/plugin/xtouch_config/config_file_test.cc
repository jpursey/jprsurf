// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/config/surface_config.h"
#include "jpr/plugin/xtouch_config/xtouch_config_test.h"
#include "jpr/scene/surface_spec.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;

// The surface tests (see XTouchConfigTest) check what the config does. This
// checks it on its own, so an error in it is reported once, here.
TEST(ConfigFileTest, ReadsAndIsValid) {
  std::ifstream file(GetConfigFilePath(), std::ios::binary);
  const std::string text{std::istreambuf_iterator<char>(file),
                         std::istreambuf_iterator<char>()};
  ASSERT_FALSE(text.empty()) << GetConfigFilePath() << " can't be read";

  std::vector<std::string> errors;
  const std::optional<SurfaceSpec> spec = ReadSurfaceConfig(text, &errors);
  EXPECT_THAT(errors, IsEmpty());
  ASSERT_TRUE(spec.has_value());
  EXPECT_TRUE(ValidateSurfaceSpec(*spec, &errors));
  EXPECT_THAT(errors, IsEmpty());
}

}  // namespace
}  // namespace jpr
