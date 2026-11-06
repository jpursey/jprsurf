// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/config/surface_config.h"

#include <cstdint>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/base/no_destructor.h"
#include "absl/container/flat_hash_map.h"
#include "absl/log/check.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "gb/parse/lexer.h"
#include "gb/parse/lexer_config.h"
#include "gb/parse/parse_error.h"
#include "gb/parse/parse_result.h"
#include "gb/parse/parser.h"
#include "gb/parse/parser_program.h"
#include "gb/parse/symbol.h"
#include "gb/parse/token.h"
#include "jpr/common/track.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/spec_line.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_path.h"
#include "jpr/scene/catalog.h"

namespace jpr {
namespace {

//==============================================================================
// The grammar
//==============================================================================

// A name with a namespace, such as "user:folder" or
// "state:master_track.volume", which is a token of its own, so that it never
// reads as a widget path.
constexpr gb::TokenType kTokenQualifiedName = gb::kTokenUser;

// A superset of kWidgetPathLexerConfig, as the grammar includes
// kWidgetPathRules: its symbols are among these, and it reads identifiers and
// integers the same way.
constexpr gb::Symbol kSymbols[] = {'{', '}', '(', ')', '[',
                                   ']', '=', ',', '.', ".."};
constexpr std::string_view kLineComments[] = {"#"};
constexpr gb::LexerConfig::UserToken kUserTokens[] = {
    {.name = "name with a namespace",
     .type = kTokenQualifiedName,
     .regex = R"(([A-Za-z_]\w*:\w+(?:\.[A-Za-z_]\w*)*))"},
};

constexpr gb::LexerConfig kLexerConfig = {
    .flags = {gb::LexerFlag::kInt32, gb::LexerFlag::kDecimalIntegers,
              gb::LexerFlag::kFloat64, gb::LexerFlag::kDecimalFloats,
              gb::kLexerFlags_CStrings, gb::LexerFlag::kDecodeEscape,
              gb::kLexerFlags_CIdentifiers},
    .escape = '\\',
    .escape_newline = 'n',
    .escape_tab = 't',
    .line_comments = kLineComments,
    .symbols = kSymbols,
    .user_tokens = kUserTokens,
};

// The version is a rule of its own, so that a version this doesn't read stops
// reading before the rest is parsed by a grammar it may not be written in.
// Widget paths are kWidgetPathRules' "WidgetPath".
constexpr std::string_view kGrammar = R"---(
  %qualified_name = 0;

  version {
    "jprsurf":'Expected the version first, such as "jprsurf 1"'
    $number=%int:"Expected the version's number";
  }

  top_level {
    $statements=statement* %end:"Expected a statement";
  }

  statement {
    $devices=("devices" "{" $items=device* "}":"Expected a device");
    $widgets=("widgets" $struct=widget_struct);
    $settings=("settings" "{" $items=setting* "}":"Expected a setting");
    $components=("components" "{" $items=component*
                 "}":"Expected a component");
  }

  device {
    $name=%ident "=" $type=%ident "(" $params=param,* ")"
    ["{" $overrides=override* "}":"Expected an override"];
  }

  override {
    "without" $control=WidgetPath "[" $flows=%ident,+ "]";
  }

  setting {
    $name=%ident $value=value;
  }

  component {
    $kind=%ident $name=%qualified_name ["(" $params=param,* ")"];
  }

  param {
    $name=%ident "=" $value=value;
  }

  value {
    $list=("[" $items=value,* "]");
    $qualified_name=%qualified_name;
    $string=%string;
    $int=%int;
    $float=%float;
    $name=%ident;
  }

  widget_struct {
    "{" $items=widget_item* "}":"Expected a widget";
  }

  widget_item {
    $include=("include" $path=WidgetPath
              ["{" $excepts=except* "}":"Expected an except"]);
    $field=($name=%ident "=" $widget=widget);
  }

  except {
    "except" $name=%ident;
  }

  widget {
    $join=("join" "(" $slices=slice,+ ")");
    $struct=widget_struct;
    $path=WidgetPath;
  }

