// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_mapping.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control_output.h"
#include "jpr/device/testing/fake_control_io.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/scene/testing/scene_test.h"
#include "jpr/scene/testing/test_property.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_property.h"

namespace jpr {
namespace {

using Type = ViewProperty::Type;
using Value = ViewProperty::Value;
using Output = ControlOutput::Type;

// +10dB, the loudest volume a control shows (as in view_mapping.cc).
constexpr double kMaxVolume = 3.16228;

// The device has a control with each kind of output alone: "DValue" (off or
// on), "CValue", "Text", and "Color". Mappings are added to an active view.
class ViewMappingTest : public SceneTest {
 protected:
  ViewMappingTest()
      : dvalue_(AddDValue("DValue")),
        cvalue_(AddCValue("CValue")),
        text_(AddText("Text")),
        color_(AddColor("Color")),
        view_(scene_.GetRootView()->AddChildView("View")) {
    scene_.GetRootView()->Enable();
    view_->Enable();
  }

  // Each adds a control with only the output: a DValue output whose modes
  // have the max values, or an output with `mode_count` modes.
  FakeDevice::FakeControl AddDValue(std::string_view name,
                                    std::vector<int> max_values = {1}) {
    return device_->AddControl(
        {.name = name,
         .dvalue_output = std::make_unique<FakeDValueOutput>(max_values)});
  }
  FakeDevice::FakeControl AddCValue(std::string_view name) {
    return device_->AddControl(
        {.name = name, .cvalue_output = std::make_unique<FakeCValueOutput>()});
  }
  FakeDevice::FakeControl AddText(std::string_view name, int mode_count = 1) {
    return device_->AddControl(
        {.name = name,
         .text_output = std::make_unique<FakeTextOutput>(mode_count)});
  }
  FakeDevice::FakeControl AddColor(std::string_view name) {
    return device_->AddControl(
        {.name = name, .color_output = std::make_unique<FakeColorOutput>()});
  }

  // Adds a control with every kind of output.
  FakeDevice::FakeControl AddAllOutputs(std::string_view name) {
    return device_->AddControl(
        {.name = name,
         .cvalue_output = std::make_unique<FakeCValueOutput>(),
         .dvalue_output = std::make_unique<FakeDValueOutput>(),
         .text_output = std::make_unique<FakeTextOutput>(),
         .color_output = std::make_unique<FakeColorOutput>()});
  }

  // Adds a property to the scene that holds `value` (see TestProperty).
  TestProperty* AddProperty(Type type, Value value,
                            std::string_view name = "user:value",
                            int max_value = 0) {
    return scene_.AddUserProperty(std::make_unique<TestProperty>(
        type, std::move(value), max_value, name));
  }
  ToggleValueProperty* AddFlag(std::string_view name = "user:flag") {
    return scene_.AddUserProperty(std::make_unique<ToggleValueProperty>(name));
  }

  // Maps the property to write to each control.
  void Write(std::string_view property,
             std::initializer_list<std::string_view> controls,
             const ViewMapping::Config& config = {}) {
    for (std::string_view control : controls) {
      EXPECT_TRUE(view_->AddMapping(ViewMapping::kWriteControl, property,
                                    GetControlName(control), config))
          << property << " to " << control;
    }
  }

  // Maps the property to write to each of DValue, CValue, Text, and Color.
  void WriteToEach(std::string_view property) {
    Write(property, {"DValue", "CValue", "Text", "Color"});
  }

