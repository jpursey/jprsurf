// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_mapping.h"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"
#include "jpr/common/color.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/timeline.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
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
using Input = ControlInput::Type;
using PressBehavior = InputConfig::PressBehavior;

// +10dB, the loudest volume a control shows (as in view_mapping.cc).
constexpr double kMaxVolume = 3.16228;

constexpr Color kBlack = {0, 0, 0};
constexpr Color kWhite = {255, 255, 255};
constexpr Color kRed = {255, 0, 0};
constexpr Color kBlue = {0, 0, 255};

constexpr Control::Inputs kAllInputs = {Input::kValue, Input::kDelta,
                                        Input::kPress};

// The device has a control with each kind of output alone: "DValue" (off or
// on), "CValue", "Text", and "Color"; and with each kind of input alone:
// "Fader" (a value input), "Pot" (a delta input), and "Button" (a press input,
// with a release). Mappings are added to an active view.
class ViewMappingTest : public SceneTest {
 protected:
  ViewMappingTest()
      : dvalue_(AddDValue("DValue")),
        cvalue_(AddCValue("CValue")),
        text_(AddText("Text")),
        color_(AddColor("Color")),
        fader_(AddInputs("Fader", Input::kValue)),
        pot_(AddInputs("Pot", Input::kDelta)),
        button_(AddInputs("Button", Input::kPress)),
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

  // Adds a control with only the inputs. A press input has a release, unless
  // `has_release` is false.
  FakeDevice::FakeControl AddInputs(std::string_view name,
                                    Control::Inputs inputs,
                                    bool has_release = true) {
    FakeDevice::ControlOptions options = {.name = name};
    if (inputs.IsSet(Input::kValue)) {
      options.value_input = std::make_unique<FakeValueInput>();
    }
    if (inputs.IsSet(Input::kDelta)) {
      options.delta_input = std::make_unique<FakeDeltaInput>();
    }
    if (inputs.IsSet(Input::kPress)) {
      options.press_input = std::make_unique<FakePressInput>(has_release);
    }
    return device_->AddControl(std::move(options));
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

  // Maps the property to write to, or read from, each control.
  void Write(std::string_view property,
             std::initializer_list<std::string_view> controls,
             const ViewMapping::Config& config = {}) {
    Map(ViewMapping::kWriteControl, property, controls, config);
  }
  void Read(std::string_view property,
            std::initializer_list<std::string_view> controls,
            const ViewMapping::Config& config = {}) {
    Map(ViewMapping::kReadControl, property, controls, config);
  }

  // Maps the property to write to each of DValue, CValue, Text, and Color.
  void WriteToEach(std::string_view property) {
    Write(property, {"DValue", "CValue", "Text", "Color"});
  }

  // A property, and a control that reads it.
  struct Reader {
    TestProperty* property;
    FakeDevice::FakeControl control;
  };

  // Adds a property that holds `value` (see AddProperty(), with a highest
  // value of 2), and a control with the inputs that reads it, named
  // "user:<name>" and `name`.
  Reader AddReader(std::string_view name, Type type, Value value,
                   Control::Inputs inputs,
                   const ViewMapping::ReadConfig& read = {}) {
    const std::string property = absl::StrCat("user:", name);
    Reader reader = {
        .property = AddProperty(type, std::move(value), property,
                                /*max_value=*/2),
        .control = AddInputs(name, inputs),
    };
    Read(property, {name}, {.read = read});
    return reader;
  }

  // Gives each kind of input the reader's control has in turn (a value of 1, a
  // delta of 0.5, and a tap), and returns the first that changed its property
  // from `initial`, or nullopt if none did.
  std::optional<Input> GetInputRead(const Reader& reader,
                                    const Value& initial) {
    const FakeDevice::FakeControl& control = reader.control;
    if (control.value_input != nullptr) {
      Move(control, 1.0);
      surface_->Run();
      if (!reader.property->Equals(initial)) {
        return Input::kValue;
      }
    }
    if (control.delta_input != nullptr) {
      Turn(control, 0.5);
      surface_->Run();
      if (!reader.property->Equals(initial)) {
        return Input::kDelta;
      }
    }
    if (control.press_input != nullptr) {
      Tap(control);
      if (!reader.property->Equals(initial)) {
        return Input::kPress;
      }
    }
    return std::nullopt;
  }

  const FakeDevice::FakeControl dvalue_;
  const FakeDevice::FakeControl cvalue_;
  const FakeDevice::FakeControl text_;
  const FakeDevice::FakeControl color_;
  const FakeDevice::FakeControl fader_;
  const FakeDevice::FakeControl pot_;
  const FakeDevice::FakeControl button_;
  View* const view_;

 private:
  void Map(ViewMapping::TypeFlags type, std::string_view property,
           std::initializer_list<std::string_view> controls,
           const ViewMapping::Config& config) {
    for (std::string_view control : controls) {
      EXPECT_TRUE(
          view_->AddMapping(type, property, GetControlName(control), config))
          << property << " and " << control;
    }
  }
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
  EXPECT_EQ(color_.color_output->GetColor(), kWhite);

  property->SetBool(false);
  surface_->Run();
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 0);
  EXPECT_EQ(cvalue_.cvalue_output->GetValue(), 0.0);
  EXPECT_EQ(text_.text_output->GetText(), "Off");
  EXPECT_EQ(color_.color_output->GetColor(), kBlack);
}

