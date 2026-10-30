// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_path.h"

#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "absl/strings/str_cat.h"
#include "gb/parse/lexer.h"
#include "gb/parse/lexer_config.h"
#include "gb/parse/parser.h"
#include "gb/parse/parser_program.h"
#include "gb/parse/symbol.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace jpr {
namespace {

using ::testing::ElementsAre;
using ::testing::ElementsAreArray;
using ::testing::IsEmpty;
using Step = WidgetPath::Step;

TEST(WidgetNameTest, AcceptsCIdentifiers) {
  for (const char* name :
       {"strip", "assign_track", "f1", "line_2", "a", "Strip", "assignTrack",
        "_strip", "strip_", "assign__track"}) {
    EXPECT_TRUE(IsWidgetName(name)) << name;
  }
}

TEST(WidgetNameTest, RejectsWhatIsntACIdentifier) {
  for (const char* name :
       {"", "1st", "assign-track", "strip fader", "strip.fader", "strip[0]"}) {
    EXPECT_FALSE(IsWidgetName(name)) << name;
  }
}

TEST(WidgetNameTest, WidgetNamesAreTheFieldsAPathReads) {
  for (const char* name : {"strip", "Strip", "_strip", "strip_", "f1", "_",
                           "__", "_1", "1st", "assign-track", ""}) {
    std::optional<WidgetPath> path = WidgetPath::Parse(name);
    const bool is_field =
        path.has_value() &&
        ::testing::Matches(ElementsAre(Step(name)))(path->GetSteps());
    EXPECT_EQ(IsWidgetName(name), is_field) << name;
  }
}

TEST(WidgetPathTest, EmptyPathHasNoSteps) {
  EXPECT_THAT(WidgetPath().GetSteps(), IsEmpty());
  EXPECT_EQ(WidgetPath().ToString(), "");

  std::optional<WidgetPath> path = WidgetPath::Parse("");
  ASSERT_TRUE(path.has_value());
  EXPECT_THAT(path->GetSteps(), IsEmpty());
}

TEST(WidgetPathTest, ParsesFieldsAndIndicesAndGivesBackTheText) {
  struct Case {
    const char* text;
    std::vector<Step> steps;
  };
  const Case kCases[] = {
      {"strip[3].fader", {"strip", 3, "fader"}},
      {"play", {"play"}},
      {"xtouch.strip[0]", {"xtouch", "strip", 0}},
      {"grid[1][12]", {"grid", 1, 12}},
      {"[3].fader", {3, "fader"}},
      {"[0]", {0}},
      {"Strip._f1", {"Strip", "_f1"}},
  };
  for (const Case& c : kCases) {
    std::optional<WidgetPath> path = WidgetPath::Parse(c.text);
    ASSERT_TRUE(path.has_value()) << c.text;
    EXPECT_THAT(path->GetSteps(), ElementsAreArray(c.steps)) << c.text;
    EXPECT_EQ(path->ToString(), c.text);
  }
}

TEST(WidgetPathTest, ParsesSpacesBetweenSteps) {
  std::optional<WidgetPath> path = WidgetPath::Parse(" strip [ 3 ] . fader ");
  ASSERT_TRUE(path.has_value());
  EXPECT_THAT(path->GetSteps(),
              ElementsAre(Step("strip"), Step(3), Step("fader")));
  EXPECT_EQ(path->ToString(), "strip[3].fader");
}

TEST(WidgetPathTest, RejectsMalformedPaths) {
  for (const char* text :
       {".strip", "strip.", "strip..fader", "strip.[3]", "strip[]", "strip[3",
        "strip3]", "strip[3]fader", "strip[-1]", "strip[+1]", "strip[x]",
        "strip[3.5]", "strip[99999999999]", "1strip", "strip fader",
        "strip-fader", "strip.fader:"}) {
    EXPECT_FALSE(WidgetPath::Parse(text).has_value()) << text;
  }
}

TEST(WidgetPathTest, MalformedPathGivesTheParserError) {
  std::string error_message;
  EXPECT_FALSE(WidgetPath::Parse("strip.[3]", &error_message).has_value());
  EXPECT_FALSE(error_message.empty());
}

TEST(WidgetPathTest, FieldAndElementPathTextsAreAsTheTextOfThePath) {
  EXPECT_EQ(GetFieldPathText("", "play"), "play");
  EXPECT_EQ(GetFieldPathText("strip[2]", "fader"), "strip[2].fader");
  EXPECT_EQ(GetElementPathText("strip", 2), "strip[2]");
  EXPECT_EQ(GetElementPathText("", 2), "[2]");

  std::optional<WidgetPath> path = WidgetPath::Parse("strip[2].fader");
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(GetFieldPathText(GetElementPathText("strip", 2), "fader"),
            path->ToString());
}

TEST(WidgetPathTest, SubpathHasTheStepsFromFirstOn) {
  std::optional<WidgetPath> path = WidgetPath::Parse("xtouch.strip[0].fader");
  ASSERT_TRUE(path.has_value());
  EXPECT_EQ(path->GetSubpath(0).ToString(), "xtouch.strip[0].fader");
  EXPECT_EQ(path->GetSubpath(1).ToString(), "strip[0].fader");
  EXPECT_EQ(path->GetSubpath(2).ToString(), "[0].fader");
  EXPECT_THAT(path->GetSubpath(4).GetSteps(), IsEmpty());
  EXPECT_THAT(path->GetSubpath(5).GetSteps(), IsEmpty());
  EXPECT_EQ(path->GetSubpath(-1).ToString(), "xtouch.strip[0].fader");
}

// A language that includes widget paths, with a superset of their lexer config
// and rules: a mapping from a path to a string.
TEST(WidgetPathTest, GrammarWorksInALanguageThatIncludesIt) {
  std::vector<gb::Symbol> symbols(std::begin(kWidgetPathSymbols),
                                  std::end(kWidgetPathSymbols));
  symbols.push_back(':');
  symbols.push_back(';');
  gb::LexerConfig lexer_config = kWidgetPathLexerConfig;
  lexer_config.flags.Set(gb::kLexerFlags_CStrings);
  lexer_config.symbols = symbols;
  std::string error_message;
  std::shared_ptr<const gb::ParserProgram> program = gb::ParserProgram::Create(
      lexer_config, absl::StrCat(kWidgetPathRules, R"---(
        Mappings {
          $mappings=Mapping* %end;
        }

        Mapping {
          $path=WidgetPath ":" $property=%string ";";
        }
      )---"),
      &error_message);
  ASSERT_NE(program, nullptr) << error_message;
  std::unique_ptr<gb::Parser> parser = gb::Parser::Create(program);
  ASSERT_NE(parser, nullptr);
  gb::LexerContentId content = parser->GetLexer().AddContent(
      R"(strip[3].fader: "track:volume"; play: "cmd:1007";)");
  gb::ParseResult result = parser->Parse(content, "Mappings");
  ASSERT_TRUE(result.IsOk()) << result.GetError().FormatMessage();

  auto mappings = result->GetItems("mappings");
  ASSERT_EQ(std::ssize(mappings), 2);
  ASSERT_NE(mappings[0].GetItem("path"), nullptr);
  EXPECT_EQ(WidgetPath::FromParsedItem(*mappings[0].GetItem("path")).ToString(),
            "strip[3].fader");
  EXPECT_EQ(mappings[0].GetString("property"), "track:volume");
  ASSERT_NE(mappings[1].GetItem("path"), nullptr);
  EXPECT_EQ(WidgetPath::FromParsedItem(*mappings[1].GetItem("path")).ToString(),
            "play");
  EXPECT_EQ(mappings[1].GetString("property"), "cmd:1007");
}

}  // namespace
}  // namespace jpr
