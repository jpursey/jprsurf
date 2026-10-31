// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device_types.h"

#include <algorithm>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device.h"
#include "jpr/device/device_xtouch.h"
#include "jpr/device/testing/fake_control_io.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_doc.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"

namespace jpr {

// Prints a type in test failures by its name. It is in jpr, not the tests'
// namespace, for gtest to find it with the type.
void PrintTo(const DeviceType& type, std::ostream* os) { *os << type.name; }

namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr ControlInput::Type kValue = ControlInput::Type::kValue;
constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValue = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValue = ControlOutput::Type::kDValue;
constexpr ControlOutput::Type kText = ControlOutput::Type::kText;

//==============================================================================
// Device types
//==============================================================================

TEST(DeviceTypesTest, FindsEachTypeByName) {
  const DeviceType* xtouch = FindDeviceType("XTouch");
  ASSERT_NE(xtouch, nullptr);
  EXPECT_EQ(xtouch->name, "XTouch");
  EXPECT_EQ(xtouch->catalog,
            &DeviceXTouch::GetCatalog(DeviceXTouch::Type::kFull));
  const DeviceType* xtouch_ext = FindDeviceType("XTouchExt");
  ASSERT_NE(xtouch_ext, nullptr);
  EXPECT_EQ(xtouch_ext->name, "XTouchExt");
  EXPECT_EQ(xtouch_ext->catalog,
            &DeviceXTouch::GetCatalog(DeviceXTouch::Type::kExtender));

  EXPECT_EQ(FindDeviceType("xtouch"), nullptr);
  EXPECT_EQ(FindDeviceType(""), nullptr);
}

// A control in a catalog: its path, and its shape.
struct CatalogControl {
  std::string path;
  const WidgetShape* shape;
};

// Adds each control of the shape, which is at `path` in its catalog.
void AddCatalogControls(const WidgetShape& shape, const std::string& path,
                        std::vector<CatalogControl>& controls) {
  switch (shape.GetKind()) {
    case WidgetKind::kControl:
      controls.push_back({path, &shape});
      break;
    case WidgetKind::kStruct:
      for (const WidgetShape::Field& field : shape.GetFields()) {
        AddCatalogControls(field.shape, GetFieldPathText(path, field.name),
                           controls);
      }
      break;
    case WidgetKind::kArray:
      for (int i = 0; i < shape.GetSize(); ++i) {
        AddCatalogControls(*shape.GetElement(), GetElementPathText(path, i),
                           controls);
      }
      break;
  }
}

// Each device type, created on a port of the fake REAPER's with nothing
// behind it.
class DeviceTypeTest : public ::testing::TestWithParam<DeviceType> {
 protected:
  std::unique_ptr<Device> Create(
      absl::Span<const ControlOverride> overrides = {}) {
    return CreateDevice(GetParam(), runner_, ports_.OpenInput("Device"),
                        ports_.OpenOutput("Device"), overrides);
  }

  FakeReaper reaper_;

  // The ports, which MidiPorts lists as it is created.
  FakeMidiInput* const input_ = reaper_.AddMidiInput("Device");
  FakeMidiOutput* const output_ = reaper_.AddMidiOutput("Device");