  const FakeDevice::FakeControl dvalue_;
  const FakeDevice::FakeControl cvalue_;
  const FakeDevice::FakeControl text_;
  const FakeDevice::FakeControl color_;
  View* const view_;
};

//==============================================================================
// Property types
//==============================================================================

TEST_F(ViewMappingTest, EachTypeWritesTheOutputItSuitsBest) {
  struct Row {
    Type type;
    Value value;
    Output best;
  };
  const std::vector<Row> rows = {
      {Type::kToggle, true, Output::kDValue},
      {Type::kPan, 0.5, Output::kDValue},
      {Type::kVolume, 0.5, Output::kDValue},
      {Type::kNormalized, 0.5, Output::kDValue},
      {Type::kEnumerated, 1, Output::kDValue},
      {Type::kText, std::string("Text"), Output::kText},
      {Type::kTimelinePosition, TimelinePosition(1.0), Output::kText},
      {Type::kColor, Color{1, 2, 3}, Output::kColor},
  };
  std::vector<FakeDevice::FakeControl> controls;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const std::string name = absl::StrCat("user:value", i);
    AddProperty(rows[i].type, rows[i].value, name, /*max_value=*/2);
    controls.push_back(AddAllOutputs(absl::StrCat("All", i)));
    Write(name, {absl::StrCat("All", i)});
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    const FakeDevice::FakeControl& control = controls[i];
    const Output best = rows[i].best;
    EXPECT_EQ(control.dvalue_output->GetSetCount() > 0,
              best == Output::kDValue);
    EXPECT_EQ(control.cvalue_output->GetSetCount() > 0,
              best == Output::kCValue);
    EXPECT_EQ(control.text_output->GetSetCount() > 0, best == Output::kText);
    EXPECT_EQ(control.color_output->GetSetCount() > 0, best == Output::kColor);
  }
}

TEST_F(ViewMappingTest, TogglesWriteEachOutput) {
  TestProperty* property = AddProperty(Type::kToggle, true);
  WriteToEach("user:value");
  AddSurface();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 1);
  EXPECT_EQ(cvalue_.cvalue_output->GetValue(), 1.0);
  EXPECT_EQ(text_.text_output->GetText(), "On");
  EXPECT_EQ(color_.color_output->GetColor(), (Color{255, 255, 255}));

  property->SetBool(false);
  surface_->Run();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 0);
  EXPECT_EQ(cvalue_.cvalue_output->GetValue(), 0.0);
  EXPECT_EQ(text_.text_output->GetText(), "Off");
  EXPECT_EQ(color_.color_output->GetColor(), (Color{0, 0, 0}));
}

TEST_F(ViewMappingTest, PansWriteSteps) {
  TestProperty* property = AddProperty(Type::kPan, -1.0);
  FakeDevice::FakeControl three = AddDValue("Three", {2});
  FakeDevice::FakeControl five = AddDValue("Five", {4});
  Write("user:value", {"DValue", "Three", "Five", "CValue"});
  AddSurface();

  // Two steps split at the center, three beyond a pan of 0.65 either way, and
  // more keep a step each for hard left, the center, and hard right.
  struct Row {
    double pan;
    int two;
    int three;
    int five;
  };
  for (const Row& row : std::vector<Row>{
           {-1.0, 0, 0, 0},
           {-0.8, 0, 0, 1},
           {-0.5, 0, 1, 1},
           {0.0, 1, 1, 2},
           {0.5, 1, 1, 3},
           {0.8, 1, 2, 3},
           {1.0, 1, 2, 4},
       }) {
    SCOPED_TRACE(row.pan);
    property->SetPan(row.pan);
    surface_->Run();
    EXPECT_EQ(dvalue_.dvalue_output->GetValue(), row.two);
    EXPECT_EQ(three.dvalue_output->GetValue(), row.three);
    EXPECT_EQ(five.dvalue_output->GetValue(), row.five);
    EXPECT_NEAR(cvalue_.cvalue_output->GetValue(), (row.pan + 1.0) / 2.0, 1e-9);
  }
}

TEST_F(ViewMappingTest, VolumesWriteStepsUpToTheMax) {
  TestProperty* property = AddProperty(Type::kVolume, 0.0);
  FakeDevice::FakeControl three = AddDValue("Three", {2});
  FakeDevice::FakeControl five = AddDValue("Five", {4});
  Write("user:value", {"DValue", "Three", "Five", "CValue"});
  AddSurface();

  // Two steps are silent or not, and three split at unity gain.
  struct Row {
    double volume;
    int two;
    int three;
    int five;
  };
  for (const Row& row : std::vector<Row>{
           {0.0, 0, 0, 0},
           {0.5, 1, 1, 1},
           {1.0, 1, 1, 1},
           {2.0, 1, 2, 2},
           {kMaxVolume, 1, 2, 4},
           {5.0, 1, 2, 4},
       }) {
    SCOPED_TRACE(row.volume);
    property->SetVolume(row.volume);
    surface_->Run();
    EXPECT_EQ(dvalue_.dvalue_output->GetValue(), row.two);
    EXPECT_EQ(three.dvalue_output->GetValue(), row.three);
    EXPECT_EQ(five.dvalue_output->GetValue(), row.five);
    EXPECT_DOUBLE_EQ(cvalue_.cvalue_output->GetValue(),
                     std::min(row.volume / kMaxVolume, 1.0));
  }
}

