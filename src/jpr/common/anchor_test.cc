// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/anchor.h"

#include <utility>

#include "gtest/gtest.h"

namespace jpr {
namespace {

constexpr Modifiers kAnchorModifier = kModUserStart;
constexpr Modifiers kOtherModifier = kModUserStart << 1;

class AnchorTest : public ::testing::Test {
 protected:
  void SetUp() override { ResetModifiers(); }
  void TearDown() override { ResetModifiers(); }

  int a_ = 0;
  int b_ = 0;
};

TEST_F(AnchorTest, StartsUnheld) {
  Anchor<int> anchor;
  EXPECT_FALSE(anchor.IsHeld());
  EXPECT_EQ(anchor.Get(), nullptr);
}

TEST_F(AnchorTest, HoldAndRelease) {
  Anchor<int> anchor;
  AnchorHold hold = anchor.Hold(&a_);
  EXPECT_TRUE(hold.IsHeld());
  EXPECT_EQ(hold.GetAnchor(), &anchor);
  EXPECT_TRUE(anchor.IsHeld());
  EXPECT_EQ(anchor.Get(), &a_);

  hold.Reset();
  EXPECT_FALSE(hold.IsHeld());
  EXPECT_EQ(hold.GetAnchor(), nullptr);
  EXPECT_FALSE(anchor.IsHeld());
  EXPECT_EQ(anchor.Get(), nullptr);
}

TEST_F(AnchorTest, DestroyReleases) {
  Anchor<int> anchor;
  {
    AnchorHold hold = anchor.Hold(&a_);
    EXPECT_EQ(anchor.Get(), &a_);
  }
  EXPECT_EQ(anchor.Get(), nullptr);
}

TEST_F(AnchorTest, HoldNullIsEmpty) {
  Anchor<int> anchor;
  AnchorHold hold = anchor.Hold(nullptr);
  EXPECT_FALSE(hold.IsHeld());
  EXPECT_EQ(hold.GetAnchor(), nullptr);
  EXPECT_FALSE(anchor.IsHeld());
}

TEST_F(AnchorTest, FirstHoldWins) {
  Anchor<int> anchor;
  AnchorHold hold_a = anchor.Hold(&a_);
  AnchorHold hold_b = anchor.Hold(&b_);
  EXPECT_TRUE(hold_a.IsHeld());
  EXPECT_FALSE(hold_b.IsHeld());
  EXPECT_EQ(hold_b.GetAnchor(), nullptr);
  EXPECT_EQ(anchor.Get(), &a_);

  // Releasing the empty hold does not affect the anchor.
  hold_b.Reset();
  EXPECT_EQ(anchor.Get(), &a_);
}

TEST_F(AnchorTest, ClearEmptiesHold) {
  Anchor<int> anchor;
  AnchorHold hold = anchor.Hold(&a_);
  anchor.Clear();
  EXPECT_FALSE(anchor.IsHeld());
  EXPECT_FALSE(hold.IsHeld());
  EXPECT_EQ(hold.GetAnchor(), nullptr);

  hold.Reset();
  EXPECT_FALSE(anchor.IsHeld());
}

TEST_F(AnchorTest, DestroyAnchorEmptiesHold) {
  AnchorHold hold;
  {
    Anchor<int> anchor;
    anchor.SetModifier(kAnchorModifier);
    hold = anchor.Hold(&a_);
    EXPECT_TRUE(hold.IsHeld());
  }
  EXPECT_FALSE(hold.IsHeld());
  EXPECT_EQ(hold.GetAnchor(), nullptr);
  EXPECT_TRUE(AreModifiersOff(kAnchorModifier));
}

TEST_F(AnchorTest, StaleHoldDoesNotReleaseNewHold) {
  Anchor<int> anchor;
  AnchorHold hold_a = anchor.Hold(&a_);
  anchor.Clear();
  AnchorHold hold_b = anchor.Hold(&b_);
  EXPECT_FALSE(hold_a.IsHeld());
  EXPECT_TRUE(hold_b.IsHeld());

  hold_a.Reset();
  EXPECT_TRUE(hold_b.IsHeld());
  EXPECT_EQ(anchor.Get(), &b_);
}

TEST_F(AnchorTest, MoveConstruct) {
  Anchor<int> anchor;
  AnchorHold hold = anchor.Hold(&a_);
  AnchorHold moved(std::move(hold));
  EXPECT_FALSE(hold.IsHeld());
  EXPECT_EQ(hold.GetAnchor(), nullptr);
  EXPECT_TRUE(moved.IsHeld());

  hold.Reset();
  EXPECT_EQ(anchor.Get(), &a_);
  moved.Reset();
  EXPECT_EQ(anchor.Get(), nullptr);
}

TEST_F(AnchorTest, MoveAssignReleasesPrevious) {
  Anchor<int> anchor_a;
  Anchor<int> anchor_b;
  AnchorHold hold = anchor_a.Hold(&a_);
  hold = anchor_b.Hold(&b_);
  EXPECT_FALSE(anchor_a.IsHeld());
  EXPECT_EQ(anchor_b.Get(), &b_);
  EXPECT_EQ(hold.GetAnchor(), &anchor_b);
}

TEST_F(AnchorTest, ModifierOnWhileHeld) {
  Anchor<int> anchor;
  anchor.SetModifier(kAnchorModifier);
  EXPECT_TRUE(AreModifiersOff(kAnchorModifier));

  AnchorHold hold = anchor.Hold(&a_);
  EXPECT_TRUE(AreModifiersOn(kAnchorModifier));
  hold.Reset();
  EXPECT_TRUE(AreModifiersOff(kAnchorModifier));

  hold = anchor.Hold(&a_);
  EXPECT_TRUE(AreModifiersOn(kAnchorModifier));
  anchor.Clear();
  EXPECT_TRUE(AreModifiersOff(kAnchorModifier));
}

TEST_F(AnchorTest, ModifierNotAffectedByEmptyHold) {
  Anchor<int> anchor;
  anchor.SetModifier(kAnchorModifier);
  AnchorHold hold_a = anchor.Hold(&a_);
  AnchorHold hold_b = anchor.Hold(&b_);
  hold_b.Reset();
  EXPECT_TRUE(AreModifiersOn(kAnchorModifier));
}

TEST_F(AnchorTest, ChangeModifierWhileHeld) {
  Anchor<int> anchor;
  anchor.SetModifier(kAnchorModifier);
  AnchorHold hold = anchor.Hold(&a_);
  anchor.SetModifier(kOtherModifier);
  EXPECT_TRUE(AreModifiersOff(kAnchorModifier));
  EXPECT_TRUE(AreModifiersOn(kOtherModifier));

  hold.Reset();
  EXPECT_TRUE(AreModifiersOff(kOtherModifier));
}

}  // namespace
}  // namespace jpr