  MidiPorts ports_;
  Runner runner_{"Device"};
};

INSTANTIATE_TEST_SUITE_P(Types, DeviceTypeTest,
                         ::testing::ValuesIn(GetDeviceTypes()),
                         [](const ::testing::TestParamInfo<DeviceType>& info) {
                           return std::string(info.param.name);
                         });

// Building a shape never fails, so a bad or repeated name in a catalog only
// shows up in validating it.
TEST_P(DeviceTypeTest, CatalogIsValid) {
  const WidgetShape* catalog = GetParam().catalog;
  ASSERT_NE(catalog, nullptr);
  EXPECT_EQ(catalog->GetKind(), WidgetKind::kStruct);
  std::vector<std::string> errors;
  EXPECT_TRUE(
      ValidateWidgets({}, {{.name = "device", .catalog = catalog}}, &errors)
          .has_value());
  EXPECT_THAT(errors, IsEmpty());
}

TEST_P(DeviceTypeTest, CatalogIsDocumented) {
  const CatalogDoc* doc = GetParam().catalog_doc;
  ASSERT_NE(doc, nullptr);
  std::vector<std::string> errors;
  EXPECT_TRUE(ValidateCatalogDoc(*GetParam().catalog, *doc, &errors));
  EXPECT_THAT(errors, IsEmpty());
}

TEST_P(DeviceTypeTest, ControlsMatchTheCatalog) {
  std::unique_ptr<Device> device = Create();
  ASSERT_NE(device, nullptr);
  std::vector<std::string> errors;
  EXPECT_TRUE(
      ValidateDeviceControls(*device, *GetParam().catalog, {}, &errors));
  EXPECT_THAT(errors, IsEmpty());
}

// Drops every input of the catalog's first control with inputs, and every
// output of its last control with outputs.
TEST_P(DeviceTypeTest, OverriddenControlsMatchTheCatalog) {
  const WidgetShape& catalog = *GetParam().catalog;
  std::vector<CatalogControl> controls;
  AddCatalogControls(catalog, "", controls);
  auto with_inputs = std::find_if(
      controls.begin(), controls.end(), [](const CatalogControl& control) {
        return !control.shape->GetInputs().IsEmpty();
      });
  auto with_outputs = std::find_if(
      controls.rbegin(), controls.rend(), [](const CatalogControl& control) {
        return !control.shape->GetOutputs().IsEmpty();
      });
  ASSERT_NE(with_inputs, controls.end());
  ASSERT_NE(with_outputs, controls.rend());
  const std::vector<ControlOverride> overrides = {
      {.control = with_inputs->path, .inputs = with_inputs->shape->GetInputs()},
      {.control = with_outputs->path,
       .outputs = with_outputs->shape->GetOutputs()},
  };
  ASSERT_TRUE(ValidateControlOverrides(catalog, overrides, nullptr));
  std::unique_ptr<Device> device = Create(overrides);
  ASSERT_NE(device, nullptr);
  const Control* without_inputs = device->GetControl(with_inputs->path);
  ASSERT_NE(without_inputs, nullptr);
  EXPECT_TRUE(without_inputs->GetInputs().IsEmpty());
  const Control* without_outputs = device->GetControl(with_outputs->path);
  ASSERT_NE(without_outputs, nullptr);
  EXPECT_TRUE(without_outputs->GetOutputs().IsEmpty());
  std::vector<std::string> errors;
  EXPECT_TRUE(ValidateDeviceControls(*device, catalog, overrides, &errors));
  EXPECT_THAT(errors, IsEmpty());
}

//==============================================================================
// ValidateDeviceControls()
//==============================================================================

class ValidateDeviceControlsTest : public ::testing::Test {
 protected:
  // Adds a button with a light that blinks, as the catalog's button is.
  static void AddButton(FakeDevice& device, std::string_view name) {
    device.AddControl({
        .name = name,
        .press_input = std::make_unique<FakePressInput>(),
        .dvalue_output =
            std::make_unique<FakeDValueOutput>(std::vector<int>{1, 1}),
    });
  }

  // Adds every control of the catalog, as it describes them.
  static void AddCatalogControls(FakeDevice& device) {
    AddButton(device, "play");
    for (int strip = 0; strip < 2; ++strip) {
      device.AddFader(absl::StrCat("strip[", strip, "].fader"));
    }
  }

  // Returns the errors validating the device against the catalog adds,
  // checking that it returns whether there are none.
  std::vector<std::string> Validate(
      const Device& device, absl::Span<const ControlOverride> overrides = {}) {
    std::vector<std::string> errors;
    const bool valid =
        ValidateDeviceControls(device, catalog_, overrides, &errors);
    EXPECT_EQ(valid, errors.empty());
    return errors;
  }

