// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/types/span.h"
#include "gb/parse/lexer_config.h"
#include "gb/parse/parse_result.h"
#include "gb/parse/symbol.h"

namespace jpr {

//==============================================================================
// Widget path grammar
//==============================================================================

// The lexer config and parser rules that widget paths are parsed with. A
// language that includes widget paths, such as a surface config, parses them
// with a superset of both: a lexer config with at least these flags, and
// kWidgetPathSymbols among its symbols, and rules that include
// kWidgetPathRules, whose "WidgetPath" rule matches a path (see
// WidgetPath::FromParsedItem()). It may add symbols, comments, and other kinds
// of token (such as strings or floats), but must read identifiers and integers
// exactly as this config does: no other identifier or integer flags, no
// keywords (its rules can match words as literals instead), and no identifier
// or integer affixes. Every widget name is then still a field, and every index
// still fits an int and isn't negative.
inline constexpr gb::Symbol kWidgetPathSymbols[] = {'.', '[', ']'};
inline constexpr gb::LexerConfig kWidgetPathLexerConfig = {
    .flags = {gb::kLexerFlags_CIdentifiers, gb::LexerFlag::kInt32,
              gb::LexerFlag::kDecimalIntegers},
    .symbols = kWidgetPathSymbols,
};
inline constexpr std::string_view kWidgetPathRules = R"---(
  WidgetPath {
    ($steps=%ident | "[" $steps=%int "]")
    ("." $steps=%ident | "[" $steps=%int "]")*;
  }
)---";

//==============================================================================
// Widget names
//==============================================================================

// Returns true if the name is a widget name: a C identifier, of letters,
// digits, and underscores, not starting with a digit. These are the
// identifiers kWidgetPathLexerConfig reads, so a widget name is always a field
// in a path. Fields, devices, and output modes all have widget names.
bool IsWidgetName(std::string_view name);

// Returns the text of the path to a field of the widget at `path`, or to an
// element of the array at `path`, as WidgetPath::ToString() writes it, such as
// "strip[2].fader" or "strip[2]". `path` is empty for the widget itself.
std::string GetFieldPathText(std::string_view path, std::string_view field);
std::string GetElementPathText(std::string_view path, int index);

//==============================================================================
// WidgetPath
//==============================================================================

// A path to a widget from another widget: field names and array indices, such
// as `strip[3].fader`. Indices start from 0. An empty path is the widget
// itself.
class WidgetPath {
 public:
  // A field name or an array index.
  using Step = std::variant<std::string, int>;

  // Constructs the empty path.
  WidgetPath() = default;

  // Returns the path, or nullopt if the text isn't one, setting
  // `error_message` (if provided) to the parser's error. Each field but the
  // first follows a `.`, so a path can start with an index (`[3].fader`), but
  // `strip.[3]` isn't one. Spaces between steps are allowed, as the text is
  // parsed with kWidgetPathRules.
  static std::optional<WidgetPath> Parse(std::string_view text,
                                         std::string* error_message = nullptr);

  // Returns the path that a match of the "WidgetPath" rule in kWidgetPathRules
  // parsed.
  static WidgetPath FromParsedItem(const gb::ParsedItem& item);

  absl::Span<const Step> GetSteps() const { return steps_; }

  // Returns the path of the steps from `first` on, such as `strip[0].fader`
  // from `xtouch.strip[0].fader` for 1. It is empty if `first` is past the
  // last step.
  WidgetPath GetSubpath(int first) const;

  // Returns the path's text, which Parse() reads as the same path. Paths with
  // the same steps have the same text.
  std::string ToString() const;

 private:
  explicit WidgetPath(std::vector<Step> steps) : steps_(std::move(steps)) {}

  std::vector<Step> steps_;
};

}  // namespace jpr
