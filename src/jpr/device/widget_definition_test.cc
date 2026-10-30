// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_definition.h"

#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/common/runner.h"
#include "jpr/device/control.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/testing/fake_device.h"
#include "jpr/device/widget.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;
using ::testing::StartsWith;

constexpr ControlInput::Type kValue = ControlInput::Type::kValue;
constexpr ControlInput::Type kDelta = ControlInput::Type::kDelta;
constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValue = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValue = ControlOutput::Type::kDValue;

// Returns the names of the struct shape's fields, in order.
std::vector<std::string> GetFieldNames(const WidgetShape& shape) {
  std::vector<std::string> names;
  for (const WidgetShape::Field& field : shape.GetFields()) {
    names.push_back(field.name);
  }
  return names;
}

// Three catalogs: "xtouch" has buttons, a knob, and 8 strips, "ext" has 8
// strips of the same shape, and "other" has 4 strips of another shape.
class WidgetDefinitionTest : public ::testing::Test {
 protected:
  std::optional<WidgetShape> Validate(const WidgetStructDefinition& widgets) {
    errors_.clear();
    return ValidateWidgets(widgets, devices_, &errors_);
  }

  const WidgetShape button_ =
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"});
  const WidgetShape fader_ =
      WidgetShape::ForControl({kValue, kPress}, {kCValue});
  const WidgetShape knob_ = WidgetShape::ForControl({kDelta}, {});
  const WidgetShape strip_ =
      WidgetShape::ForStruct({{"fader", fader_}, {"select", button_}});
  const WidgetShape xtouch_ =
      WidgetShape::ForStruct({{"play", button_},
                              {"stop", button_},
                              {"jog", knob_},
                              {"strip", WidgetShape::ForArray(strip_, 8)}});
  const WidgetShape ext_ =
      WidgetShape::ForStruct({{"strip", WidgetShape::ForArray(strip_, 8)}});
  const WidgetShape other_ = WidgetShape::ForStruct(
      {{"strip", WidgetShape::ForArray(
                     WidgetShape::ForStruct({{"fader", fader_}}), 4)}});
  std::vector<WidgetDevice> devices_ = {
      {"xtouch", &xtouch_}, {"ext", &ext_}, {"other", &other_}};
  std::vector<std::string> errors_;
};

TEST_F(WidgetDefinitionTest, NoWidgetsIsAnEmptyStruct) {
  std::optional<WidgetShape> shape = Validate({});
  ASSERT_TRUE(shape.has_value());
  EXPECT_EQ(shape->GetKind(), WidgetKind::kStruct);
  EXPECT_THAT(shape->GetFields(), IsEmpty());
  EXPECT_THAT(errors_, IsEmpty());
}