TEST_F(ViewMappingTest, PansWriteSteps) {
  TestProperty* property = AddProperty(Type::kPan, -1.0);
  FakeDevice::FakeControl three = AddDValue("Three", {2});
  FakeDevice::FakeControl four = AddDValue("Four", {3});
  FakeDevice::FakeControl five = AddDValue("Five", {4});
  Write("user:value", {"DValue", "Three", "Four", "Five", "CValue"});
  AddSurface();

  // Two steps split at the center, three beyond a pan of 0.65 either way, and
  // more keep a step each for hard left and hard right (and the center, with an
  // odd number).
  struct Row {
    double pan;
    int two;
    int three;
    int four;
    int five;
  };
  for (const Row& row : std::vector<Row>{
           {-1.0, 0, 0, 0, 0},
           {-0.8, 0, 0, 1, 1},
           {-0.5, 0, 1, 1, 1},
           {0.0, 1, 1, 1, 2},
           {0.5, 1, 1, 2, 3},
           {0.8, 1, 2, 2, 3},
           {1.0, 1, 2, 3, 4},
       }) {
    SCOPED_TRACE(row.pan);
    property->SetPan(row.pan);
    surface_->Run();
    EXPECT_EQ(dvalue_.dvalue_output->GetValue(), row.two);
    EXPECT_EQ(three.dvalue_output->GetValue(), row.three);
    EXPECT_EQ(four.dvalue_output->GetValue(), row.four);
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
  EXPECT_EQ(color_.color_output->GetColor(), kWhite);
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
  TestProperty* property = AddProperty(Type::kColor, kWhite);
  WriteToEach("user:value");
  AddSurface();
  EXPECT_EQ(color_.color_output->GetColor(), kWhite);
  EXPECT_EQ(text_.text_output->GetText(), "#ffffff");
  EXPECT_NEAR(cvalue_.cvalue_output->GetValue(), 1.0, 0.01);
  EXPECT_EQ(dvalue_.dvalue_output->GetValue(), 1);

  property->SetColor(kBlack);
  surface_->Run();
  EXPECT_EQ(color_.color_output->GetColor(), kBlack);
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

TEST_F(ViewMappingTest, StepsAreThoseOfTheModeAnOverridePicks) {
  // Each value is on in mode 0, and its step in mode 1.
  struct Row {
    Type type;
    Value value;
    int step;
  };
  const std::vector<Row> rows = {
      {Type::kPan, 0.5, 3},
      {Type::kVolume, 2.0, 2},
      {Type::kNormalized, 0.6, 2},
      // A luminance of 0.6.
      {Type::kColor, Color{204, 204, 204}, 2},
  };
  ToggleValueProperty* alt = AddFlag("user:alt");
  std::vector<FakeDevice::FakeControl> controls;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const std::string name = absl::StrCat("user:value", i);
    AddProperty(rows[i].type, rows[i].value, name);

    // Mode 0 is off or on, and mode 1 has five steps.
    controls.push_back(AddDValue(absl::StrCat("Lights", i), {1, 4}));
    Write(
        name, {absl::StrCat("Lights", i)},
        {.write = {.mode_overrides = {{.property = "user:alt",
                                       .value_to_mode = {{Value(true), 1}}}}}});
  }
  AddSurface();
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(controls[i].dvalue_output->GetMode(), 0);
    EXPECT_EQ(controls[i].dvalue_output->GetValue(), 1);
  }

  alt->SetBool(true);
  surface_->Run();
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(controls[i].dvalue_output->GetMode(), 1);
    EXPECT_EQ(controls[i].dvalue_output->GetValue(), rows[i].step);
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

//==============================================================================
// Reading each property type
//==============================================================================

TEST_F(ViewMappingTest, EachTypeReadsTheInputItSuitsBest) {
  // Each type is read from a control with every kind of input, and from one
  // with every kind but the best.
  struct Row {
    Type type;
    Value initial;
    Input best;
    std::optional<Input> next;
  };
  const std::vector<Row> rows = {
      {Type::kToggle, false, Input::kPress, Input::kDelta},
      {Type::kPan, 0.0, Input::kValue, Input::kDelta},
      {Type::kVolume, 0.0, Input::kValue, Input::kDelta},
      {Type::kNormalized, 0.0, Input::kValue, Input::kDelta},
      {Type::kEnumerated, 0, Input::kValue, Input::kDelta},
      {Type::kText, std::string("A"), Input::kValue, Input::kPress},
      {Type::kColor, kBlack, Input::kValue, Input::kDelta},
      {Type::kTimelinePosition, TimelinePosition(0.0), Input::kDelta,
       std::nullopt},
  };

  // Text reads a press only with a range.
  const ViewMapping::ReadConfig read = {.property_min = std::string("A"),
                                        .property_max = std::string("B")};
  std::vector<Reader> best;
  std::vector<Reader> next;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    Control::Inputs rest = kAllInputs;
    rest.Clear(rows[i].best);
    best.push_back(AddReader(absl::StrCat("best", i), rows[i].type,
                             rows[i].initial, kAllInputs, read));
    next.push_back(AddReader(absl::StrCat("next", i), rows[i].type,
                             rows[i].initial, rest, read));
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(GetInputRead(best[i], rows[i].initial), rows[i].best);
    EXPECT_EQ(GetInputRead(next[i], rows[i].initial), rows[i].next);
  }
}

