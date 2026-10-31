// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_doc.h"

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/widget_shape.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::IsEmpty;

constexpr ControlInput::Type kValue = ControlInput::Type::kValue;
constexpr ControlInput::Type kPress = ControlInput::Type::kPress;
constexpr ControlOutput::Type kCValue = ControlOutput::Type::kCValue;
constexpr ControlOutput::Type kDValue = ControlOutput::Type::kDValue;

constexpr WidgetDoc::Mode kSolid = {"solid", "Lit."};
constexpr WidgetDoc::Mode kBlink = {"blink", "Blinking."};
constexpr WidgetDoc::Mode kLightModes[] = {kSolid, kBlink};

class ValidateCatalogDocTest : public ::testing::Test {
 protected:
  // Returns the errors validating the doc against the catalog adds, checking
  // that it returns whether there are none.
  std::vector<std::string> Validate(const CatalogDoc& doc) {
    std::vector<std::string> errors;
    const bool valid = ValidateCatalogDoc(catalog_, doc, &errors);
    EXPECT_EQ(valid, errors.empty());
    return errors;
  }

  // Returns the doc of the button, with a light's modes.
  static WidgetDoc GetButtonDoc(std::string_view path) {
    return {.path = path, .description = "A button.", .modes = kLightModes};
  }

  const WidgetShape button_ =
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"});
  const WidgetShape fader_ =
      WidgetShape::ForControl({kValue, kPress}, {kCValue});
  const WidgetShape strip_ =
      WidgetShape::ForStruct({{"fader", fader_}, {"select", button_}});
  const WidgetShape catalog_ = WidgetShape::ForStruct({
      {"play", button_},
      {"function", WidgetShape::ForArray(button_, 2)},
      {"strip", WidgetShape::ForArray(strip_, 4)},
  });

  // Documents every widget in the catalog.
  CatalogDoc doc_ = {
      .description = "A unit.",
      .widgets =
          {
              GetButtonDoc("play"),
              GetButtonDoc("function"),
              {.path = "strip", .description = "The strips."},
              {.path = "strip.fader", .description = "A fader."},
              GetButtonDoc("strip.select"),
          },
  };
};

TEST_F(ValidateCatalogDocTest, DocOfEveryWidgetAndModeIsValid) {
  EXPECT_THAT(Validate(doc_), IsEmpty());
}

TEST_F(ValidateCatalogDocTest, WidgetsMayBeDocumentedInAnyOrder) {
  std::swap(doc_.widgets.front(), doc_.widgets.back());
  EXPECT_THAT(Validate(doc_), IsEmpty());
}

TEST_F(ValidateCatalogDocTest, WidgetsNotDocumentedAreErrors) {
  doc_.widgets.erase(doc_.widgets.begin() + 1, doc_.widgets.begin() + 4);
  EXPECT_THAT(Validate(doc_), ElementsAre("function: isn't documented",
                                          "strip: isn't documented",
                                          "strip.fader: isn't documented"));
}

TEST_F(ValidateCatalogDocTest, PathsThatNameNothingAreErrors) {
  doc_.widgets.push_back({.path = "strip[0].fader", .description = "A."});
  doc_.widgets.push_back({.path = "strip.pan", .description = "A."});
  doc_.widgets.push_back({.path = "play.light", .description = "A."});
  doc_.widgets.push_back({.path = "", .description = "A."});
  EXPECT_THAT(
      Validate(doc_),
      ElementsAre("strip[0].fader: names nothing", "strip.pan: names nothing",
                  "play.light: names nothing", ": names nothing"));
}

TEST_F(ValidateCatalogDocTest, WidgetsDocumentedMoreThanOnceAreErrors) {
  doc_.widgets.push_back({.path = "strip.fader", .description = "Again."});
  EXPECT_THAT(Validate(doc_),
              ElementsAre("strip.fader: is documented more than once"));
}

TEST_F(ValidateCatalogDocTest, MissingDescriptionsAreErrors) {
  doc_.description = "";
  doc_.widgets[3].description = "";
  const WidgetDoc::Mode modes[] = {kSolid, {"blink", ""}};
  doc_.widgets[4].modes = modes;
  EXPECT_THAT(Validate(doc_),
              ElementsAre("catalog: has no description",
                          "strip.fader: has no description",
                          "strip.select: mode blink has no description"));
}