TEST_F(ViewMappingTest, NormalizedValuesWriteEachOutput) {
  TestProperty* property = AddProperty(Type::kNormalized, 0.0);
  FakeDevice::FakeControl three = AddDValue("Three", {2});
  FakeDevice::FakeControl five = AddDValue("Five", {4});
  WriteToEach("user:value");
  Write("user:value", {"Three", "Five"});
  AddSurface();

  struct Row {
    double value;
    int two;
    int three;
    int five;
  };
  for (const Row& row : std::vector<Row>{
           {0.0, 0, 0, 0},
           {0.5, 0, 1, 2},
           {0.6, 1, 1, 2},
           {1.0, 1, 2, 4},
       }) {
    SCOPED_TRACE(row.value);
    property->SetNormalized(row.value);
    surface_->Run();
    EXPECT_EQ(dvalue_.dvalue_output->GetValue(), row.two);
    EXPECT_EQ(three.dvalue_output->GetValue(), row.three);
    EXPECT_EQ(five.dvalue_output->GetValue(), row.five);
    EXPECT_DOUBLE_EQ(cvalue_.cvalue_output->GetValue(), row.value);
  }
  EXPECT_EQ(text_.text_output->GetText(), "1");
  EXPECT_EQ(color_.color_output->GetColor(), (Color{255, 255, 255}));
}

TEST_F(ViewMappingTest, EnumeratedValuesWriteEachOutput) {
  TestProperty* property =
      AddProperty(Type::kEnumerated, 0, "user:value", /*max_value=*/2);
  FakeDevice::FakeControl five = AddDValue("Five", {4});
  WriteToEach("user:value");
  Write("user:value", {"Five"});
  AddSurface();

  // A DValue output's steps are spread over the values.
  struct Row {
    int value;
    int two;
    int five;
    uint8_t gray;
  };
  for (const Row& row : std::vector<Row>{
           {0, 0, 0, 0},
           {1, 1, 2, 127},
           {2, 1, 4, 255},
       }) {
    SCOPED_TRACE(row.value);
    property->SetInt(row.value);
    surface_->Run();
    EXPECT_EQ(dvalue_.dvalue_output->GetValue(), row.two);
    EXPECT_EQ(five.dvalue_output->GetValue(), row.five);
    EXPECT_DOUBLE_EQ(cvalue_.cvalue_output->GetValue(), row.value / 2.0);
    EXPECT_EQ(text_.text_output->GetText(), absl::StrCat(row.value));
    EXPECT_EQ(color_.color_output->GetColor(),
              (Color{row.gray, row.gray, row.gray}));
  }
}

TEST_F(ViewMappingTest, AnEnumeratedPropertyWithOneValueWritesTheFirstStep) {
  AddProperty(Type::kEnumerated, 0);
  FakeDevice::FakeControl five = AddDValue("Five", {4});
  Write("user:value", {"Five"});
  AddSurface();
  EXPECT_EQ(five.dvalue_output->GetValue(), 0);
  EXPECT_GT(five.dvalue_output->GetSetCount(), 0);
}

TEST_F(ViewMappingTest, TextWritesOnlyText) {
  TestProperty* property = AddProperty(Type::kText, std::string("Drums"));
  WriteToEach("user:value");
  AddSurface();
  EXPECT_EQ(text_.text_output->GetText(), "Drums");

  property->SetText("Bass");
  surface_->Run();
  EXPECT_EQ(text_.text_output->GetText(), "Bass");
  EXPECT_EQ(dvalue_.dvalue_output->GetSetCount(), 0);
  EXPECT_EQ(cvalue_.cvalue_output->GetSetCount(), 0);
  EXPECT_EQ(color_.color_output->GetSetCount(), 0);
}