TEST_F(ViewMappingTest, AConfiguredInputTypeIsReadInPlaceOfTheBest) {
  // Each type is read from a control with every kind of input, configured to
  // read one that isn't the best for it, or that it can't read at all.
  struct Row {
    Type type;
    Value initial;
    Input input;
    std::optional<Input> read;
  };
  const std::vector<Row> rows = {
      {Type::kToggle, false, Input::kValue, Input::kValue},
      {Type::kToggle, false, Input::kDelta, Input::kDelta},
      {Type::kPan, 0.0, Input::kDelta, Input::kDelta},
      {Type::kPan, 0.0, Input::kPress, Input::kPress},
      {Type::kVolume, 0.0, Input::kPress, Input::kPress},
      {Type::kNormalized, 0.0, Input::kDelta, Input::kDelta},
      {Type::kEnumerated, 0, Input::kPress, Input::kPress},
      {Type::kText, std::string("A"), Input::kPress, Input::kPress},
      {Type::kText, std::string("A"), Input::kDelta, std::nullopt},
      {Type::kColor, kBlack, Input::kDelta, Input::kDelta},
      {Type::kTimelinePosition, TimelinePosition(0.0), Input::kPress,
       std::nullopt},
  };
  std::vector<Reader> readers;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    // Text reads a press only with a range.
    readers.push_back(AddReader(absl::StrCat("value", i), rows[i].type,
                                rows[i].initial, kAllInputs,
                                {.input_type = rows[i].input,
                                 .property_min = std::string("A"),
                                 .property_max = std::string("B")}));
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(GetInputRead(readers[i], rows[i].initial), rows[i].read);
  }
}

TEST_F(ViewMappingTest, AConfiguredInputTypeTheControlLacksReadsNothing) {
  TestProperty* property = AddProperty(Type::kToggle, false);
  Read("user:value", {"Button"}, {.read = {.input_type = Input::kValue}});
  AddSurface();
  Tap(button_);
  EXPECT_FALSE(property->GetBool());
}

TEST_F(ViewMappingTest, PressReleaseReadsNothingWithAnotherInputType) {
  Reader reader =
      AddReader("value", Type::kToggle, false, kAllInputs,
                {.input_type = Input::kValue, .press_release = true});
  AddSurface();
  Press(reader.control);
  surface_->Run();
  EXPECT_FALSE(reader.property->GetBool());
  Release(reader.control);
  Move(reader.control, 1.0);
  surface_->Run();
  EXPECT_FALSE(reader.property->GetBool());
}

TEST_F(ViewMappingTest, ActionsReadOnlyPresses) {
  int run_count = 0;
  scene_.AddUserProperty(std::make_unique<CallbackActionProperty>(
      "user:value", [&run_count] { ++run_count; }));
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();
  Move(fader_, 1.0);
  Turn(pot_, 1.0);
  surface_->Run();
  EXPECT_EQ(run_count, 0);

  Tap(button_);
  EXPECT_EQ(run_count, 1);
  Tap(button_);
  EXPECT_EQ(run_count, 2);
}

TEST_F(ViewMappingTest, TogglesReadEachInput) {
  TestProperty* property = AddProperty(Type::kToggle, false);
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();

  // A value is on past halfway.
  Move(fader_, 0.6);
  surface_->Run();
  EXPECT_TRUE(property->GetBool());
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_FALSE(property->GetBool());

  // A delta turns it on or off by its direction.
  Turn(pot_, 0.1);
  surface_->Run();
  EXPECT_TRUE(property->GetBool());
  Turn(pot_, -0.1);
  surface_->Run();
  EXPECT_FALSE(property->GetBool());

  // A press flips it.
  Tap(button_);
  EXPECT_TRUE(property->GetBool());
  Tap(button_);
  EXPECT_FALSE(property->GetBool());
}