  const WidgetShape button_ =
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"});
  const WidgetShape fader_ =
      WidgetShape::ForControl({kValue, kPress}, {kCValue});
  const WidgetShape catalog_ = WidgetShape::ForStruct(
      {{"play", button_},
       {"strip", WidgetShape::ForArray(
                     WidgetShape::ForStruct({{"fader", fader_}}), 2)}});
  Runner runner_{"Controls"};
  FakeDevice device_{runner_};
};

TEST_F(ValidateDeviceControlsTest, DeviceWithTheCatalogsControlsIsValid) {
  AddCatalogControls(device_);
  EXPECT_THAT(Validate(device_), IsEmpty());
}

TEST_F(ValidateDeviceControlsTest, MissingControlsAreErrors) {
  device_.AddFader("strip[1].fader");
  EXPECT_THAT(Validate(device_),
              ElementsAre("play: is missing", "strip[0].fader: is missing"));
}

TEST_F(ValidateDeviceControlsTest, ControlsNotInTheCatalogAreErrors) {
  AddCatalogControls(device_);
  device_.AddButton("stop");
  device_.AddFader("strip[2].fader");
  EXPECT_THAT(Validate(device_),
              ElementsAre("stop: isn't in the catalog",
                          "strip[2].fader: isn't in the catalog"));
}

TEST_F(ValidateDeviceControlsTest, ControlsWithOtherInputsOrOutputsAreErrors) {
  AddButton(device_, "play");
  device_.AddPot("strip[0].fader");
  device_.AddControl({.name = "strip[1].fader"});
  EXPECT_THAT(Validate(device_),
              ElementsAre("strip[0].fader: has inputs delta, not value, press",
                          "strip[1].fader: has inputs none, not value, press",
                          "strip[1].fader: has outputs none, not cvalue"));
}

TEST_F(ValidateDeviceControlsTest, ControlsWithOtherModeCountsAreErrors) {
  device_.AddButton("play");
  device_.AddControl({
      .name = "strip[0].fader",
      .value_input = std::make_unique<FakeValueInput>(),
      .press_input = std::make_unique<FakePressInput>(),
      .cvalue_output = std::make_unique<FakeCValueOutput>(/*mode_count=*/3),
  });
  device_.AddFader("strip[1].fader");
  EXPECT_THAT(Validate(device_),
              ElementsAre("play: has a mode count of 1, not 2",
                          "strip[0].fader: has a mode count of 3, not 1"));
}

TEST_F(ValidateDeviceControlsTest, OverridesAreDroppedFromTheCatalog) {
  const ControlOverride overrides[] = {
      {.control = "strip[1].fader", .inputs = kPress},
  };
  FakeDevice device(runner_, overrides);
  AddCatalogControls(device);
  EXPECT_THAT(Validate(device, overrides), IsEmpty());

  // Without the overrides, the fader is missing its touch.
  EXPECT_THAT(
      Validate(device),
      ElementsAre("strip[1].fader: has inputs value, not value, press"));
}

// Dropping an output can only lower a control's mode count, so it may have
// fewer modes than the catalog names, but not more.
TEST_F(ValidateDeviceControlsTest, ControlsWithoutOutputsMayHaveFewerModes) {
  const ControlOverride overrides[] = {
      {.control = "play", .outputs = kDValue},
  };
  FakeDevice device(runner_, overrides);
  AddCatalogControls(device);
  EXPECT_THAT(Validate(device, overrides), IsEmpty());

  const WidgetShape catalog = WidgetShape::ForStruct(
      {{"display",
        WidgetShape::ForControl({}, {kCValue, kText}, {"one", "two"})}});
  const ControlOverride display_overrides[] = {
      {.control = "display", .outputs = kCValue},
  };
  FakeDevice display_device(runner_, display_overrides);
  display_device.AddControl({
      .name = "display",
      .cvalue_output = std::make_unique<FakeCValueOutput>(/*mode_count=*/2),
      .text_output = std::make_unique<FakeTextOutput>(/*mode_count=*/3),
  });
  std::vector<std::string> errors;
  EXPECT_FALSE(ValidateDeviceControls(display_device, catalog,
                                      display_overrides, &errors));
  EXPECT_THAT(errors, ElementsAre("display: has a mode count of 3, not 2"));
}

TEST_F(ValidateDeviceControlsTest, ErrorsAreOptional) {
  EXPECT_FALSE(ValidateDeviceControls(device_, catalog_, {}, nullptr));
  AddCatalogControls(device_);
  EXPECT_TRUE(ValidateDeviceControls(device_, catalog_, {}, nullptr));
}

}  // namespace
}  // namespace jpr