  slice {
    $path=WidgetPath ["[" $first=%int ".." [$last=%int] "]"];
  }
)---";

std::shared_ptr<const gb::ParserProgram> CreateGrammar() {
  std::string error;
  std::shared_ptr<const gb::ParserProgram> grammar = gb::ParserProgram::Create(
      kLexerConfig, absl::StrCat(kGrammar, kWidgetPathRules), &error);
  CHECK(grammar != nullptr) << error;
  return grammar;
}

std::shared_ptr<const gb::ParserProgram> GetGrammar() {
  static const absl::NoDestructor<std::shared_ptr<const gb::ParserProgram>>
      s_grammar(CreateGrammar());
  return *s_grammar;
}

// Returns the line of a lexer's line, which counts from 0, or no line if it is
// negative, as a lexer's is where there is none.
SpecLine GetSpecLine(int lexer_line) {
  return {lexer_line < 0 ? 0 : lexer_line + 1};
}

// Returns the text of a "WidgetPath" match, as WidgetPath::ToString() writes
// it.
std::string GetPathText(const gb::ParsedItem& path) {
  return WidgetPath::FromParsedItem(path).ToString();
}

// Returns a slice of a "slice" match. One that ends before it starts has no
// elements, which ValidateWidgets() reports.
WidgetSlice GetSlice(const gb::ParsedItem& item) {
  WidgetSlice slice = {.path = GetPathText(*item.GetItem("path")),
                       .first = static_cast<int>(item.GetInt("first"))};
  if (const gb::ParsedItem* last = item.GetItem("last")) {
    slice.count = static_cast<int>(last->GetToken().GetInt()) - slice.first + 1;
  }
  return slice;
}

//==============================================================================
// Parameters
//==============================================================================

// What a parameter's value is written as.
enum class ValueForm {
  kString,         // A quoted string.
  kName,           // A name, without a namespace.
  kNames,          // A list of names.
  kQualifiedName,  // A name with a namespace, such as "user:folder".
};

// A parameter that a statement may give, or a setting.
struct ParamSyntax {
  std::string_view name;
  ValueForm form = ValueForm::kName;

  // Whether it must be given. Reading requires only what a spec has no value
  // for leaving out, and checking the spec finds the rest.
  bool required = false;

