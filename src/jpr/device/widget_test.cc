// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"

namespace jpr {
namespace {

using ::testing::IsEmpty;
using Field = Widget::Field;

// Returns the path, failing the test if the text isn't one.
WidgetPath Path(std::string_view text) {
  std::optional<WidgetPath> path = WidgetPath::Parse(text);
  EXPECT_TRUE(path.has_value()) << text;
  return path.value_or(WidgetPath());
}

class WidgetTest : public ::testing::Test {
 protected:
  Runner runner_{"Controls"};
  FakeDevice device_{runner_};
  Control* const play_ = device_.AddButton("play").control;
  Control* const fader_0_ = device_.AddFader("fader_0").control;
  Control* const fader_1_ = device_.AddFader("fader_1").control;
  const std::unique_ptr<const Widget> desk_ = Widget::ForStruct(
      Field{"play", Widget::ForControl(play_)},
      Field{"stop", Widget::ForControl(nullptr)},
      Field{"strip",
            Widget::ForArray(
                Widget::ForStruct(Field{"fader", Widget::ForControl(fader_0_)}),
                Widget::ForStruct(
                    Field{"fader", Widget::ForControl(fader_1_)}))});
};

TEST_F(WidgetTest, ControlHasItsControl) {
  std::unique_ptr<const Widget> play = Widget::ForControl(play_);
  EXPECT_EQ(play->GetKind(), WidgetKind::kControl);
  EXPECT_EQ(play->GetControl(), play_);
  EXPECT_THAT(play->GetFields(), IsEmpty());
  EXPECT_EQ(play->GetField("play"), nullptr);
  EXPECT_THAT(play->GetElements(), IsEmpty());
  EXPECT_EQ(play->GetElement(0), nullptr);

  std::unique_ptr<const Widget> absent = Widget::ForControl(nullptr);
  EXPECT_EQ(absent->GetKind(), WidgetKind::kControl);
  EXPECT_EQ(absent->GetControl(), nullptr);
}

TEST_F(WidgetTest, StructHasItsFieldsInOrder) {
  EXPECT_EQ(desk_->GetKind(), WidgetKind::kStruct);
  EXPECT_EQ(desk_->GetControl(), nullptr);
  ASSERT_EQ(std::ssize(desk_->GetFields()), 3);
  EXPECT_EQ(desk_->GetFields()[0].name, "play");
  EXPECT_EQ(desk_->GetFields()[1].name, "stop");
  EXPECT_EQ(desk_->GetFields()[2].name, "strip");
  ASSERT_NE(desk_->GetField("play"), nullptr);
  EXPECT_EQ(desk_->GetField("play"), desk_->GetFields()[0].widget.get());
  EXPECT_EQ(desk_->GetField("play")->GetControl(), play_);
  EXPECT_EQ(desk_->GetField("rewind"), nullptr);
  EXPECT_THAT(desk_->GetElements(), IsEmpty());
  EXPECT_THAT(Widget::ForStruct()->GetFields(), IsEmpty());
}

TEST_F(WidgetTest, ArrayHasItsElementsInOrder) {
  const Widget* strip = desk_->GetField("strip");
  ASSERT_NE(strip, nullptr);
  EXPECT_EQ(strip->GetKind(), WidgetKind::kArray);
  ASSERT_EQ(std::ssize(strip->GetElements()), 2);
  EXPECT_EQ(strip->GetElement(1), strip->GetElements()[1].get());
  ASSERT_NE(strip->GetElement(1), nullptr);
  EXPECT_EQ(strip->GetElement(1)->GetField("fader")->GetControl(), fader_1_);
  EXPECT_EQ(strip->GetElement(2), nullptr);
  EXPECT_EQ(strip->GetElement(-1), nullptr);
  EXPECT_EQ(strip->GetControl(), nullptr);
  EXPECT_THAT(strip->GetFields(), IsEmpty());
  EXPECT_THAT(Widget::ForArray()->GetElements(), IsEmpty());
}

TEST_F(WidgetTest, VectorsBuildTheSameWidgets) {
  std::vector<Field> fields;
  fields.push_back({"play", Widget::ForControl(play_)});
  std::vector<std::unique_ptr<const Widget>> elements;
  elements.push_back(Widget::ForControl(fader_0_));
  elements.push_back(Widget::ForControl(fader_1_));
  fields.push_back({"faders", Widget::ForArray(std::move(elements))});
  std::unique_ptr<const Widget> desk = Widget::ForStruct(std::move(fields));

  ASSERT_NE(desk->Find(Path("play")), nullptr);
  EXPECT_EQ(desk->Find(Path("play"))->GetControl(), play_);
  ASSERT_NE(desk->Find(Path("faders[1]")), nullptr);
  EXPECT_EQ(desk->Find(Path("faders[1]"))->GetControl(), fader_1_);
}

TEST_F(WidgetTest, FindFollowsFieldsAndIndices) {
  EXPECT_EQ(desk_->Find(Path("")), desk_.get());
  ASSERT_NE(desk_->Find(Path("play")), nullptr);
  EXPECT_EQ(desk_->Find(Path("play"))->GetControl(), play_);
  ASSERT_NE(desk_->Find(Path("strip[1].fader")), nullptr);
  EXPECT_EQ(desk_->Find(Path("strip[1].fader"))->GetControl(), fader_1_);

  const Widget* strip_0 = desk_->Find(Path("strip[0]"));
  ASSERT_NE(strip_0, nullptr);
  ASSERT_NE(strip_0->Find(Path("fader")), nullptr);
  EXPECT_EQ(strip_0->Find(Path("fader"))->GetControl(), fader_0_);
  const Widget* strip = desk_->Find(Path("strip"));
  ASSERT_NE(strip, nullptr);
  ASSERT_NE(strip->Find(Path("[1].fader")), nullptr);
  EXPECT_EQ(strip->Find(Path("[1].fader"))->GetControl(), fader_1_);
}

TEST_F(WidgetTest, FindIsNullWhereThereIsNoWidget) {
  EXPECT_EQ(desk_->Find(Path("rewind")), nullptr);
  EXPECT_EQ(desk_->Find(Path("strip[2]")), nullptr);
  EXPECT_EQ(desk_->Find(Path("strip[0].select")), nullptr);
  EXPECT_EQ(desk_->Find(Path("play.solid")), nullptr);
  EXPECT_EQ(desk_->Find(Path("play[0]")), nullptr);
  EXPECT_EQ(desk_->Find(Path("[0]")), nullptr);
  EXPECT_EQ(desk_->Find(Path("strip.fader")), nullptr);
}

}  // namespace
}  // namespace jpr
