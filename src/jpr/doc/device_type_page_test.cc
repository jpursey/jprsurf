// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/doc/device_type_page.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/no_destructor.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/device_types.h"
#include "jpr/device/widget_doc.h"
#include "jpr/device/widget_shape.h"

namespace jpr {
namespace {

using ::testing::HasSubstr;
using ::testing::UnorderedElementsAreArray;

constexpr ControlInput::Type kValue = ControlInput::Type::kValue;
constexpr ControlInput::Type kDelta = ControlInput::Type::kDelta;
constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValue = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValue = ControlOutput::Type::kDValue;

constexpr WidgetDoc::Mode kLightModes[] = {{"solid", "Lit."},
                                           {"blink", "Blinking."}};
constexpr WidgetDoc::Mode kRingModes[] = {{"dot", "One light."},
                                          {"off", "No lights."}};

// Returns the shape of a button with a light.
const WidgetShape& GetButtonShape() {
  static const absl::NoDestructor<WidgetShape> s_button(
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"}));
  return *s_button;
}

// Returns the doc of a button, with a light's modes.
WidgetDoc GetButtonDoc(std::string_view path, std::string_view description) {
  return {.path = path, .description = description, .modes = kLightModes};
}

TEST(GenerateDeviceTypePageTest, ListsTheCatalogAndItsModes) {
  const WidgetShape catalog = WidgetShape::ForStruct({
      {"play", GetButtonShape()},
      {"function", WidgetShape::ForArray(GetButtonShape(), 2)},
      {"fader", WidgetShape::ForControl({kValue, kPress}, {kCValue})},
      {"strip", WidgetShape::ForArray(
                    WidgetShape::ForStruct({
                        {"pot", WidgetShape::ForControl({kDelta}, {kDValue},
                                                        {"dot", "off"})},
                        {"select", GetButtonShape()},
                    }),
                    2)},
  });
  const CatalogDoc doc = {
      .description = "A unit.",
      .widgets = {
          GetButtonDoc("play", "Play."),
          GetButtonDoc("function", "The keys."),
          {.path = "fader", .description = "Up | down.\nMotorized."},
          {.path = "strip", .description = "The strips."},
          {.path = "strip.pot", .description = "A pot.", .modes = kRingModes},
          GetButtonDoc("strip.select", "Select."),
      }};
  const DeviceType type = {
      .name = "Test", .catalog = &catalog, .catalog_doc = &doc};

  EXPECT_EQ(
      GenerateDeviceTypePage(type),
      R"(<!-- Generated from the Test catalog by the update_device_docs target. Don't edit. -->

# Test

A unit.

## Widgets

| Widget | Inputs | Outputs | Modes | Description |
| --- | --- | --- | --- | --- |
| `play` | press | dvalue | `solid`, `blink` | Play. |
| `function[0..1]` | press | dvalue | `solid`, `blink` | The keys. |
| `fader` | value, press | cvalue |  | Up \| down. Motorized. |
| `strip[0..1]` |  |  |  | The strips. |
| `strip[i].pot` | delta | dvalue | `dot`, `off` | A pot. |
| `strip[i].select` | press | dvalue | `solid`, `blink` | Select. |

## Modes

### `solid`, `blink`

The modes of `play`, `function[i]`, `strip[i].select`.

| Mode | Description |
| --- | --- |
| `solid` | Lit. |
| `blink` | Blinking. |

### `dot`, `off`

The modes of `strip[i].pot`.

| Mode | Description |
| --- | --- |
| `dot` | One light. |
| `off` | No lights. |
)");
}

TEST(GenerateDeviceTypePageTest, ListsNestedWidgetsByEachIndex) {
  const WidgetShape catalog = WidgetShape::ForStruct({
      {"grid",
       WidgetShape::ForArray(WidgetShape::ForArray(GetButtonShape(), 2), 3)},
      {"section", WidgetShape::ForStruct({{"pad", GetButtonShape()}})},
  });
  const CatalogDoc doc = {.description = "A unit.",
                          .widgets = {
                              GetButtonDoc("grid", "The grid."),
                              {.path = "section", .description = "A section."},
                              GetButtonDoc("section.pad", "A pad."),
                          }};
  const DeviceType type = {
      .name = "Test", .catalog = &catalog, .catalog_doc = &doc};

  const std::string page = GenerateDeviceTypePage(type);
  EXPECT_THAT(page, HasSubstr("| `grid[0..2][0..1]` | press | dvalue | "
                              "`solid`, `blink` | The grid. |\n"
                              "| `section` |  |  |  | A section. |\n"
                              "| `section.pad` | press | dvalue | "));
  EXPECT_THAT(page, HasSubstr("The modes of `grid[i][j]`, `section.pad`."));
}

TEST(GenerateDeviceTypePageTest, ListsEmptyArraysWithNoRange) {
  const WidgetShape catalog = WidgetShape::ForStruct({
      {"pads", WidgetShape::ForArray(GetButtonShape(), 0)},
  });
  const CatalogDoc doc = {.description = "A unit.",
                          .widgets = {GetButtonDoc("pads", "No pads.")}};
  const DeviceType type = {
      .name = "Test", .catalog = &catalog, .catalog_doc = &doc};

  EXPECT_THAT(GenerateDeviceTypePage(type),
              HasSubstr("| `pads[]` | press | dvalue | "));
}

TEST(GenerateDeviceTypePageTest, OnlyModesInOneArrayAreShared) {
  const WidgetShape catalog = WidgetShape::ForStruct({
      {"play", GetButtonShape()},
      {"stop", GetButtonShape()},
  });
  const WidgetDoc::Mode flashing[] = {{"solid", "Lit."},
                                      {"blink", "Flashing."}};
  WidgetDoc stop = GetButtonDoc("stop", "Stop.");
  stop.modes = flashing;
  const CatalogDoc doc = {.description = "A unit.",
                          .widgets = {GetButtonDoc("play", "Play."), stop}};
  const DeviceType type = {
      .name = "Test", .catalog = &catalog, .catalog_doc = &doc};

  const std::string page = GenerateDeviceTypePage(type);
  EXPECT_THAT(page, HasSubstr("The modes of `play`.\n"));
  EXPECT_THAT(page, HasSubstr("The modes of `stop`.\n"));
  EXPECT_THAT(page, HasSubstr("| `blink` | Flashing. |\n"));
}

// Returns the file's contents without carriage returns, or "" if it can't be
// read.
std::string ReadPage(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  std::string text{std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>()};
  return absl::StrReplaceAll(text, {{"\r", ""}});
}

TEST(DeviceTypePagesTest, CheckedInPagesAreUpToDate) {
  for (const DeviceType& type : GetDeviceTypes()) {
    const std::filesystem::path path =
        std::filesystem::path(JPR_DEVICE_DOCS_DIR) /
        GetDeviceTypePageName(type);
    EXPECT_EQ(ReadPage(path), GenerateDeviceTypePage(type))
        << path.generic_string()
        << " is out of date: build the update_device_docs target.";
  }
}

TEST(DeviceTypePagesTest, CheckedInPagesAreOnlyTheTypes) {
  std::vector<std::string> types;
  for (const DeviceType& type : GetDeviceTypes()) {
    types.push_back(GetDeviceTypePageName(type));
  }
  std::vector<std::string> pages;
  for (const std::filesystem::directory_entry& entry :
       std::filesystem::directory_iterator(JPR_DEVICE_DOCS_DIR)) {
    if (entry.path().extension() == ".md") {
      pages.push_back(entry.path().filename().string());
    }
  }
  EXPECT_THAT(pages, UnorderedElementsAreArray(types))
      << JPR_DEVICE_DOCS_DIR
      << " has pages of no type: build the update_device_docs target.";
}

}  // namespace
}  // namespace jpr