TEST_F(ViewMappingTest, PansReadEachInput) {
  TestProperty* property = AddProperty(Type::kPan, 0.0);
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();

  // A value spans hard left to hard right.
  Move(fader_, 0.25);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), -0.5);
  Move(fader_, 1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), 1.0);

  // A delta moves it, as far as hard left or right.
  Turn(pot_, -0.25);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), 0.75);
  Turn(pot_, 5.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), 1.0);
  Turn(pot_, -5.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), -1.0);

  // A press steps from hard left to the center, then hard right, and back.
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetPan(), 0.0);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetPan(), 1.0);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetPan(), -1.0);
}

TEST_F(ViewMappingTest, VolumesReadEachInput) {
  TestProperty* property = AddProperty(Type::kVolume, 0.0);
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();

  // A value spans silence to the loudest.
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), kMaxVolume / 2.0);
  Move(fader_, 1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), kMaxVolume);

  // A delta moves it, as far as silence or the loudest.
  Turn(pot_, -1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), kMaxVolume - 1.0);
  Turn(pot_, 5.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), kMaxVolume);
  Turn(pot_, -5.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), 0.0);

  // A press toggles between silence and unity gain.
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetVolume(), 1.0);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetVolume(), 0.0);
}

TEST_F(ViewMappingTest, NormalizedValuesReadEachInput) {
  TestProperty* property = AddProperty(Type::kNormalized, 0.0);
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();
  Move(fader_, 0.25);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.25);

  // A delta moves it, as far as 0 or 1.
  Turn(pot_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.75);
  Turn(pot_, 1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 1.0);
  Turn(pot_, -5.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.0);

  // A press toggles between 0 and 1.
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 1.0);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.0);
}

TEST_F(ViewMappingTest, EnumeratedValuesReadEachInput) {
  TestProperty* property =
      AddProperty(Type::kEnumerated, 0, "user:value", /*max_value=*/4);
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();

  // A value is rounded to the nearest.
  Move(fader_, 0.6);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 2);
  Move(fader_, 0.65);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 3);

  // A delta steps once whatever its size, as far as 0 or the highest.
  Turn(pot_, 0.1);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 4);
  Turn(pot_, 0.1);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 4);
  Turn(pot_, -3.0);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 3);
  Move(fader_, 0.0);
  surface_->Run();
  Turn(pot_, -0.1);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 0);

  // A press steps to the next, wrapping around.
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 1);
  Move(fader_, 1.0);
  surface_->Run();
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 0);
}

TEST_F(ViewMappingTest, TextReadsAValueAndAPressInItsRange) {
  TestProperty* property = AddProperty(Type::kText, std::string("C"));
  TestProperty* unranged =
      AddProperty(Type::kText, std::string("C"), "user:unranged");
  FakeDevice::FakeControl other = AddInputs("Other", Input::kPress);
  Read("user:value", {"Fader", "Pot", "Button"},
       {.read = {.property_min = std::string("A"),
                 .property_max = std::string("B")}});
  Read("user:unranged", {"Other"});
  AddSurface();
  Turn(pot_, 1.0);
  surface_->Run();
  EXPECT_EQ(property->GetText(), "C");

  // A value is written as a number.
  Move(fader_, 0.25);
  surface_->Run();
  EXPECT_EQ(property->GetText(), "0.25");

  // A press sets the end of the range it isn't at, and without a range does
  // nothing.
  Tap(button_);
  EXPECT_EQ(property->GetText(), "B");
  Tap(button_);
  EXPECT_EQ(property->GetText(), "A");
  Tap(other);
  EXPECT_EQ(unranged->GetText(), "C");
}

TEST_F(ViewMappingTest, ColorsReadEachInput) {
  TestProperty* property = AddProperty(Type::kColor, kBlack);
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();

  // A value is a gray.
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_EQ(property->GetColor(), (Color{127, 127, 127}));
  Move(fader_, 0.0);
  surface_->Run();
  EXPECT_EQ(property->GetColor(), kBlack);

  // A delta moves its luminance, as a gray.
  Turn(pot_, 1.0);
  surface_->Run();
  EXPECT_EQ(property->GetColor(), kWhite);
  Turn(pot_, -0.5);
  surface_->Run();
  EXPECT_EQ(property->GetColor(), (Color{127, 127, 127}));

  // A press makes a dark color white, and a bright one black.
  Tap(button_);
  EXPECT_EQ(property->GetColor(), kWhite);
  Tap(button_);
  EXPECT_EQ(property->GetColor(), kBlack);
}

TEST_F(ViewMappingTest, TimelinePositionsReadOnlyADelta) {
  TestProperty* property =
      AddProperty(Type::kTimelinePosition, TimelinePosition(1.0));
  Read("user:value", {"Fader", "Pot", "Button"});
  AddSurface();
  Move(fader_, 0.5);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetTimelinePosition().GetValue(), 1.0);

  Turn(pot_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetTimelinePosition().GetValue(), 1.5);
  Turn(pot_, -1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetTimelinePosition().GetValue(), 0.5);
}