  absl::Span<const std::string_view> choices;  // A name's, if it is limited.
};

// A parameter's value, as text: one string, or one for each name of a list. A
// name among the parameter's choices also has its index. It is empty if its
// value isn't valid.
struct ParamValue {
  std::vector<std::string> text;
  int choice = -1;
};

// The values of the parameters a statement gives, by their names.
using ParamValues = absl::flat_hash_map<std::string_view, ParamValue>;

// Returns the parameter's text, or empty if it isn't given.
std::string GetText(const ParamValues& values, std::string_view name) {
  const auto it = values.find(name);
  return it == values.end() || it->second.text.empty()
             ? std::string()
             : it->second.text.front();
}

// Returns the names of the parameter's list, or none if it isn't given.
std::vector<std::string> GetNames(const ParamValues& values,
                                  std::string_view name) {
  const auto it = values.find(name);
  return it == values.end() ? std::vector<std::string>() : it->second.text;
}

// Returns the index of the parameter's choice, or nullopt if it isn't given.
std::optional<int> GetChoice(const ParamValues& values, std::string_view name) {
  const auto it = values.find(name);
  if (it == values.end() || it->second.choice < 0) {
    return std::nullopt;
  }
  return it->second.choice;
}

// Returns true if the value is written in the form.
bool IsForm(const gb::ParsedItem& value, ValueForm form) {
  const std::string_view match = value.GetMatchName();
  switch (form) {
    case ValueForm::kString:
      return match == "string";
    case ValueForm::kName:
      return match == "name";
    case ValueForm::kNames:
      return match == "list" &&
             absl::c_all_of(value.GetItem("list")->GetItems("items"),
                            [](const gb::ParsedItem& item) {
                              return item.GetMatchName() == "name";
                            });
    case ValueForm::kQualifiedName:
      return match == "qualified_name";
  }
  return false;
}

// Returns how an error names what a value is written as.
std::string_view GetFormName(ValueForm form) {
  switch (form) {
    case ValueForm::kString:
      return "a string";
    case ValueForm::kName:
      return "a name";
    case ValueForm::kNames:
      return "a list of names";
    case ValueForm::kQualifiedName:
      return "a name with a namespace";
  }
  return {};
}

//------------------------------------------------------------------------------
// Devices and settings
//------------------------------------------------------------------------------

constexpr std::string_view kBoolNames[] = {"true", "false"};

// Ports are checked with the device (see CreateWidgetDevices()).
constexpr ParamSyntax kDeviceParams[] = {
    {"input", ValueForm::kString},
    {"output", ValueForm::kString},
    {"optional", ValueForm::kName, false, kBoolNames},
};

// The track filter's choices, and the filter each is, in the same order.
constexpr std::string_view kTrackFilterNames[] = {"mixer", "track_panel",
                                                  "all"};
constexpr TrackFilter kTrackFilters[] = {TrackFilter::kMcp, TrackFilter::kTcp,
                                         TrackFilter::kAll};

constexpr ParamSyntax kSettings[] = {
    {"track_filter", ValueForm::kName, false, kTrackFilterNames},
};

//------------------------------------------------------------------------------
// Components
//------------------------------------------------------------------------------

// Returns the syntax of a component kind's parameters. Of the required ones,
// only a choice is required here, as a spec has no value for none of its
// choices, and checking the spec finds the others left out (see
// ValidateSurfaceSpec()), as it knows where each component is.
std::vector<ParamSyntax> GetParamSyntax(absl::Span<const ParamInfo> params) {
  std::vector<ParamSyntax> syntax;
  for (const ParamInfo& param : params) {
    ValueForm form = ValueForm::kName;
    if (param.kind == ParamKind::kNewNameList) {
      form = ValueForm::kNames;
    } else if (param.kind == ParamKind::kTrackReference ||
               param.kind == ParamKind::kModifier) {
      form = ValueForm::kQualifiedName;
    }
    syntax.push_back({.name = param.name,
                      .form = form,
                      .required = param.kind == ParamKind::kChoice &&
                                  param.use == ParamUse::kRequired,
                      .choices = param.choices});
  }
  return syntax;
}

// Returns a component of the kind at `index` in GetComponentKinds(), which is
// the index of its alternative in ComponentSpec.
template <int... kIndexes>
ComponentSpec MakeComponent(int index,
                            std::integer_sequence<int, kIndexes...>) {
  ComponentSpec component;
  ((index == kIndexes ? (void)component.emplace<kIndexes>() : void()), ...);
  return component;
}

ComponentSpec MakeComponent(int index) {
  return MakeComponent(
      index,
      std::make_integer_sequence<int, std::variant_size_v<ComponentSpec>>());
}

// Each sets the fields of a kind of component from its parameters, by their
// names in its kind's ParamInfo.
void SetParams(ModifierSpec&, const ParamValues&) {}

void SetParams(EnumeratedValueSpec& spec, const ParamValues& values) {
  spec.values = GetNames(values, "values");
  spec.initial = GetText(values, "initial");
}

void SetParams(TrackReferenceSpec& spec, const ParamValues& values) {
  spec.fallback = GetText(values, "fallback");
  spec.follow = GetText(values, "follow");
}

void SetParams(TrackAnchorSpec& spec, const ParamValues& values) {
  // The choices are in the order of kTrackBoolProperties.
  if (std::optional<int> action = GetChoice(values, "action")) {
    spec.action = kTrackBoolProperties[*action];
  }
  spec.modifier = GetText(values, "modifier");
}

void SetParams(TrackPickSpec& spec, const ParamValues& values) {
  spec.reference = GetText(values, "reference");
  spec.source = GetText(values, "source");
}

//==============================================================================
// ConfigReader
//==============================================================================

// Reads a config's parse into a spec, adding an error for each problem.
class ConfigReader final {
 public:
  ConfigReader(gb::Parser& parser, std::vector<std::string>* errors)
      : parser_(parser), errors_(errors) {}

  // Returns the spec of the content, or nullopt if there is an error.
  std::optional<SurfaceSpec> Read(gb::LexerContentId content);

 private:
  // Returns the line the item starts on.
  SpecLine GetLine(const gb::ParsedItem& item) const;