TEST_F(ViewMappingTest, ColorsWriteEachOutput) {
  TestProperty* property = AddProperty(Type::kColor, Color{255, 255, 255});
  WriteToEach("user:value");
  AddSurface();
  EXPECT_EQ(color_.color_output->GetColor(), (Color{255, 255, 255}));
  EXPECT_EQ(text_.text_output->GetText(), "#ffffff");
  EXPECT_NEAR(cvalue_.cvalue_output->GetValue(), 1.0, 0.01);
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 1);

  property->SetColor({0, 0, 0});
  surface_->Run();
  EXPECT_EQ(color_.color_output->GetColor(), (Color{0, 0, 0}));
  EXPECT_EQ(text_.text_output->GetText(), "#000000");
  EXPECT_DOUBLE_EQ(cvalue_.cvalue_output->GetValue(), 0.0);
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 0);
}

TEST_F(ViewMappingTest, TimelinePositionsWriteTextInTheModesFormat) {
  TestProperty* property =
      AddProperty(Type::kTimelinePosition, TimelinePosition(1.5));
  FakeDevice::FakeControl time = AddText("Time");
  WriteToEach("user:value");

  // Mode 2 is the time format (see ControlTextOutput::SetTimelineText()).
  Write("user:value", {"Time"}, {.write = {.mode = 2}});
  AddSurface();
  EXPECT_EQ(time.text_output->GetPosition(), 1.5);
  EXPECT_EQ(time.text_output->GetTimelineMode(), TimelineMode::kTime);

  property->SetTimelinePosition(TimelinePosition(2.5));
  surface_->Run();
  EXPECT_EQ(time.text_output->GetPosition(), 2.5);
  EXPECT_EQ(text_.text_output->GetPosition(), 2.5);
  EXPECT_EQ(dvalue_.dvalue_output->GetSetCount(), 0);
  EXPECT_EQ(cvalue_.cvalue_output->GetSetCount(), 0);
  EXPECT_EQ(color_.color_output->GetSetCount(), 0);
}

TEST_F(ViewMappingTest, ActionsWriteNothing) {
  scene_.AddUserProperty(
      std::make_unique<CallbackActionProperty>("user:value", [] {}));
  WriteToEach("user:value");
  AddSurface();
  EXPECT_EQ(dvalue_.dvalue_output->GetSetCount(), 0);
  EXPECT_EQ(cvalue_.cvalue_output->GetSetCount(), 0);
  EXPECT_EQ(text_.text_output->GetSetCount(), 0);
  EXPECT_EQ(color_.color_output->GetSetCount(), 0);
}

//==============================================================================
// Modes
//==============================================================================

TEST_F(ViewMappingTest, WritesInItsMode) {
  AddProperty(Type::kToggle, true);
  FakeDevice::FakeControl lights = AddDValue("Lights", {1, 3});
  FakeDevice::FakeControl texts = AddText("Texts", 2);
  Write("user:value", {"Lights", "Texts"}, {.write = {.mode = 1}});
  AddSurface();

  // On is the mode's highest value.
  EXPECT_EQ(lights.dvalue_output->GetMode(), 1);
  EXPECT_EQ(lights.dvalue_output->GetValue(), 3);
  EXPECT_EQ(texts.text_output->GetMode(), 1);
  EXPECT_EQ(texts.text_output->GetText(), "On");
}