//==============================================================================
// Reading in a range
//==============================================================================

TEST_F(ViewMappingTest, APressGoesToTheOtherEndOrTheNearerOne) {
  // A pan whose range is on one side of the center, a volume, and a
  // normalized value each toggle between the ends of their range: from one end
  // to the other, and from between them to the nearer.
  struct Row {
    Type type;
    Value initial;
    ViewMapping::ReadConfig read;
    double pressed;
  };
  const ViewMapping::ReadConfig right = {.property_min = Value(0.0),
                                         .property_max = Value(1.0)};
  const ViewMapping::ReadConfig middle = {.property_min = Value(0.2),
                                          .property_max = Value(0.6)};
  const std::vector<Row> rows = {
      {Type::kPan, 0.0, right, 1.0},
      {Type::kPan, 1.0, right, 0.0},
      {Type::kPan, 0.3, right, 0.0},
      {Type::kPan, 0.7, right, 1.0},
      {Type::kPan, 0.5, right, 1.0},
      {Type::kVolume, 0.3, {}, 0.0},
      {Type::kVolume, 0.8, {}, 1.0},
      {Type::kNormalized, 0.3, middle, 0.2},
      {Type::kNormalized, 0.5, middle, 0.6},

      // Beyond an end is at it.
      {Type::kVolume, 2.0, {}, 0.0},
      {Type::kNormalized, 0.1, middle, 0.6},
  };
  std::vector<Reader> readers;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    readers.push_back(AddReader(absl::StrCat("value", i), rows[i].type,
                                rows[i].initial, Input::kPress, rows[i].read));
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    Tap(readers[i].control);
    EXPECT_TRUE(readers[i].property->Equals(rows[i].pressed));
  }
}

TEST_F(ViewMappingTest, PansReadInTheirRange) {
  TestProperty* property = AddProperty(Type::kPan, 0.0);
  Read("user:value", {"Fader", "Pot", "Button"},
       {.read = {.property_min = Value(-0.5), .property_max = Value(0.5)}});
  AddSurface();
  Move(fader_, 0.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), -0.5);
  Move(fader_, 1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), 0.5);
  Turn(pot_, -2.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetPan(), -0.5);

  // A press steps through the center, as the range has it.
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetPan(), 0.0);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetPan(), 0.5);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetPan(), -0.5);
}

TEST_F(ViewMappingTest, VolumesReadInTheirRange) {
  TestProperty* property = AddProperty(Type::kVolume, 0.0);
  Read("user:value", {"Fader", "Pot", "Button"},
       {.read = {.property_min = Value(0.5), .property_max = Value(2.0)}});
  AddSurface();
  Move(fader_, 0.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), 0.5);
  Move(fader_, 1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), 2.0);
  Turn(pot_, -5.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetVolume(), 0.5);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetVolume(), 2.0);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetVolume(), 0.5);
}

TEST_F(ViewMappingTest, NormalizedValuesReadInTheirRange) {
  TestProperty* property = AddProperty(Type::kNormalized, 0.0);
  Read("user:value", {"Fader", "Pot", "Button"},
       {.read = {.property_min = Value(0.2), .property_max = Value(0.6)}});
  AddSurface();
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.4);
  Turn(pot_, 1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.6);
  Turn(pot_, -1.0);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.2);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.6);
  Tap(button_);
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.2);
}

TEST_F(ViewMappingTest, EnumeratedValuesReadInTheirRange) {
  TestProperty* property =
      AddProperty(Type::kEnumerated, 0, "user:value", /*max_value=*/4);
  Read("user:value", {"Fader", "Pot", "Button"},
       {.read = {.property_min = Value(1), .property_max = Value(3)}});
  AddSurface();
  Move(fader_, 0.0);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 1);
  Move(fader_, 1.0);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 3);
  Turn(pot_, 1.0);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 3);
  Turn(pot_, -1.0);
  surface_->Run();
  EXPECT_EQ(property->GetInt(), 2);

  // A press wraps around within the range.
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 3);
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 1);
}

TEST_F(ViewMappingTest, PressTogglesSwitchAnEnumeratedValueBetweenItsEnds) {
  TestProperty* property =
      AddProperty(Type::kEnumerated, 1, "user:value", /*max_value=*/3);
  TestProperty* ranged =
      AddProperty(Type::kEnumerated, 0, "user:ranged", /*max_value=*/3);
  FakeDevice::FakeControl other = AddInputs("Other", Input::kPress);
  Read("user:value", {"Button"}, {.read = {.press_toggles = true}});
  Read("user:ranged", {"Other"},
       {.read = {.property_min = Value(1),
                 .property_max = Value(2),
                 .press_toggles = true}});
  AddSurface();
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 3);
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 0);
  Tap(button_);
  EXPECT_EQ(property->GetInt(), 3);

  Tap(other);
  EXPECT_EQ(ranged->GetInt(), 2);
  Tap(other);
  EXPECT_EQ(ranged->GetInt(), 1);
}