  void AddError(SpecPart part, std::string_view message);
  void AddParseError(const gb::ParseError& error);

  // Returns true if this is the first of its block, which the top level may
  // have only one of, or adds an error.
  bool IsFirstBlock(std::string_view keyword, SpecLine line);

  void ReadStatement(const gb::ParsedItem& statement);

  // Returns the values of the parameters, each of which `syntax` must have,
  // given at most once, in its form. Adds an error for each problem, about
  // the parameter's line and `what`, and for each required parameter left
  // out, about `what`. `noun` is what a parameter is called, for an error
  // about one that doesn't exist.
  ParamValues ReadParams(absl::Span<const gb::ParsedItem> params,
                         absl::Span<const ParamSyntax> syntax,
                         std::string_view noun, SpecPart what);

  // Sets `result` to the value, or adds an error if it isn't valid.
  void ReadValue(const gb::ParsedItem& value, const ParamSyntax& param,
                 SpecPart what, ParamValue& result);

  DeviceSpec ReadDevice(const gb::ParsedItem& device);
  ControlOverride ReadOverride(const gb::ParsedItem& item,
                               std::string_view device_name);

  // Reads a struct of widgets, whose path is `path`, or empty for the
  // top-level struct.
  WidgetStructDefinition ReadWidgetStruct(const gb::ParsedItem& item,
                                          std::string_view path);
  WidgetFieldDefinition ReadWidgetField(const gb::ParsedItem& item,
                                        std::string_view parent);

  void ReadSettings(const gb::ParsedItem& block);
  std::optional<ComponentSpec> ReadComponent(const gb::ParsedItem& item);

  gb::Parser& parser_;
  std::vector<std::string>* const errors_;
  bool has_error_ = false;
  SurfaceSpec spec_;