TEST_F(ValidateCatalogDocTest, ModesNotDocumentedAreErrors) {
  const WidgetDoc::Mode modes[] = {kBlink};
  doc_.widgets[0].modes = modes;
  doc_.widgets[1].modes = {};
  EXPECT_THAT(Validate(doc_),
              ElementsAre("play: mode solid isn't documented",
                          "function: mode solid isn't documented",
                          "function: mode blink isn't documented"));
}

TEST_F(ValidateCatalogDocTest, ModesThatNameNothingAreErrors) {
  const WidgetDoc::Mode with_flash[] = {kSolid, kBlink, {"flash", "Flashing."}};
  const WidgetDoc::Mode solid[] = {kSolid};
  doc_.widgets[0].modes = with_flash;
  doc_.widgets[2].modes = solid;
  doc_.widgets[3].modes = solid;
  EXPECT_THAT(Validate(doc_),
              ElementsAre("play: mode flash names nothing",
                          "strip: mode solid names nothing",
                          "strip.fader: mode solid names nothing"));
}

TEST_F(ValidateCatalogDocTest, ModesDocumentedMoreThanOnceAreErrors) {
  const WidgetDoc::Mode modes[] = {kSolid, kBlink, {"blink", "Again."}};
  doc_.widgets[0].modes = modes;
  EXPECT_THAT(Validate(doc_),
              ElementsAre("play: mode blink is documented more than once"));
}

TEST_F(ValidateCatalogDocTest, ModesOutOfTheControlsOrderAreErrors) {
  const WidgetDoc::Mode modes[] = {kBlink, kSolid};
  doc_.widgets[0].modes = modes;
  EXPECT_THAT(Validate(doc_),
              ElementsAre("play: has modes out of the control's order"));
}

TEST_F(ValidateCatalogDocTest, ErrorsAreOptional) {
  EXPECT_TRUE(ValidateCatalogDoc(catalog_, doc_, nullptr));
  doc_.widgets.pop_back();
  EXPECT_FALSE(ValidateCatalogDoc(catalog_, doc_, nullptr));
}

TEST_F(ValidateCatalogDocTest, FindsWidgetsAndModesByName) {
  const WidgetDoc* select = doc_.FindWidget("strip.select");
  ASSERT_NE(select, nullptr);
  EXPECT_EQ(select->path, "strip.select");
  EXPECT_EQ(doc_.FindWidget("strip.pan"), nullptr);

  const WidgetDoc::Mode* blink = select->FindMode("blink");
  ASSERT_NE(blink, nullptr);
  EXPECT_EQ(blink->description, "Blinking.");
  EXPECT_EQ(select->FindMode("flash"), nullptr);
}

TEST(ValidateCatalogDocNestingTest, NestedWidgetsAreDocumentedByFieldNames) {
  const WidgetShape button =
      WidgetShape::ForControl({kPress}, {kDValue}, {"solid", "blink"});
  const WidgetShape catalog = WidgetShape::ForStruct({
      {"grid", WidgetShape::ForArray(WidgetShape::ForArray(button, 2), 3)},
      {"section", WidgetShape::ForStruct({{"pad", button}})},
  });
  CatalogDoc doc = {
      .description = "A unit.",
      .widgets = {
          {.path = "grid", .description = "A grid.", .modes = kLightModes},
          {.path = "section", .description = "A section."},
          {.path = "section.pad",
           .description = "A pad.",
           .modes = kLightModes},
      }};
  std::vector<std::string> errors;
  EXPECT_TRUE(ValidateCatalogDoc(catalog, doc, &errors));
  EXPECT_THAT(errors, IsEmpty());

  doc.widgets.pop_back();
  doc.widgets.push_back(
      {.path = "grid.pad", .description = "A pad.", .modes = kLightModes});
  EXPECT_FALSE(ValidateCatalogDoc(catalog, doc, &errors));
  EXPECT_THAT(errors, ElementsAre("grid.pad: names nothing",
                                  "section.pad: isn't documented"));
}

}  // namespace
}  // namespace jpr