TEST_F(ViewMappingTest, ColorsReadInTheirRange) {
  TestProperty* property = AddProperty(Type::kColor, kBlack);
  TestProperty* max_only = AddProperty(Type::kColor, kBlack, "user:max_only");
  TestProperty* min_only = AddProperty(Type::kColor, kBlack, "user:min_only");
  FakeDevice::FakeControl max_fader = AddInputs("MaxFader", Input::kValue);
  FakeDevice::FakeControl min_fader = AddInputs("MinFader", Input::kValue);
  Read("user:value", {"Fader", "Button"},
       {.read = {.property_min = kRed, .property_max = kBlue}});
  Read("user:max_only", {"MaxFader"}, {.read = {.property_max = kRed}});
  Read("user:min_only", {"MinFader"}, {.read = {.property_min = kRed}});
  AddSurface();

  // A value blends the ends.
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_EQ(property->GetColor(), (Color{127, 0, 127}));
  Move(fader_, 1.0);
  surface_->Run();
  EXPECT_EQ(property->GetColor(), kBlue);

  // A press sets the end it isn't at.
  Tap(button_);
  EXPECT_EQ(property->GetColor(), kRed);
  Tap(button_);
  EXPECT_EQ(property->GetColor(), kBlue);

  // With one end, the other is black or white.
  Move(max_fader, 0.0);
  Move(min_fader, 1.0);
  surface_->Run();
  EXPECT_EQ(max_only->GetColor(), kBlack);
  EXPECT_EQ(min_only->GetColor(), kWhite);
  Move(max_fader, 1.0);
  Move(min_fader, 0.0);
  surface_->Run();
  EXPECT_EQ(max_only->GetColor(), kRed);
  EXPECT_EQ(min_only->GetColor(), kRed);
}

//==============================================================================
// Reading a press and release
//==============================================================================

TEST_F(ViewMappingTest, PressReleaseSetsTheMaxWhileHeld) {
  // Pans, volumes, and text without a range don't read a press and release.
  struct Row {
    Type type;
    Value initial;
    ViewMapping::ReadConfig read;
    Value held;
    Value released;
  };
  const std::vector<Row> rows = {
      {Type::kToggle, false, {}, true, false},
      {Type::kNormalized, 0.5, {}, 1.0, 0.0},
      {Type::kNormalized,
       0.5,
       {.property_min = Value(0.2), .property_max = Value(0.6)},
       0.6,
       0.2},
      {Type::kEnumerated, 1, {}, 2, 0},
      {Type::kEnumerated,
       0,
       {.property_min = Value(1), .property_max = Value(2)},
       2,
       1},
      {Type::kText,
       std::string("C"),
       {.property_min = std::string("A"), .property_max = std::string("B")},
       std::string("B"),
       std::string("A")},
      {Type::kColor, kBlue, {}, kWhite, kBlack},
      {Type::kColor,
       kBlack,
       {.property_min = kRed, .property_max = kBlue},
       kBlue,
       kRed},
      {Type::kPan, 0.0, {}, 0.0, 0.0},
      {Type::kVolume, 1.0, {}, 1.0, 1.0},
      {Type::kText, std::string("C"), {}, std::string("C"), std::string("C")},
  };
  std::vector<Reader> readers;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    ViewMapping::ReadConfig read = rows[i].read;
    read.press_release = true;
    readers.push_back(AddReader(absl::StrCat("value", i), rows[i].type,
                                rows[i].initial, Input::kPress, read));
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    Press(readers[i].control);
    surface_->Run();
    EXPECT_TRUE(readers[i].property->Equals(rows[i].held));
    Release(readers[i].control);
    surface_->Run();
    EXPECT_TRUE(readers[i].property->Equals(rows[i].released));
  }
}

TEST_F(ViewMappingTest, APressIsReadWhenPressedNotReleased) {
  struct Row {
    Type type;
    Value initial;
    ViewMapping::ReadConfig read;
    Value pressed;
  };
  const std::vector<Row> rows = {
      {Type::kToggle, false, {}, true},
      {Type::kPan, -1.0, {}, 0.0},
      {Type::kPan,
       0.0,
       {.property_min = Value(0.0), .property_max = Value(1.0)},
       1.0},
      {Type::kVolume, 0.0, {}, 1.0},
      {Type::kNormalized, 0.0, {}, 1.0},
      {Type::kEnumerated, 0, {}, 1},
      {Type::kEnumerated, 0, {.press_toggles = true}, 2},
      {Type::kText,
       std::string("A"),
       {.property_min = std::string("A"), .property_max = std::string("B")},
       std::string("B")},
      {Type::kColor, kBlack, {}, kWhite},
      {Type::kColor,
       kRed,
       {.property_min = kRed, .property_max = kBlue},
       kBlue},
  };
  std::vector<Reader> readers;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    readers.push_back(AddReader(absl::StrCat("value", i), rows[i].type,
                                rows[i].initial, Input::kPress, rows[i].read));
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    Press(readers[i].control);
    surface_->Run();
    EXPECT_TRUE(readers[i].property->Equals(rows[i].pressed));
    Release(readers[i].control);
    surface_->Run();
    EXPECT_TRUE(readers[i].property->Equals(rows[i].pressed));
  }
}

