// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/device/widget_path.h"

#include <memory>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/log/log.h"
#include "absl/strings/ascii.h"
#include "absl/strings/str_cat.h"
#include "gb/parse/lexer.h"
#include "gb/parse/parser.h"
#include "gb/parse/parser_program.h"

namespace jpr {

namespace {

// The rule Parse() matches: the whole text is a path, or empty.
constexpr std::string_view kWidgetPathTextRule = R"---(
  WidgetPathText {
    [$path=WidgetPath] %end;
  }
)---";

std::shared_ptr<const gb::ParserProgram> CreateParserProgram() {
  std::string error_message;
  std::shared_ptr<const gb::ParserProgram> program = gb::ParserProgram::Create(
      kWidgetPathLexerConfig,
      absl::StrCat(kWidgetPathRules, kWidgetPathTextRule), &error_message);
  if (program == nullptr) {
    LOG(ERROR) << "Widget path grammar is invalid: " << error_message;
  }
  return program;
}

}  // namespace

bool IsWidgetName(std::string_view name) {
  return !name.empty() && !absl::ascii_isdigit(name.front()) &&
         absl::c_all_of(
             name, [](char c) { return absl::ascii_isalnum(c) || c == '_'; });
}

std::optional<WidgetPath> WidgetPath::Parse(std::string_view text,
                                            std::string* error_message) {
  static const absl::NoDestructor<std::shared_ptr<const gb::ParserProgram>>
      s_program(CreateParserProgram());
  std::unique_ptr<gb::Parser> parser = gb::Parser::Create(*s_program);
  if (parser == nullptr) {
    if (error_message != nullptr) {
      *error_message = "Widget path grammar is invalid";
    }
    return std::nullopt;
  }
  gb::LexerContentId content = parser->GetLexer().AddContent(std::string(text));
  gb::ParseResult result = parser->Parse(content, "WidgetPathText");
  if (!result) {
    if (error_message != nullptr) {
      *error_message = result.GetError().FormatMessage();
    }
    return std::nullopt;
  }
  const gb::ParsedItem* path = result->GetItem("path");
  return path != nullptr ? FromParsedItem(*path) : WidgetPath();
}

WidgetPath WidgetPath::FromParsedItem(const gb::ParsedItem& item) {
  std::vector<Step> steps;
  for (const gb::ParsedItem& step : item.GetItems("steps")) {
    if (step.IsIdent()) {
      steps.emplace_back(std::string(step.GetToken().GetString()));
    } else {
      steps.emplace_back(static_cast<int>(step.GetToken().GetInt()));
    }
  }
  return WidgetPath(std::move(steps));
}

std::string WidgetPath::ToString() const {
  std::string text;
  for (const Step& step : steps_) {
    if (const std::string* name = std::get_if<std::string>(&step)) {
      if (!text.empty()) {
        text += '.';
      }
      text += *name;
    } else if (const int* index = std::get_if<int>(&step)) {
      absl::StrAppend(&text, "[", *index, "]");
    }
  }
  return text;
}

}  // namespace jpr
