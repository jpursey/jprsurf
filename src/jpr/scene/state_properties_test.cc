// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/state_properties.h"

#include "gtest/gtest.h"
#include "jpr/common/automation.h"
#include "jpr/common/testing/fake_project.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

TEST(StatePropertiesTest, TurningTheOverrideOnRestoresBypassWithEachFake) {
  {
    FakeReaper reaper;
    reaper.GetProject().SetAutomationOverride(
        static_cast<int>(AutoOverride::kWrite));
    Scene scene("Scene");

    // Seeing an override on keeps it, for turning the override on again.
    ViewProperty* active = scene.GetProperty(kStateAutoOverrideActive);
    ASSERT_NE(active, nullptr);
    EXPECT_TRUE(active->GetBool());
  }
  FakeReaper reaper;
  Scene scene("Scene");
  ViewProperty* active = scene.GetProperty(kStateAutoOverrideActive);
  ASSERT_NE(active, nullptr);
  active->SetBool(true);
  EXPECT_EQ(reaper.GetProject().GetAutomationOverride(),
            static_cast<int>(AutoOverride::kBypass));
}

}  // namespace
}  // namespace jpr