TEST_F(ViewMappingTest, PressReleaseNeedsARelease) {
  TestProperty* property = AddProperty(Type::kToggle, false);
  FakeDevice::FakeControl pedal =
      AddInputs("Pedal", Input::kPress, /*has_release=*/false);
  Read("user:value", {"Pedal"}, {.read = {.press_release = true}});
  AddSurface();
  Tap(pedal);
  EXPECT_FALSE(property->GetBool());
}

//==============================================================================
// Reading with modifiers and press behaviors
//==============================================================================

TEST_F(ViewMappingTest, ARequiredModifierMustBeOn) {
  TestProperty* property = AddProperty(Type::kNormalized, 0.0);
  Read("user:value", {"Fader"}, {.read = {.required_modifiers = kModShift}});
  AddSurface();
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.0);

  SetModifiers(kModShift, true);
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.5);
}

TEST_F(ViewMappingTest, MappingsTakeTurnsOnAControlByTheirModifiers) {
  // Input goes only to the mapping whose modifiers are all on, while no other
  // modifier that another mapping needs is.
  TestProperty* plain = AddProperty(Type::kNormalized, 0.0, "user:plain");
  TestProperty* shift = AddProperty(Type::kNormalized, 0.0, "user:shift");
  TestProperty* ctrl_shift =
      AddProperty(Type::kNormalized, 0.0, "user:ctrl_shift");
  Read("user:plain", {"Fader"});
  Read("user:shift", {"Fader"}, {.read = {.required_modifiers = kModShift}});
  Read("user:ctrl_shift", {"Fader"},
       {.read = {.required_modifiers = kModShift | kModCtrl}});
  AddSurface();

  struct Row {
    Modifiers modifiers;
    double value;
    double plain;
    double shift;
    double ctrl_shift;
  };
  for (const Row& row : std::vector<Row>{
           {0, 0.25, 0.25, 0.0, 0.0},
           {kModShift, 0.5, 0.25, 0.5, 0.0},
           {kModShift | kModCtrl, 0.75, 0.25, 0.5, 0.75},
           {kModCtrl, 1.0, 0.25, 0.5, 0.75},
       }) {
    SCOPED_TRACE(row.modifiers);
    ResetModifiers();
    SetModifiers(row.modifiers, true);
    Move(fader_, row.value);
    surface_->Run();
    EXPECT_DOUBLE_EQ(plain->GetNormalized(), row.plain);
    EXPECT_DOUBLE_EQ(shift->GetNormalized(), row.shift);
    EXPECT_DOUBLE_EQ(ctrl_shift->GetNormalized(), row.ctrl_shift);
  }
}

TEST_F(ViewMappingTest, TapsReadOnlyShortPresses) {
  TestProperty* held = AddProperty(Type::kToggle, false, "user:held");
  TestProperty* tapped = AddProperty(Type::kToggle, false, "user:tapped");
  Read("user:held", {"Button"}, {.read = {.press_release = true}});
  Read("user:tapped", {"Button"},
       {.read = {.press_behavior = PressBehavior::kTap}});
  AddSurface();
  Hold(button_);
  EXPECT_TRUE(held->GetBool());
  Release(button_);
  RunUntilShown();
  EXPECT_FALSE(held->GetBool());
  EXPECT_FALSE(tapped->GetBool());

  Tap(button_);
  EXPECT_TRUE(tapped->GetBool());
}

TEST_F(ViewMappingTest, DoublePressesReadOnlyTwoQuickPresses) {
  TestProperty* once = AddProperty(Type::kToggle, false, "user:once");
  TestProperty* twice = AddProperty(Type::kToggle, false, "user:twice");
  Read("user:once", {"Button"});
  Read("user:twice", {"Button"},
       {.read = {.press_behavior = PressBehavior::kDoublePress}});
  AddSurface();
  Tap(button_);
  EXPECT_TRUE(once->GetBool());
  EXPECT_FALSE(twice->GetBool());

  DoublePress(button_);
  EXPECT_TRUE(once->GetBool());
  EXPECT_TRUE(twice->GetBool());
}