TEST_F(WidgetDefinitionTest, PathHasTheCatalogsShapeThere) {
  std::optional<WidgetShape> shape =
      Validate({.fields = {
                    {"play", "xtouch.play"},
                    {"info_strip", "xtouch.strip[7]"},
                    {"ext_strip", "ext.strip"},
                    {"ext", "ext"},
                }});
  ASSERT_TRUE(shape.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_THAT(GetFieldNames(*shape),
              ElementsAre("play", "info_strip", "ext_strip", "ext"));
  EXPECT_EQ(*shape->GetField("play"), button_);
  EXPECT_EQ(*shape->GetField("info_strip"), strip_);
  EXPECT_EQ(*shape->GetField("ext_strip"), WidgetShape::ForArray(strip_, 8));
  EXPECT_EQ(*shape->GetField("ext"), ext_);
}

TEST_F(WidgetDefinitionTest, IncludeAddsItsFieldsApartFromTheExcluded) {
  std::optional<WidgetShape> shape =
      Validate({.includes = {{"xtouch", {"strip", "jog"}}},
                .fields = {{"wheel", "xtouch.jog"}}});
  ASSERT_TRUE(shape.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_THAT(GetFieldNames(*shape), ElementsAre("play", "stop", "wheel"));
  EXPECT_EQ(*shape->GetField("play"), button_);
  EXPECT_EQ(*shape->GetField("wheel"), knob_);
}

TEST_F(WidgetDefinitionTest, StructHasItsIncludesThenItsFields) {
  std::optional<WidgetShape> shape = Validate(
      {.fields = {
           {"transport",
            WidgetStructDefinition{
                .fields = {{"stop", "xtouch.stop"}, {"play", "xtouch.play"}}}},
           {"info",
            WidgetStructDefinition{.includes = {{"xtouch.strip[7]", {"select"}},
                                                {"ext.strip[0]", {"fader"}}},
                                   .fields = {{"jog", "xtouch.jog"}}}},
       }});
  ASSERT_TRUE(shape.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  const WidgetShape* transport = shape->GetField("transport");
  ASSERT_NE(transport, nullptr);
  EXPECT_THAT(GetFieldNames(*transport), ElementsAre("stop", "play"));
  const WidgetShape* info = shape->GetField("info");
  ASSERT_NE(info, nullptr);
  EXPECT_THAT(GetFieldNames(*info), ElementsAre("fader", "select", "jog"));
  EXPECT_EQ(*info->GetField("select"), button_);
}

TEST_F(WidgetDefinitionTest, JoinIsSizedByAllItsSlices) {
  std::optional<WidgetShape> shape = Validate(
      {.fields = {
           {"strip", WidgetJoinDefinition{{{"ext.strip"}, {"xtouch.strip"}}}},
           {"route_strip",
            WidgetJoinDefinition{{{"ext.strip"}, {"xtouch.strip", 0, 7}}}},
           {"middle", WidgetJoinDefinition{{{"ext.strip", 6, 2},
                                            {"xtouch.strip", 0, 2}}}},
       }});
  ASSERT_TRUE(shape.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_EQ(*shape->GetField("strip"), WidgetShape::ForArray(strip_, 16));
  EXPECT_EQ(*shape->GetField("route_strip"), WidgetShape::ForArray(strip_, 15));
  EXPECT_EQ(*shape->GetField("middle"), WidgetShape::ForArray(strip_, 4));
}

TEST_F(WidgetDefinitionTest, SliceWithNoCountTakesTheRestOfItsArray) {
  std::optional<WidgetShape> shape =
      Validate({.fields = {
                    {"last", WidgetJoinDefinition{{{"xtouch.strip", 3}}}},
                }});
  ASSERT_TRUE(shape.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_EQ(*shape->GetField("last"), WidgetShape::ForArray(strip_, 5));
}

TEST_F(WidgetDefinitionTest, DevicesSharingACatalogHaveTheirOwnNames) {
  devices_ = {{"ext_1", &ext_}, {"ext_2", &ext_}, {"xtouch", &xtouch_}};
  std::optional<WidgetShape> shape =
      Validate({.fields = {
                    {"strip",
                     WidgetJoinDefinition{
                         {{"ext_1.strip"}, {"ext_2.strip"}, {"xtouch.strip"}}}},
                    {"first_strip", "ext_1.strip[0]"},
                }});
  ASSERT_TRUE(shape.has_value());
  EXPECT_THAT(errors_, IsEmpty());
  EXPECT_EQ(*shape->GetField("strip"), WidgetShape::ForArray(strip_, 24));
  EXPECT_EQ(*shape->GetField("first_strip"), strip_);
}

TEST_F(WidgetDefinitionTest, ReportsBadDevices) {
  devices_ = {{"1x", &ext_},
              {"xtouch", &xtouch_},
              {"xtouch", &ext_},
              {"none", nullptr},
              {"knob", &knob_}};
  EXPECT_FALSE(Validate({}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("device name \"1x\" isn't a widget name",
                          "device \"xtouch\" is repeated",
                          "device \"none\" has no catalog",
                          "catalog of device \"knob\" isn't a struct"));
}

TEST_F(WidgetDefinitionTest, PathsFromABadDeviceAreNotReportedAgain) {
  devices_.push_back({"none", nullptr});
  devices_.push_back({"knob", &knob_});
  EXPECT_FALSE(
      Validate({.includes = {{"knob"}},
                .fields = {{"gone", "none.play"},
                           {"turn", WidgetJoinDefinition{{{"knob"}}}}}})
          .has_value());
  EXPECT_THAT(errors_,
              ElementsAre("device \"none\" has no catalog",
                          "catalog of device \"knob\" isn't a struct"));
}

TEST_F(WidgetDefinitionTest, RepeatedDeviceStillHasItsCatalogValidated) {
  const WidgetShape bad =
      WidgetShape::ForStruct({{"play", button_}, {"play", button_}});
  devices_ = {{"xtouch", &xtouch_}, {"xtouch", &bad}};
  // Paths from the name are from the first device.
  EXPECT_FALSE(Validate({.fields = {{"jog", "xtouch.jog"}}}).has_value());
  EXPECT_THAT(errors_, ElementsAre("device \"xtouch\" is repeated",
                                   "xtouch: field \"play\" is repeated"));
}

TEST_F(WidgetDefinitionTest, IncludeOfARepeatedCatalogFieldIsOnlyReportedOnce) {
  const WidgetShape bad = WidgetShape::ForStruct(
      {{"play", button_}, {"stop", button_}, {"play", fader_}});
  devices_ = {{"bad", &bad}};
  EXPECT_FALSE(Validate({.includes = {{"bad"}}}).has_value());
  EXPECT_THAT(errors_, ElementsAre("bad: field \"play\" is repeated"));
}

TEST_F(WidgetDefinitionTest, ReportsAJoinTooLargeForAnArray) {
  const WidgetShape huge = WidgetShape::ForStruct(
      {{"strip",
        WidgetShape::ForArray(strip_, std::numeric_limits<int>::max())}});
  devices_ = {{"huge", &huge}};
  EXPECT_FALSE(
      Validate({.fields = {{"strip", WidgetJoinDefinition{{{"huge.strip"},
                                                           {"huge.strip"}}}}}})
          .has_value());
  EXPECT_THAT(errors_,
              ElementsAre("strip: join has 4294967294 elements, more than "
                          "2147483647"));
}

TEST_F(WidgetDefinitionTest, ReportsBadNamesInACatalogAtAnyDepth) {
  const WidgetShape bad = WidgetShape::ForStruct({
      {"play",
       WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "solid", "a-b"})},
      {"strip",
       WidgetShape::ForArray(
           WidgetShape::ForStruct(
               {{"fader", fader_}, {"fader", fader_}, {"1st", button_}}),
           2)},
      {"play", button_},
  });
  // A catalog shared by two devices is only reported once.
  devices_ = {{"bad", &bad}, {"bad_too", &bad}};
  EXPECT_FALSE(Validate({}).has_value());
  EXPECT_THAT(errors_,
              ElementsAre("bad: play: mode \"solid\" is repeated",
                          "bad: play: mode name \"a-b\" isn't a widget name",
                          "bad: strip[0]: field \"fader\" is repeated",
                          "bad: strip[0]: field name \"1st\" isn't a widget "
                          "name",
                          "bad: field \"play\" is repeated"));
}

TEST_F(WidgetDefinitionTest, ReportsBadPaths) {
  EXPECT_FALSE(Validate({.fields =
                             {
                                 {"a", "xtouch.strip."},
                                 {"b", "nowhere.play"},
                                 {"c", "[0].play"},
                                 {"d", ""},
                                 {"e", "xtouch.paly"},
                                 {"f", "xtouch.strip[8]"},
                                 {"g", "xtouch.play.solid"},
                             }})
                   .has_value());
  EXPECT_THAT(
      errors_,
      ElementsAre(StartsWith("a: path \"xtouch.strip.\" isn't a path: "),
                  "b: path \"nowhere.play\" doesn't start at a device",
                  "c: path \"[0].play\" doesn't start at a device",
                  "d: path \"\" doesn't start at a device",
                  "e: path \"xtouch.paly\" names nothing",
                  "f: path \"xtouch.strip[8]\" names nothing",
                  "g: path \"xtouch.play.solid\" names nothing"));
}

TEST_F(WidgetDefinitionTest, ReportsBadAndRepeatedFieldNames) {
  EXPECT_FALSE(Validate({.includes = {{"xtouch", {"strip"}}},
                         .fields =
                             {
                                 {"1st", "xtouch.play"},
                                 {"play", "xtouch.play"},
                                 {"transport",
                                  WidgetStructDefinition{
                                      .fields = {{"play", "xtouch.play"},
                                                 {"play", "xtouch.stop"}}}},
                                 {"x", "xtouch.nope"},
                                 {"x", "xtouch.play"},
                             }})
                   .has_value());
  EXPECT_THAT(errors_, ElementsAre("field name \"1st\" isn't a widget name",
                                   "field \"play\" is repeated",
                                   "transport: field \"play\" is repeated",
                                   "x: path \"xtouch.nope\" names nothing",
                                   "field \"x\" is repeated"));
}

TEST_F(WidgetDefinitionTest, ReportsBadIncludes) {
  EXPECT_FALSE(
      Validate({.includes = {{"xtouch.nope"}},
                .fields = {{"t",
                            WidgetStructDefinition{
                                .includes = {{"xtouch.play"},
                                             {"xtouch", {"strip", "rewind"}},
                                             {"xtouch.strip"}}}}}})
          .has_value());
  EXPECT_THAT(
      errors_,
      ElementsAre(
          "include \"xtouch.nope\" names nothing",
          "t: include \"xtouch.play\" isn't a struct",
          "t: include \"xtouch\" excludes \"rewind\", which it doesn't have",
          "t: include \"xtouch.strip\" isn't a struct"));
}

TEST_F(WidgetDefinitionTest, ReportsBadJoins) {
  EXPECT_FALSE(
      Validate(
          {.fields =
               {
                   {"a", WidgetJoinDefinition{}},
                   {"b", WidgetJoinDefinition{{{"xtouch.play"}}}},
                   {"c", WidgetJoinDefinition{{{"xtouch.strip", 6, 3}}}},
                   {"d", WidgetJoinDefinition{{{"xtouch.strip", -1}}}},
                   {"e", WidgetJoinDefinition{{{"xtouch.strip", 9}}}},
                   {"f",
                    WidgetJoinDefinition{{{"xtouch.strip", 0,
                                           std::numeric_limits<int>::max()}}}},
                   {"g", WidgetJoinDefinition{{{"xtouch.strip", 8}}}},
                   {"h", WidgetJoinDefinition{{{"xtouch.strip", 2, 0}}}},
                   {"i", WidgetJoinDefinition{{{"xtouch.strip", 2, -1}}}},
                   {"j", WidgetJoinDefinition{{{"xtouch.nope"}}}},
                   {"k",
                    WidgetJoinDefinition{
                        {{"xtouch.strip"}, {"ext.strip"}, {"other.strip"}}}},
               }})
          .has_value());
  EXPECT_THAT(
      errors_,
      ElementsAre(
          "a: join has no slices",
          "b: slice \"xtouch.play\" from 0 isn't an array",
          "c: slice \"xtouch.strip\" from 6 for 3 runs past its 8 elements",
          "d: slice \"xtouch.strip\" from -1 runs past its 8 elements",
          "e: slice \"xtouch.strip\" from 9 runs past its 8 elements",
          "f: slice \"xtouch.strip\" from 0 for 2147483647 runs past its 8 "
          "elements",
          "g: slice \"xtouch.strip\" from 8 is empty",
          "h: slice \"xtouch.strip\" from 2 for 0 is empty",
          "i: slice \"xtouch.strip\" from 2 for -1 is empty",
          "j: slice \"xtouch.nope\" names nothing",
          "k: slice \"other.strip\" from 0 has elements of another shape than "
          "slice \"xtouch.strip\" from 0"));
}

TEST_F(WidgetDefinitionTest, ReportsEveryProblemInOneValidation) {
  devices_.push_back({"none", nullptr});
  EXPECT_FALSE(Validate({.fields =
                             {
                                 {"play", "xtouch.paly"},
                                 {"strip", WidgetJoinDefinition{}},
                                 {"stop", "xtouch.stop"},
                                 {"gone", "none.play"},
                             }})
                   .has_value());
  // A path from a device without a catalog only reports the device.
  EXPECT_THAT(errors_, ElementsAre("device \"none\" has no catalog",
                                   "play: path \"xtouch.paly\" names nothing",
                                   "strip: join has no slices"));
}

TEST_F(WidgetDefinitionTest, ErrorsAreAddedToThoseAlreadyThere) {
  errors_ = {"earlier"};
  EXPECT_FALSE(
      ValidateWidgets({.fields = {{"play", "xtouch.paly"}}}, devices_, &errors_)
          .has_value());
  EXPECT_THAT(errors_, ElementsAre("earlier",
                                   "play: path \"xtouch.paly\" names nothing"));
}

TEST_F(WidgetDefinitionTest, ErrorsAreOptional) {
  EXPECT_FALSE(
      ValidateWidgets({.fields = {{"play", "xtouch.paly"}}}, devices_, nullptr)
          .has_value());
  EXPECT_TRUE(
      ValidateWidgets({.fields = {{"play", "xtouch.play"}}}, devices_, nullptr)
          .has_value());
}

// The fixture's catalogs on fake devices, whose controls are named by their
// catalog paths.
class ResolveWidgetsTest : public WidgetDefinitionTest {
 protected:
  ResolveWidgetsTest() {
    xtouch_device_.AddButton("play");
    xtouch_device_.AddButton("stop");
    xtouch_device_.AddPot("jog");
    AddStrips(xtouch_device_);
    AddStrips(ext_1_device_);
    AddStrips(ext_2_device_);
    devices_ = {{"ext_1", &ext_, &ext_1_device_},
                {"ext_2", &ext_, &ext_2_device_},
                {"xtouch", &xtouch_, &xtouch_device_}};
  }

  static void AddStrips(FakeDevice& device) {
    for (int i = 0; i < 8; ++i) {
      device.AddFader(absl::StrCat("strip[", i, "].fader"));
      device.AddButton(absl::StrCat("strip[", i, "].select"));
    }
  }

  std::unique_ptr<const Widget> Resolve(const WidgetStructDefinition& widgets) {
    errors_.clear();
    return ResolveWidgets(widgets, devices_, &errors_);
  }

  // Returns the path, failing the test if the text isn't one.
  static WidgetPath Path(std::string_view text) {
    std::optional<WidgetPath> path = WidgetPath::Parse(text);
    EXPECT_TRUE(path.has_value()) << text;
    return path.value_or(WidgetPath());
  }

  // Returns the control of the widget at the path, failing the test if there
  // is no widget there.
  static Control* GetControlAt(const Widget& widget, std::string_view text) {
    const Widget* found = widget.Find(Path(text));
    EXPECT_NE(found, nullptr) << text;
    return found != nullptr ? found->GetControl() : nullptr;
  }

  // Returns the number of elements of the array widget at the path, failing
  // the test if there is no widget there.
  static int GetSizeAt(const Widget& widget, std::string_view text) {
    const Widget* found = widget.Find(Path(text));
    EXPECT_NE(found, nullptr) << text;
    return found != nullptr ? static_cast<int>(std::ssize(found->GetElements()))
                            : -1;
  }

  // The definitions of JPRSurf's own widgets, on two extenders.
  const WidgetStructDefinition widgets_ = {
      .includes = {{"xtouch", {"strip"}}},
      .fields = {
          {"strip",
           WidgetJoinDefinition{
               {{"ext_1.strip"}, {"ext_2.strip"}, {"xtouch.strip"}}}},
          {"route_strip",
           WidgetJoinDefinition{{{"ext_1.strip", 2}, {"xtouch.strip", 0, 7}}}},
          {"info_strip", "xtouch.strip[7]"},
          {"transport",
           WidgetStructDefinition{.includes = {{"ext_2.strip[0]", {"select"}}},
                                  .fields = {{"stop", "xtouch.stop"}}}},
      }};

  Runner runner_{"Controls"};
  FakeDevice xtouch_device_{runner_};
  FakeDevice ext_1_device_{runner_};
  FakeDevice ext_2_device_{runner_};
};

TEST_F(ResolveWidgetsTest, ControlsAreTheDevicesControls) {
  std::unique_ptr<const Widget> widgets = Resolve(widgets_);
  ASSERT_NE(widgets, nullptr);
  EXPECT_THAT(errors_, IsEmpty());

  EXPECT_EQ(GetControlAt(*widgets, "play"), xtouch_device_.GetControl("play"));
  EXPECT_EQ(GetControlAt(*widgets, "jog"), xtouch_device_.GetControl("jog"));
  EXPECT_EQ(GetControlAt(*widgets, "info_strip.select"),
            xtouch_device_.GetControl("strip[7].select"));
  EXPECT_EQ(GetControlAt(*widgets, "transport.stop"),
            xtouch_device_.GetControl("stop"));
  EXPECT_EQ(GetControlAt(*widgets, "transport.fader"),
            ext_2_device_.GetControl("strip[0].fader"));
  EXPECT_EQ(widgets->Find(Path("strip[0].fader")),
            widgets->GetField("strip")->GetElement(0)->GetField("fader"));
}

TEST_F(ResolveWidgetsTest, JoinsHaveTheirSlicesElementsInOrder) {
  std::unique_ptr<const Widget> widgets = Resolve(widgets_);
  ASSERT_NE(widgets, nullptr);

  EXPECT_EQ(GetSizeAt(*widgets, "strip"), 24);
  EXPECT_EQ(GetControlAt(*widgets, "strip[0].fader"),
            ext_1_device_.GetControl("strip[0].fader"));
  EXPECT_EQ(GetControlAt(*widgets, "strip[8].fader"),
            ext_2_device_.GetControl("strip[0].fader"));
  EXPECT_EQ(GetControlAt(*widgets, "strip[23].select"),
            xtouch_device_.GetControl("strip[7].select"));

  EXPECT_EQ(GetSizeAt(*widgets, "route_strip"), 13);
  EXPECT_EQ(GetControlAt(*widgets, "route_strip[0].fader"),
            ext_1_device_.GetControl("strip[2].fader"));
  EXPECT_EQ(GetControlAt(*widgets, "route_strip[12].fader"),
            xtouch_device_.GetControl("strip[6].fader"));

  // From an element.
  const Widget* element = widgets->GetField("strip")->GetElement(16);
  ASSERT_NE(element, nullptr);
  EXPECT_EQ(GetControlAt(*element, "fader"),
            xtouch_device_.GetControl("strip[0].fader"));
}

TEST_F(ResolveWidgetsTest, AbsentDeviceHasNullControlsAndEmptyArrays) {
  devices_[2].device = nullptr;  // xtouch
  std::unique_ptr<const Widget> widgets =
      Resolve({.includes = widgets_.includes,
               .fields = {widgets_.fields[0],
                          widgets_.fields[2],
                          {"xtouch_strip", "xtouch.strip"},
                          {"xtouch_strip_2", "xtouch.strip[2]"}}});
  ASSERT_NE(widgets, nullptr);

  EXPECT_EQ(GetControlAt(*widgets, "play"), nullptr);
  EXPECT_EQ(GetControlAt(*widgets, "info_strip.fader"), nullptr);
  EXPECT_EQ(GetControlAt(*widgets, "xtouch_strip_2.select"), nullptr);
  EXPECT_EQ(GetSizeAt(*widgets, "xtouch_strip"), 0);

  EXPECT_EQ(GetSizeAt(*widgets, "strip"), 16);
  EXPECT_EQ(GetControlAt(*widgets, "strip[15].fader"),
            ext_2_device_.GetControl("strip[7].fader"));
  EXPECT_EQ(widgets->Find(Path("strip[16]")), nullptr);
}

TEST_F(ResolveWidgetsTest, DevicesSharingACatalogHaveTheirOwnControls) {
  devices_[0].device = nullptr;  // ext_1
  std::unique_ptr<const Widget> widgets = Resolve(widgets_);
  ASSERT_NE(widgets, nullptr);
  EXPECT_EQ(GetSizeAt(*widgets, "strip"), 16);
  EXPECT_EQ(GetControlAt(*widgets, "strip[0].fader"),
            ext_2_device_.GetControl("strip[0].fader"));
  EXPECT_EQ(GetControlAt(*widgets, "strip[8].fader"),
            xtouch_device_.GetControl("strip[0].fader"));
  EXPECT_EQ(GetSizeAt(*widgets, "route_strip"), 7);

  devices_[0].device = &ext_1_device_;
  devices_[1].device = nullptr;  // ext_2
  widgets = Resolve(widgets_);
  ASSERT_NE(widgets, nullptr);
  EXPECT_EQ(GetSizeAt(*widgets, "strip"), 16);
  EXPECT_EQ(GetControlAt(*widgets, "strip[7].fader"),
            ext_1_device_.GetControl("strip[7].fader"));
  EXPECT_EQ(GetControlAt(*widgets, "strip[8].fader"),
            xtouch_device_.GetControl("strip[0].fader"));
  EXPECT_EQ(GetControlAt(*widgets, "transport.fader"), nullptr);
}

TEST_F(ResolveWidgetsTest, PresentDeviceWithoutACatalogControlHasNull) {
  FakeDevice device(runner_);
  device.AddButton("play");
  devices_[2].device = &device;
  std::unique_ptr<const Widget> widgets = Resolve(widgets_);
  ASSERT_NE(widgets, nullptr);
  EXPECT_EQ(GetControlAt(*widgets, "play"), device.GetControl("play"));
  EXPECT_EQ(GetControlAt(*widgets, "stop"), nullptr);
  // A present device has every element of its arrays.
  EXPECT_EQ(GetSizeAt(*widgets, "strip"), 24);
  EXPECT_EQ(GetControlAt(*widgets, "strip[16].fader"), nullptr);
}

TEST_F(ResolveWidgetsTest, InvalidDefinitionHasItsErrors) {
  EXPECT_EQ(Resolve({.fields = {{"play", "xtouch.paly"}}}), nullptr);
  EXPECT_THAT(errors_, ElementsAre("play: path \"xtouch.paly\" names nothing"));
}

}  // namespace
}  // namespace jpr
