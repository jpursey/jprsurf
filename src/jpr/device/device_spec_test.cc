// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/device_spec.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/common/testing/fake_midi.h"
#include "jpr/common/testing/fake_reaper.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device.h"
#include "jpr/device/device_types.h"
#include "jpr/device/testing/fake_xtouch.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_shape.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::IsNull;
using ::testing::NotNull;
using ::testing::SizeIs;

using Light = FakeXTouch::Light;
using StripButton = FakeXTouch::StripButton;

constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlInput::Type kDelta = ControlInput::Type::kDelta;

// The fake REAPER's MIDI ports, with an X-Touch on "X-Touch", and an extender
// on "X-Touch-Ext" once it is added.
class CreateDevicesTest : public ::testing::Test {
 protected:
  // Lists the extender's ports, which MidiPorts lists as it is created.
  void AddExtender() {
    xtouch_ext_.emplace(reaper_, FakeXTouch::Type::kExtender, "X-Touch-Ext");
  }

  // Returns the devices of the specs, and sets `errors_` to the errors.
  std::optional<DeviceList> Create(const std::vector<DeviceSpec>& specs) {
    ports_.emplace();
    errors_.clear();
    return CreateDevices(specs, *ports_, runner_, &errors_);
  }

  // Runs the devices, then MIDI output, as PluginSurface::OnRun() does.
  void Run() {
    reaper_.AdvanceTime(FakeReaper::GetRunTime());
    const RunTime time = RunTime::Now();
    runner_.Run(time);
    ports_->RunOutput(time);
  }

  // Returns the catalog of the type.
  static const WidgetShape* GetCatalog(std::string_view type) {
    const DeviceType* device_type = FindDeviceType(type);
    return device_type != nullptr ? device_type->catalog : nullptr;
  }

  // JPRSurf's own devices.
  const DeviceSpec xtouch_spec_ = {.name = "xtouch",
                                   .type = "XTouch",
                                   .input_port = "X-Touch",
                                   .output_port = "X-Touch"};
  const DeviceSpec xtouch_ext_spec_ = {
      .name = "xtouch_ext",
      .type = "XTouchExt",
      .input_port = "X-Touch-Ext",
      .output_port = "X-Touch-Ext",
      .required = false,
      .overrides = {{.control = "strip[2].fader", .inputs = kPress}}};

  FakeReaper reaper_;
  FakeXTouch xtouch_{reaper_, FakeXTouch::Type::kFull, "X-Touch"};
  std::optional<FakeXTouch> xtouch_ext_;
  Runner runner_{"Devices"};
  std::optional<MidiPorts> ports_;
  std::vector<std::string> errors_;
};

TEST_F(CreateDevicesTest, DevicesPresentAreCreatedOnTheirPorts) {
  AddExtender();
  std::optional<DeviceList> list = Create({xtouch_spec_, xtouch_ext_spec_});
  ASSERT_TRUE(list.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  ASSERT_THAT(list->devices, SizeIs(2));
  ASSERT_THAT(list->widget_devices, SizeIs(2));
  Device* xtouch = list->devices[0].get();
  Device* xtouch_ext = list->devices[1].get();
  EXPECT_EQ(list->widget_devices[0].name, "xtouch");
  EXPECT_EQ(list->widget_devices[0].catalog, GetCatalog("XTouch"));
  EXPECT_EQ(list->widget_devices[0].device, xtouch);
  EXPECT_EQ(list->widget_devices[1].name, "xtouch_ext");
  EXPECT_EQ(list->widget_devices[1].catalog, GetCatalog("XTouchExt"));
  EXPECT_EQ(list->widget_devices[1].device, xtouch_ext);

  // Each is of its type, with its overrides.
  EXPECT_THAT(xtouch->GetControl("play"), NotNull());
  EXPECT_THAT(xtouch_ext->GetControl("play"), IsNull());
  Control* ext_fader = xtouch_ext->GetControl("strip[2].fader");
  ASSERT_THAT(ext_fader, NotNull());
  EXPECT_FALSE(ext_fader->GetInputs().IsSet(kPress));

  // Each is on its ports.
  Control* xtouch_select = xtouch->GetControl("strip[0].select");
  Control* ext_select = xtouch_ext->GetControl("strip[1].select");
  ASSERT_THAT(xtouch_select, NotNull());
  ASSERT_THAT(ext_select, NotNull());
  xtouch_select->SetDValue(1);
  ext_select->SetDValue(1);
  Run();
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 0), Light::kOn);
  EXPECT_EQ(xtouch_.GetLight(StripButton::kSelect, 1), Light::kOff);
  EXPECT_EQ(xtouch_ext_->GetLight(StripButton::kSelect, 0), Light::kOff);
  EXPECT_EQ(xtouch_ext_->GetLight(StripButton::kSelect, 1), Light::kOn);
}