TEST_F(ViewMappingTest, TheFirstModeOverrideThatMatchesWins) {
  AddProperty(Type::kToggle, true);
  TestProperty* mode =
      AddProperty(Type::kEnumerated, 0, "user:mode", /*max_value=*/2);
  ToggleValueProperty* alt = AddFlag("user:alt");
  FakeDevice::FakeControl lights = AddDValue("Lights", {1, 3, 2, 4});
  Write("user:value", {"Lights"},
        {.write = {
             .mode = 0,
             .mode_overrides = {
                 {.property = "user:mode",
                  .value_to_mode = {{Value(1), 1}, {Value(2), 2}}},
                 {.property = "user:alt", .value_to_mode = {{Value(true), 3}}},
             }}});
  AddSurface();
  EXPECT_EQ(lights.dvalue_output->GetMode(), 0);
  EXPECT_EQ(lights.dvalue_output->GetValue(), 1);

  // A change to an override's property writes the control again, with on as
  // the mode's highest value.
  struct Row {
    int mode;
    bool alt;
    int expected_mode;
  };
  for (const Row& row : std::vector<Row>{
           {0, true, 3},
           {1, true, 1},
           {2, true, 2},
           {0, true, 3},
           {0, false, 0},
       }) {
    SCOPED_TRACE(absl::StrCat(row.mode, " ", row.alt));
    mode->SetInt(row.mode);
    alt->SetBool(row.alt);
    surface_->Run();
    EXPECT_EQ(lights.dvalue_output->GetMode(), row.expected_mode);
    EXPECT_EQ(lights.dvalue_output->GetValue(),
              (std::vector<int>{1, 3, 2, 4}[row.expected_mode]));
  }
}

//==============================================================================
// Conditions
//==============================================================================

// A control clears its outputs on its run after the last mapping writing to
// it lets go, so these run until the controls show it.

TEST_F(ViewMappingTest, AConditionHoldsTheOutputWhileMet) {
  AddProperty(Type::kToggle, true);
  ToggleValueProperty* flag = AddFlag();
  Write("user:value", {"DValue"},
        {.condition = ViewCondition::Config{.property = "user:flag"}});
  AddSurface();
  EXPECT_EQ(dvalue_.dvalue_output->GetSetCount(), 0);

  flag->SetBool(true);
  surface_->Run();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 1);

  flag->SetBool(false);
  RunUntilShown();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 0);
}

TEST_F(ViewMappingTest, MappingsTakeTurnsOnAControlByTheirConditions) {
  AddProperty(Type::kText, std::string("A"), "user:a");
  AddProperty(Type::kText, std::string("B"), "user:b");
  ToggleValueProperty* flag = AddFlag();
  Write("user:a", {"Text"},
        {.condition = ViewCondition::Config{.property = "user:flag"}});
  Write("user:b", {"Text"},
        {.condition =
             ViewCondition::Config{.property = "user:flag", .value = false}});
  AddSurface();
  EXPECT_EQ(text_.text_output->GetText(), "B");

  // The output isn't cleared between them.
  const int set_count = text_.text_output->GetSetCount();
  flag->SetBool(true);
  RunUntilShown();
  EXPECT_EQ(text_.text_output->GetText(), "A");
  EXPECT_EQ(text_.text_output->GetSetCount(), set_count + 1);
}

TEST_F(ViewMappingTest, AMappingThatCantWriteDoesntHoldTheOutput) {
  AddProperty(Type::kToggle, true);
  AddProperty(Type::kText, std::string("Text"), "user:text");
  ToggleValueProperty* flag = AddFlag();
  Write("user:value", {"DValue"},
        {.condition = ViewCondition::Config{.property = "user:flag"}});
  Write("user:text", {"DValue"});
  flag->SetBool(true);
  AddSurface();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 1);

  flag->SetBool(false);
  RunUntilShown();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 0);
}

//==============================================================================
// When it writes
//==============================================================================

TEST_F(ViewMappingTest, WritesOnlyWhenThePropertyChanges) {
  TestProperty* property = AddProperty(Type::kToggle, true);
  Write("user:value", {"DValue"});
  AddSurface();
  const int set_count = dvalue_.dvalue_output->GetSetCount();
  RunUntilShown();
  EXPECT_EQ(dvalue_.dvalue_output->GetSetCount(), set_count);

  property->SetBool(false);
  RunUntilShown();
  EXPECT_EQ(dvalue_.dvalue_output->GetSetCount(), set_count + 1);
}

}  // namespace
}  // namespace jpr