TEST_F(ViewMappingTest, LongPressesReadOnlyHeldPresses) {
  TestProperty* once = AddProperty(Type::kToggle, false, "user:once");
  TestProperty* held = AddProperty(Type::kToggle, false, "user:held");
  Read("user:once", {"Button"});
  Read("user:held", {"Button"},
       {.read = {.press_behavior = PressBehavior::kLongPress}});
  AddSurface();
  Tap(button_);
  EXPECT_TRUE(once->GetBool());
  EXPECT_FALSE(held->GetBool());

  LongPress(button_);
  EXPECT_TRUE(once->GetBool());
  EXPECT_TRUE(held->GetBool());
}

//==============================================================================
// Reading with a condition
//==============================================================================

TEST_F(ViewMappingTest, AConditionMustBeMetToRead) {
  TestProperty* property = AddProperty(Type::kNormalized, 0.0);
  ToggleValueProperty* flag = AddFlag();
  Read("user:value", {"Fader"},
       {.condition = ViewCondition::Config{.property = "user:flag"}});
  AddSurface();
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.0);

  flag->SetBool(true);
  surface_->Run();
  Move(fader_, 0.5);
  surface_->Run();
  EXPECT_DOUBLE_EQ(property->GetNormalized(), 0.5);
}

TEST_F(ViewMappingTest, InputForAnUnmetConditionGoesToNoOtherMapping) {
  TestProperty* shift = AddProperty(Type::kToggle, false, "user:shift");
  TestProperty* plain = AddProperty(Type::kToggle, false, "user:plain");
  ToggleValueProperty* flag = AddFlag();
  Read("user:shift", {"Button"},
       {.read = {.required_modifiers = kModShift},
        .condition = ViewCondition::Config{.property = "user:flag"}});
  Read("user:plain", {"Button"});
  AddSurface();
  SetModifiers(kModShift, true);
  Tap(button_);
  EXPECT_FALSE(shift->GetBool());
  EXPECT_FALSE(plain->GetBool());

  flag->SetBool(true);
  surface_->Run();
  Tap(button_);
  EXPECT_TRUE(shift->GetBool());
  EXPECT_FALSE(plain->GetBool());
}

TEST_F(ViewMappingTest, PressReleaseReleasesOnlyWhatItHeld) {
  TestProperty* property = AddProperty(Type::kToggle, false);
  ToggleValueProperty* flag = AddFlag();
  flag->SetBool(true);
  Read("user:value", {"Button"},
       {.read = {.press_release = true},
        .condition = ViewCondition::Config{.property = "user:flag"}});
  AddSurface();

  // A press it held is released after the condition is no longer met.
  Press(button_);
  surface_->Run();
  EXPECT_TRUE(property->GetBool());
  flag->SetBool(false);
  surface_->Run();
  Release(button_);
  surface_->Run();
  EXPECT_FALSE(property->GetBool());

  // A press while it isn't met isn't held, so its release changes nothing.
  Press(button_);
  surface_->Run();
  EXPECT_FALSE(property->GetBool());
  property->SetBool(true);
  Release(button_);
  surface_->Run();
  EXPECT_TRUE(property->GetBool());
}

//==============================================================================
// Watching what it reads
//==============================================================================

TEST_F(ViewMappingTest, ReadsFromThePropertysValueWatchIt) {
  // A read that changes the property from its value (a delta or a press)
  // watches it, so a polled property is current, and a value never does. Text
  // reads a press only with a range.
  struct Row {
    Type type;
    Value initial;
    bool by_delta;
    bool by_press;
  };
  const std::vector<Row> rows = {
      {Type::kToggle, false, false, true},
      {Type::kPan, 0.0, true, true},
      {Type::kVolume, 0.0, true, true},
      {Type::kNormalized, 0.0, true, true},
      {Type::kEnumerated, 0, true, true},
      {Type::kText, std::string("A"), false, true},
      {Type::kColor, kBlack, true, true},
      {Type::kTimelinePosition, TimelinePosition(0.0), true, false},
  };
  const ViewMapping::ReadConfig read = {.property_min = std::string("A"),
                                        .property_max = std::string("B")};
  struct Readers {
    Reader by_value;
    Reader by_delta;
    Reader by_press;
  };
  std::vector<Readers> readers;
  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    const Row& row = rows[i];
    readers.push_back({
        AddReader(absl::StrCat("value", i), row.type, row.initial,
                  Input::kValue, read),
        AddReader(absl::StrCat("delta", i), row.type, row.initial,
                  Input::kDelta, read),
        AddReader(absl::StrCat("press", i), row.type, row.initial,
                  Input::kPress, read),
    });
  }
  AddSurface();

  for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
    SCOPED_TRACE(i);
    EXPECT_FALSE(readers[i].by_value.property->IsWatched());
    EXPECT_EQ(readers[i].by_delta.property->IsWatched(), rows[i].by_delta);
    EXPECT_EQ(readers[i].by_press.property->IsWatched(), rows[i].by_press);
  }
}

}  // namespace
}  // namespace jpr