  // The line of each block the top level may have only one of, by its
  // keyword.
  absl::flat_hash_map<std::string, SpecLine> block_lines_;
};

std::optional<SurfaceSpec> ConfigReader::Read(gb::LexerContentId content) {
  gb::ParseResult version = parser_.Parse(content, "version");
  if (!version) {
    AddParseError(version.GetError());
    return std::nullopt;
  }
  if (const int64_t number = version->GetInt("number");
      number != kSurfaceConfigVersion) {
    AddError({.line = GetLine(*version)},
             absl::StrCat("version ", number, " isn't known: JPRSurf reads ",
                          "version ", kSurfaceConfigVersion));
    return std::nullopt;
  }

  gb::ParseResult top_level = parser_.Parse(content, "top_level");
  if (!top_level) {
    AddParseError(top_level.GetError());
    return std::nullopt;
  }
  for (const gb::ParsedItem& statement : top_level->GetItems("statements")) {
    ReadStatement(statement);
  }
  for (std::string_view keyword : {"devices", "widgets"}) {
    if (!block_lines_.contains(keyword)) {
      AddError({}, absl::StrCat("the config has no \"", keyword, "\" block"));
    }
  }
  if (has_error_) {
    return std::nullopt;
  }
  return std::move(spec_);
}

SpecLine ConfigReader::GetLine(const gb::ParsedItem& item) const {
  return GetSpecLine(parser_.GetLexer().GetTokenLocation(item.GetToken()).line);
}

void ConfigReader::AddError(SpecPart part, std::string_view message) {
  has_error_ = true;
  if (errors_ != nullptr) {
    errors_->push_back(GetSpecError(part, message));
  }
}

void ConfigReader::AddParseError(const gb::ParseError& error) {
  AddError({.line = GetSpecLine(error.GetLocation().line)}, error.GetMessage());
}

bool ConfigReader::IsFirstBlock(std::string_view keyword, SpecLine line) {
  const auto [first, added] =
      block_lines_.try_emplace(std::string(keyword), line);
  if (!added) {
    AddError({.line = line},
             absl::StrCat("the config already has a \"", keyword,
                          "\" block, on line ", first->second.number));
  }
  return added;
}

void ConfigReader::ReadStatement(const gb::ParsedItem& statement) {
  const std::string_view keyword = statement.GetMatchName();
  if (keyword != "components" && !IsFirstBlock(keyword, GetLine(statement))) {
    return;
  }
  const gb::ParsedItem& block = *statement.GetItem(keyword);
  if (keyword == "devices") {
    for (const gb::ParsedItem& device : block.GetItems("items")) {
      spec_.devices.push_back(ReadDevice(device));
    }
  } else if (keyword == "widgets") {
    spec_.widgets = ReadWidgetStruct(*block.GetItem("struct"), {});
  } else if (keyword == "settings") {
    ReadSettings(block);
  } else if (keyword == "components") {
    for (const gb::ParsedItem& item : block.GetItems("items")) {
      if (std::optional<ComponentSpec> component = ReadComponent(item)) {
        spec_.components.push_back(*std::move(component));
      }
    }
  }
}

ParamValues ConfigReader::ReadParams(absl::Span<const gb::ParsedItem> params,
                                     absl::Span<const ParamSyntax> syntax,
                                     std::string_view noun, SpecPart what) {
  ParamValues values;
  for (const gb::ParsedItem& item : params) {
    const SpecPart part = {.line = GetLine(item), .text = what.text};
    const std::string_view name = item.GetString("name");
    const auto param = absl::c_find_if(
        syntax,
        [name](const ParamSyntax& param) { return param.name == name; });
    if (param == syntax.end()) {
      AddError(part, absl::StrCat("\"", name, "\" isn't a ", noun));
      continue;
    }
    const auto [value, added] = values.try_emplace(param->name);
    if (!added) {
      AddError(part, absl::StrCat("\"", name, "\" is given twice"));
      continue;
    }
    ReadValue(*item.GetItem("value"), *param, part, value->second);
  }
  for (const ParamSyntax& param : syntax) {
    if (param.required && !values.contains(param.name)) {
      AddError(what, absl::StrCat("has no ", param.name));
    }
  }
  return values;
}

void ConfigReader::ReadValue(const gb::ParsedItem& value,
                             const ParamSyntax& param, SpecPart what,
                             ParamValue& result) {
  if (!IsForm(value, param.form)) {
    AddError(what,
             absl::StrCat(param.name, " isn't ", GetFormName(param.form)));
    return;
  }
  if (param.form == ValueForm::kNames) {
    for (const gb::ParsedItem& item :
         value.GetItem("list")->GetItems("items")) {
      result.text.emplace_back(item.GetString("name"));
    }
    return;
  }
  const std::string_view text = value.GetString(value.GetMatchName());
  if (!param.choices.empty()) {
    const auto choice = absl::c_find(param.choices, text);
    if (choice == param.choices.end()) {
      AddError(what, absl::StrCat(param.name, " \"", text, "\" isn't one of: ",
                                  absl::StrJoin(param.choices, ", ")));
      return;
    }
    result.choice = static_cast<int>(choice - param.choices.begin());
  }
  result.text.emplace_back(text);
}

DeviceSpec ConfigReader::ReadDevice(const gb::ParsedItem& item) {
  DeviceSpec device = {.name = std::string(item.GetString("name")),
                       .type = std::string(item.GetString("type")),
                       .line = GetLine(item)};
  const ParamValues values =
      ReadParams(item.GetItems("params"), kDeviceParams, "parameter",
                 {.line = device.line, .text = device.name});
  device.input_port = GetText(values, "input");
  device.output_port = GetText(values, "output");
  device.required = GetText(values, "optional") != "true";
  for (const gb::ParsedItem& control : item.GetItems("overrides")) {
    device.overrides.push_back(ReadOverride(control, device.name));
  }
  return device;
}

ControlOverride ConfigReader::ReadOverride(const gb::ParsedItem& item,
                                           std::string_view device_name) {
  ControlOverride control = {.control = GetPathText(*item.GetItem("control")),
                             .line = GetLine(item)};
  for (const gb::ParsedItem& flow : item.GetItems("flows")) {
    const std::string_view name = flow.GetToken().GetString();
    const auto input =
        absl::c_find_if(ControlInput::kTypes, [name](ControlInput::Type type) {
          return ControlInput::GetTypeName(type) == name;
        });
    const auto output = absl::c_find_if(
        ControlOutput::kTypes, [name](ControlOutput::Type type) {
          return ControlOutput::GetTypeName(type) == name;
        });
    if (input != std::end(ControlInput::kTypes)) {
      control.inputs.Set(*input);
    } else if (output != std::end(ControlOutput::kTypes)) {
      control.outputs.Set(*output);
    } else {
      const std::string subject =
          absl::StrCat(device_name, ": ", control.control);
      AddError({.line = GetLine(flow), .text = subject},
               absl::StrCat("\"", name, "\" isn't an input or an output"));
    }
  }
  return control;
}

WidgetStructDefinition ConfigReader::ReadWidgetStruct(
    const gb::ParsedItem& item, std::string_view path) {
  WidgetStructDefinition widgets;
  for (const gb::ParsedItem& widget : item.GetItems("items")) {
    if (const gb::ParsedItem* include = widget.GetItem("include")) {
      WidgetInclude& added = widgets.includes.emplace_back(
          WidgetInclude{.path = GetPathText(*include->GetItem("path")),
                        .line = GetLine(widget)});
      for (const gb::ParsedItem& except : include->GetItems("excepts")) {
        added.excluded.emplace_back(except.GetString("name"));
      }
    } else {
      widgets.fields.push_back(ReadWidgetField(*widget.GetItem("field"), path));
    }
  }
  return widgets;
}

WidgetFieldDefinition ConfigReader::ReadWidgetField(const gb::ParsedItem& item,
                                                    std::string_view parent) {
  WidgetFieldDefinition field = {.name = std::string(item.GetString("name")),
                                 .line = GetLine(item)};
  const gb::ParsedItem& widget = *item.GetItem("widget");
  const std::string_view form = widget.GetMatchName();
  if (form == "join") {
    WidgetJoinDefinition join;
    for (const gb::ParsedItem& slice :
         widget.GetItem("join")->GetItems("slices")) {
      join.slices.push_back(GetSlice(slice));
    }
    field.widget = std::move(join);
  } else if (form == "struct") {
    field.widget = ReadWidgetStruct(*widget.GetItem("struct"),
                                    GetFieldPathText(parent, field.name));
  } else {
    field.widget = GetPathText(*widget.GetItem("path"));
  }
  return field;
}

void ConfigReader::ReadSettings(const gb::ParsedItem& block) {
  const ParamValues values =
      ReadParams(block.GetItems("items"), kSettings, "setting", {});
  if (std::optional<int> filter = GetChoice(values, "track_filter")) {
    spec_.track_filter = kTrackFilters[*filter];
  }
}

std::optional<ComponentSpec> ConfigReader::ReadComponent(
    const gb::ParsedItem& item) {
  const std::string_view name = item.GetString("name");
  const SpecPart what = {.line = GetLine(item), .text = name};
  const std::string_view kind_name = item.GetString("kind");
  const absl::Span<const ComponentKindInfo> kinds = GetComponentKinds();
  const auto kind =
      absl::c_find_if(kinds, [kind_name](const ComponentKindInfo& kind) {
        return kind.name == kind_name;
      });
  if (kind == kinds.end()) {
    AddError(what, absl::StrCat("\"", kind_name, "\" isn't a component kind"));
    return std::nullopt;
  }
  const ParamValues values = ReadParams(
      item.GetItems("params"), GetParamSyntax(kind->params), "parameter", what);
  ComponentSpec component =
      MakeComponent(static_cast<int>(kind - kinds.begin()));
  std::visit(
      [&](auto& spec) {
        spec.name = std::string(name);
        spec.line = what.line;
        SetParams(spec, values);
      },
      component);
  return component;
}

}  // namespace

std::optional<SurfaceSpec> ReadSurfaceConfig(std::string text,
                                             std::vector<std::string>* errors) {
  std::unique_ptr<gb::Parser> parser = gb::Parser::Create(GetGrammar());
  const gb::LexerContentId content =
      parser->GetLexer().AddContent(std::move(text));
  if (content == gb::kNoLexerContent) {
    if (errors != nullptr) {
      errors->push_back("the config is too large to read");
    }
    return std::nullopt;
  }
  return ConfigReader(*parser, errors).Read(content);
}

}  // namespace jpr