TEST_F(CreateDevicesTest, OptionalDeviceAbsentHasNoDevice) {
  std::optional<DeviceList> list = Create({xtouch_ext_spec_, xtouch_spec_});
  ASSERT_TRUE(list.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  ASSERT_THAT(list->devices, SizeIs(1));
  ASSERT_THAT(list->widget_devices, SizeIs(2));
  EXPECT_EQ(list->widget_devices[0].name, "xtouch_ext");
  EXPECT_EQ(list->widget_devices[0].catalog, GetCatalog("XTouchExt"));
  EXPECT_THAT(list->widget_devices[0].device, IsNull());
  EXPECT_EQ(list->widget_devices[1].device, list->devices[0].get());
}

// A device with one of its ports found is absent, and leaves that port closed.
TEST_F(CreateDevicesTest, OptionalDeviceWithOnePortHasNoDevice) {
  FakeMidiInput* ext_input = reaper_.AddMidiInput("X-Touch-Ext");
  std::optional<DeviceList> list = Create({xtouch_spec_, xtouch_ext_spec_});
  ASSERT_TRUE(list.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_THAT(list->devices, SizeIs(1));
  ASSERT_THAT(list->widget_devices, SizeIs(2));
  EXPECT_THAT(list->widget_devices[1].device, IsNull());
  EXPECT_FALSE(ext_input->IsOpen());
}

// A required device absent leaves every port closed.
TEST_F(CreateDevicesTest, RequiredDeviceAbsentCreatesNone) {
  DeviceSpec required_ext = xtouch_ext_spec_;
  required_ext.required = true;
  EXPECT_FALSE(Create({xtouch_spec_, required_ext}).has_value());
  EXPECT_THAT(errors_, ElementsAre("xtouch_ext: is required, but its input "
                                   "port \"X-Touch-Ext\" isn't found",
                                   "xtouch_ext: is required, but its output "
                                   "port \"X-Touch-Ext\" isn't found"));
  EXPECT_FALSE(xtouch_.GetOutputPort()->IsOpen());

  reaper_.AddMidiOutput("X-Touch-Ext");
  EXPECT_FALSE(Create({xtouch_spec_, required_ext}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("xtouch_ext: is required, but its input port "
                          "\"X-Touch-Ext\" isn't found"));
  EXPECT_FALSE(xtouch_.GetOutputPort()->IsOpen());
}

TEST_F(CreateDevicesTest, OptionalDeviceWhosePortFailsToOpenHasNoDevice) {
  AddExtender();
  xtouch_ext_->GetOutputPort()->SetOpenFails(true);
  std::optional<DeviceList> list = Create({xtouch_spec_, xtouch_ext_spec_});
  ASSERT_TRUE(list.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_THAT(list->devices, SizeIs(1));
  ASSERT_THAT(list->widget_devices, SizeIs(2));
  EXPECT_THAT(list->widget_devices[1].device, IsNull());
}

TEST_F(CreateDevicesTest, RequiredDeviceWhosePortFailsToOpenCreatesNone) {
  AddExtender();
  xtouch_ext_->GetInputPort()->SetOpenFails(true);
  DeviceSpec required_ext = xtouch_ext_spec_;
  required_ext.required = true;
  EXPECT_FALSE(Create({xtouch_spec_, required_ext}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("xtouch_ext: is required, but its ports failed to "
                          "open"));
}

TEST_F(CreateDevicesTest, SharedPortsCreateNone) {
  AddExtender();
  DeviceSpec input_shared = xtouch_ext_spec_;
  input_shared.input_port = "X-Touch";
  DeviceSpec output_shared = xtouch_ext_spec_;
  output_shared.name = "xtouch_ext2";
  output_shared.input_port = "Keys";
  output_shared.output_port = "X-Touch-Ext";
  EXPECT_FALSE(Create({xtouch_spec_, input_shared, output_shared}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("xtouch_ext: input port \"X-Touch\" is also xtouch's",
                          "xtouch_ext2: output port \"X-Touch-Ext\" is also "
                          "xtouch_ext's"));
  EXPECT_FALSE(xtouch_.GetOutputPort()->IsOpen());
}

TEST_F(CreateDevicesTest, InvalidNamesCreateNone) {
  // Each on its own ports, so that only the names are wrong.
  DeviceSpec unnamed = {
      .name = "", .type = "XTouchExt", .input_port = "A", .output_port = "A"};
  DeviceSpec spaced = {.name = "xtouch ext",
                       .type = "XTouchExt",
                       .input_port = "B",
                       .output_port = "B"};
  DeviceSpec repeated = {.name = "xtouch",
                         .type = "XTouchExt",
                         .input_port = "C",
                         .output_port = "C"};
  EXPECT_FALSE(Create({xtouch_spec_, unnamed, spaced, repeated}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("device name \"\" isn't a widget name",
                          "device name \"xtouch ext\" isn't a widget name",
                          "device \"xtouch\" is repeated"));
  EXPECT_FALSE(xtouch_.GetOutputPort()->IsOpen());
}

TEST_F(CreateDevicesTest, InvalidTypeCreatesNone) {
  DeviceSpec misspelled = xtouch_ext_spec_;
  misspelled.type = "XTouh";
  EXPECT_FALSE(Create({xtouch_spec_, misspelled}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("xtouch_ext: type \"XTouh\" isn't a device type"));
  EXPECT_FALSE(xtouch_.GetOutputPort()->IsOpen());
}

// The name of a spec whose type isn't a device type is still validated.
TEST_F(CreateDevicesTest, InvalidTypeStillHasItsNameValidated) {
  DeviceSpec misspelled = xtouch_ext_spec_;
  misspelled.name = "xtouch";
  misspelled.type = "XTouh";
  EXPECT_FALSE(Create({xtouch_spec_, misspelled}).has_value());
  EXPECT_THAT(errors_, ElementsAre("xtouch: type \"XTouh\" isn't a device type",
                                   "device \"xtouch\" is repeated"));
}

TEST_F(CreateDevicesTest, InvalidOverridesCreateNone) {
  DeviceSpec overridden = xtouch_ext_spec_;
  overridden.overrides = {{.control = "strip[2].fader", .inputs = kDelta},
                          {.control = "play", .inputs = kPress}};
  EXPECT_FALSE(Create({xtouch_spec_, overridden}).has_value());
  EXPECT_THAT(errors_, ElementsAre("xtouch_ext: strip[2].fader: has no delta "
                                   "input",
                                   "xtouch_ext: play: names nothing"));
  EXPECT_FALSE(xtouch_.GetOutputPort()->IsOpen());
}

TEST_F(CreateDevicesTest, ErrorsAreOptional) {
  DeviceSpec required_ext = xtouch_ext_spec_;
  required_ext.required = true;
  ports_.emplace();
  EXPECT_FALSE(
      CreateDevices({required_ext}, *ports_, runner_, nullptr).has_value());
  EXPECT_TRUE(
      CreateDevices({xtouch_spec_}, *ports_, runner_, nullptr).has_value());
}

}  // namespace
}  // namespace jpr
