// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_api.h"

#include <string_view>

#include "gtest/gtest.h"

namespace jpr {
namespace {

// Stands in for every REAPER function. It is never called.
void Function() {}

void* GetFunction(const char* name) {
  return reinterpret_cast<void*>(&Function);
}

void* GetFunctionExceptGetTrack(const char* name) {
  if (std::string_view(name) == "GetTrack") {
    return nullptr;
  }
  return GetFunction(name);
}

// This also fails if a REAPERAPI_WANT_ line is missing from JPR_REAPER_API.
TEST(ReaperApiTest, LoadsEveryListedFunction) {
  EXPECT_TRUE(LoadReaperApi(&GetFunction));
#define JPR_EXPECT_LOADED(name)                 \
  EXPECT_EQ(reinterpret_cast<void*>(::name),    \
            reinterpret_cast<void*>(&Function)) \
      << #name;
  JPR_REAPER_API(JPR_EXPECT_LOADED)
#undef JPR_EXPECT_LOADED
}

TEST(ReaperApiTest, FailsIfAFunctionIsMissing) {
  EXPECT_FALSE(LoadReaperApi(&GetFunctionExceptGetTrack));
  EXPECT_EQ(::GetTrack, nullptr);
  EXPECT_NE(::CountTracks, nullptr);
}

}  // namespace
}  // namespace jpr
