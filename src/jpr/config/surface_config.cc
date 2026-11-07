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
#include "absl/container/flat_hash_set.h"
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
#include "jpr/common/color.h"
#include "jpr/common/track.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_input_handle.h"
#include "jpr/device/control_output.h"
#include "jpr/device/control_override.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/spec_line.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_path.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/surface_spec.h"
#include "jpr/scene/track_actions.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

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
constexpr gb::Symbol kSymbols[] = {'{', '}', '(',  ')',  '[',  ']',  '=',
                                   ',', '.', "..", "->", "<-", "<->"};
constexpr std::string_view kLineComments[] = {"#"};
constexpr gb::LexerConfig::UserToken kUserTokens[] = {
    {.name = "name with a namespace",
     .type = kTokenQualifiedName,
     .regex = R"(([A-Za-z_]\w*:\w+(?:\.[A-Za-z_]\w*)*))"},
};

constexpr gb::LexerConfig kLexerConfig = {
    .flags = {gb::LexerFlag::kInt32, gb::LexerFlag::kDecimalIntegers,
              gb::LexerFlag::kFloat64, gb::LexerFlag::kDecimalFloats,
              gb::LexerFlag::kNegativeFloats, gb::kLexerFlags_CStrings,
              gb::LexerFlag::kDecodeEscape, gb::kLexerFlags_CIdentifiers},
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
    $mappings=("mappings" [$name=%ident ["(" $params=template_param,* ")"]]
               "{" $items=mapping_item* "}":"Expected a mapping");
    $view=view;
    $when=("when" $condition=condition "{" $items=statement*
           "}":"Expected a statement");
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
    $call=($name=%ident "(" $params=param,* ")");
    $name=%ident;
  }

  view {
    "view" ($element=%ident "in" $array=WidgetPath |
            $name=%ident ["(" $params=param,* ")"])
    "{" $items=statement* "}":"Expected a statement";
  }

  template_param {
    $widget=("widget" $name=%ident);
    $property=("property" $name=%ident
               ["=" ($default=%qualified_name | $default=%ident)]);
  }

  mapping_item {
    $use=("use" $name=%ident ["(" $args=arg,* ")"]);
    $when=("when" $condition=condition "{" $items=mapping_item*
           "}":"Expected a mapping");
    $mapping=mapping;
  }

  arg {
    $name=%ident "=" ($qualified_name=%qualified_name | $path=WidgetPath);
  }

  mapping {
    $control=WidgetPath ["[" $reads=read_option,+ "]"] $arrow=arrow
    $target=target ["{" $options=option* "}":"Expected an option"];
  }

  read_option {
    $modifier=%qualified_name;
    $name=%ident;
  }

  arrow {
    $read="->";
    $write="<-";
    $both="<->";
  }

  target {
    $property=%qualified_name;
    $fixed=fixed;
    $param=%ident;
  }

  constant {
    $fixed=fixed;
    $name=%ident;
  }

  fixed {
    $color=("rgb" "(" $r=%int "," $g=%int "," $b=%int ")");
    $true="true";
    $false="false";
    $string=%string;
    $int=%int;
    $float=%float;
  }

  option {
    $set=("set" $value=constant);
    $range=("range" $min=constant ".." $max=constant);
    $toggle=("toggle" $min=constant ".." $max=constant);
    $mode=("mode" $name=%ident);
    $when=("when" $condition=condition "{" $items=option*
           "}":"Expected an option");
  }

  condition {
    ($property=%qualified_name | $property=%ident) ["=" $value=constant];
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
  kInt,            // An integer.
  kCall,           // A name, with or without parameters, such as a list.
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

// A parameter's value, as text: one string, or one for each name of a list,
// which is empty if its value isn't valid. A name among the parameter's
// choices also has its index, a call its parameters, and an integer only its
// number.
struct ParamValue {
  std::vector<std::string> text;
  std::optional<int> choice;
  std::optional<int> number;
  absl::Span<const gb::ParsedItem> params;
};

// The values of the parameters a statement gives, by their names.
using ParamValues = absl::flat_hash_map<std::string_view, ParamValue>;

// Returns the parameter's value, which is empty if it isn't given.
const ParamValue& GetValue(const ParamValues& values, std::string_view name) {
  static const absl::NoDestructor<ParamValue> s_empty;
  const auto it = values.find(name);
  return it == values.end() ? *s_empty : it->second;
}

// Returns the parameter's text, or empty if it isn't given.
std::string GetText(const ParamValues& values, std::string_view name) {
  const ParamValue& value = GetValue(values, name);
  return value.text.empty() ? std::string() : value.text.front();
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
    case ValueForm::kInt:
      return match == "int";
    case ValueForm::kCall:
      return match == "call" || match == "name";
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
    case ValueForm::kInt:
      return "an integer";
    case ValueForm::kCall:
      return "a name, with or without parameters";
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
// Views and mappings
//------------------------------------------------------------------------------

// A list's kinds, in the order of View::ListConfig::items' alternatives.
constexpr std::string_view kListNames[] = {"child_tracks", "routes"};

constexpr ParamSyntax kViewParams[] = {
    {"subject", ValueForm::kQualifiedName},
    {"list", ValueForm::kCall, false, kListNames},
};

constexpr ParamSyntax kChildTracksParams[] = {
    {"reveal", ValueForm::kQualifiedName},
    {"bank", ValueForm::kInt},
};

// A routes list's types, and the rule each is, in the same order. Leaving it
// out is kSendsUnlessOnlyReceives.
constexpr std::string_view kRouteTypeNames[] = {"sends", "receives"};
constexpr View::RouteTypeRule kRouteTypeRules[] = {
    View::RouteTypeRule::kSends, View::RouteTypeRule::kReceives};

constexpr ParamSyntax kRoutesParams[] = {
    {"type", ValueForm::kName, false, kRouteTypeNames},
    {"bank", ValueForm::kInt},
};

// The press behaviors a mapping may read its control with, and the behavior
// each is, in the same order.
constexpr std::string_view kPressBehaviorNames[] = {"long", "double", "tap"};
constexpr InputConfig::PressBehavior kPressBehaviors[] = {
    InputConfig::PressBehavior::kLongPress,
    InputConfig::PressBehavior::kDoublePress, InputConfig::PressBehavior::kTap};

constexpr std::string_view kWhenInWhen =
    "a \"when\" can't be in another \"when\"";

// Returns the input or output type with the name, or nullopt if there is none.
std::optional<ControlInput::Type> FindInputType(std::string_view name) {
  for (ControlInput::Type type : ControlInput::kTypes) {
    if (ControlInput::GetTypeName(type) == name) {
      return type;
    }
  }
  return std::nullopt;
}

std::optional<ControlOutput::Type> FindOutputType(std::string_view name) {
  for (ControlOutput::Type type : ControlOutput::kTypes) {
    if (ControlOutput::GetTypeName(type) == name) {
      return type;
    }
  }
  return std::nullopt;
}

// Returns the fixed value a mapping writes for a constant: true or false is a
// toggle, a number normalized, a string text, and a color a color.
FixedValue GetFixedValue(ViewProperty::Value value) {
  if (const int* number = std::get_if<int>(&value)) {
    value = static_cast<double>(*number);
  }
  ViewProperty::Type type = ViewProperty::Type::kToggle;
  if (std::holds_alternative<double>(value)) {
    type = ViewProperty::Type::kNormalized;
  } else if (std::holds_alternative<std::string>(value)) {
    type = ViewProperty::Type::kText;
  } else if (std::holds_alternative<Color>(value)) {
    type = ViewProperty::Type::kColor;
  }
  return {.type = type, .value = std::move(value)};
}

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
  spec.values = GetValue(values, "values").text;
  spec.initial = GetText(values, "initial");
}

void SetParams(TrackReferenceSpec& spec, const ParamValues& values) {
  spec.fallback = GetText(values, "fallback");
  spec.follow = GetText(values, "follow");
}

void SetParams(TrackAnchorSpec& spec, const ParamValues& values) {
  // The choices are in the order of kTrackAnchorActions.
  if (std::optional<int> action = GetValue(values, "action").choice) {
    spec.action = kTrackAnchorActions[*action];
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

  // Reads a statement at the top level, where a template's parameters are
  // already read (see Read()). One that a view's body may have too is the
  // root view's.
  void ReadTopLevelStatement(const gb::ParsedItem& statement);

  // Reads a statement into the body of the view at `path`.
  void ReadStatement(const gb::ParsedItem& statement, ViewSpec& view,
                     std::string_view path);

  // Reads a "when" around views, into the view at `path`.
  void ReadViewsWhen(const gb::ParsedItem& when, ViewSpec& view,
                     std::string_view path);
  ViewSpec ReadView(const gb::ParsedItem& item, std::string_view parent_path,
                    std::optional<ViewCondition::Config> condition);
  void ReadViewParams(absl::Span<const gb::ParsedItem> params, SpecPart what,
                      View::Config& config);
  View::ListConfig ReadList(int kind, absl::Span<const gb::ParsedItem> params,
                            SpecPart what);

  // Returns a template with the parameters of the named "mappings" block, but
  // none of its mappings or uses.
  TemplateSpec ReadTemplateParams(const gb::ParsedItem& block);

  // Returns the first template with the name, or null if there is none.
  const TemplateSpec* FindTemplate(std::string_view name) const;

  // Reads a "mappings" block's mappings and uses, in `scope` (a view's path,
  // or a template), each with the condition, if any.
  void ReadMappings(absl::Span<const gb::ParsedItem> items,
                    std::string_view scope,
                    const std::optional<ViewCondition::Config>& condition,
                    std::vector<MappingSpec>& mappings,
                    std::vector<TemplateUse>& uses);
  TemplateUse ReadUse(const gb::ParsedItem& item, std::string_view scope);
  MappingSpec ReadMapping(const gb::ParsedItem& item, std::string_view scope,
                          std::optional<ViewCondition::Config> condition);

  // Reads how a mapping reads its control, about `what`.
  void ReadHow(absl::Span<const gb::ParsedItem> reads, SpecPart what,
               ViewMapping::ReadConfig& read);
  std::variant<std::string, FixedValue> ReadTarget(const gb::ParsedItem& target,
                                                   SpecPart what);

  // Each reads a mapping's options, or a "when" around its modes, about
  // `what`.
  void ReadOptions(absl::Span<const gb::ParsedItem> options, SpecPart what,
                   ViewMapping::Config& config);
  void ReadModeWhen(const gb::ParsedItem& when, SpecPart what,
                    ViewMapping::WriteConfig& write);

  ViewCondition::Config ReadCondition(const gb::ParsedItem& item);

  // Each returns the value of a "constant" or "fixed" match, or none if it
  // isn't valid, adding an error about `what`.
  ViewProperty::Value ReadConstant(const gb::ParsedItem& constant,
                                   SpecPart what);
  ViewProperty::Value ReadFixed(const gb::ParsedItem& fixed, SpecPart what);

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

  int next_template_ = 0;  // The template whose mappings are read next.
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
  // Templates' parameters are read first, so that a use can sort its
  // arguments by them, wherever it is.
  const absl::Span<const gb::ParsedItem> statements =
      top_level->GetItems("statements");
  for (const gb::ParsedItem& statement : statements) {
    const gb::ParsedItem* block = statement.GetItem("mappings");
    if (block != nullptr && block->GetItem("name") != nullptr) {
      spec_.templates.push_back(ReadTemplateParams(*block));
    }
  }
  for (const gb::ParsedItem& statement : statements) {
    ReadTopLevelStatement(statement);
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

void ConfigReader::ReadTopLevelStatement(const gb::ParsedItem& statement) {
  const std::string_view keyword = statement.GetMatchName();
  const gb::ParsedItem& block = *statement.GetItem(keyword);
  if (keyword == "mappings" && block.GetItem("name") != nullptr) {
    // The templates are in the order of their blocks.
    TemplateSpec& spec = spec_.templates[next_template_++];
    ReadMappings(block.GetItems("items"), absl::StrCat("template ", spec.name),
                 std::nullopt, spec.mappings, spec.uses);
  } else if (keyword == "components") {
    // The top level's components are the surface's, rather than the root
    // view's.
    for (const gb::ParsedItem& item : block.GetItems("items")) {
      if (std::optional<ComponentSpec> component = ReadComponent(item)) {
        spec_.components.push_back(*std::move(component));
      }
    }
  } else if (keyword != "devices" && keyword != "widgets" &&
             keyword != "settings") {
    ReadStatement(statement, spec_.root, "/");
  } else if (IsFirstBlock(keyword, GetLine(statement))) {
    if (keyword == "devices") {
      for (const gb::ParsedItem& device : block.GetItems("items")) {
        spec_.devices.push_back(ReadDevice(device));
      }
    } else if (keyword == "widgets") {
      spec_.widgets = ReadWidgetStruct(*block.GetItem("struct"), {});
    } else {
      ReadSettings(block);
    }
  }
}

void ConfigReader::ReadStatement(const gb::ParsedItem& statement,
                                 ViewSpec& view, std::string_view path) {
  const std::string_view keyword = statement.GetMatchName();
  const gb::ParsedItem& block = *statement.GetItem(keyword);
  if (keyword == "components") {
    for (const gb::ParsedItem& item : block.GetItems("items")) {
      if (std::optional<ComponentSpec> component = ReadComponent(item)) {
        view.components.push_back(*std::move(component));
      }
    }
  } else if (keyword == "mappings") {
    if (block.GetItem("name") != nullptr) {
      AddError({.line = GetLine(statement)},
               "named mappings are only at the top level");
    } else {
      ReadMappings(block.GetItems("items"), path, std::nullopt, view.mappings,
                   view.uses);
    }
  } else if (keyword == "view") {
    view.children.push_back(ReadView(block, path, std::nullopt));
  } else if (keyword == "when") {
    ReadViewsWhen(block, view, path);
  } else {
    AddError({.line = GetLine(statement)},
             absl::StrCat("\"", keyword, "\" is only at the top level"));
  }
}

void ConfigReader::ReadViewsWhen(const gb::ParsedItem& when, ViewSpec& view,
                                 std::string_view path) {
  const ViewCondition::Config condition =
      ReadCondition(*when.GetItem("condition"));
  for (const gb::ParsedItem& statement : when.GetItems("items")) {
    const std::string_view keyword = statement.GetMatchName();
    if (keyword == "view") {
      view.children.push_back(
          ReadView(*statement.GetItem("view"), path, condition));
    } else {
      AddError({.line = GetLine(statement)},
               keyword == "when"
                   ? kWhenInWhen
                   : absl::StrCat("\"", keyword, "\" can't be in a \"when\" ",
                                  "here, which holds only views"));
    }
  }
}

ViewSpec ConfigReader::ReadView(
    const gb::ParsedItem& item, std::string_view parent_path,
    std::optional<ViewCondition::Config> condition) {
  ViewSpec view = {.config = {.condition = std::move(condition)},
                   .line = GetLine(item)};
  if (const gb::ParsedItem* array = item.GetItem("array")) {
    view.config.subject = View::ListItemSubject{};
    view.repeat = RepeatSpec{.array = GetPathText(*array),
                             .element = std::string(item.GetString("element"))};
  } else {
    view.name = item.GetString("name");
  }
  const std::string path = GetViewPath(parent_path, view);
  ReadViewParams(item.GetItems("params"), {.line = view.line, .text = path},
                 view.config);
  for (const gb::ParsedItem& statement : item.GetItems("items")) {
    ReadStatement(statement, view, path);
  }
  return view;
}

void ConfigReader::ReadViewParams(absl::Span<const gb::ParsedItem> params,
                                  SpecPart what, View::Config& config) {
  const ParamValues values = ReadParams(params, kViewParams, "parameter", what);
  if (std::string subject = GetText(values, "subject"); !subject.empty()) {
    config.subject = View::ReferenceSubject{.name = std::move(subject)};
  }
  const ParamValue& list = GetValue(values, "list");
  if (list.choice.has_value()) {
    config.list = ReadList(*list.choice, list.params, what);
  }
}

View::ListConfig ConfigReader::ReadList(int kind,
                                        absl::Span<const gb::ParsedItem> params,
                                        SpecPart what) {
  const bool child_tracks = kListNames[kind] == "child_tracks";
  const ParamValues values =
      ReadParams(params, child_tracks ? kChildTracksParams : kRoutesParams,
                 "parameter", what);
  View::ListConfig list = {.bank_size = GetValue(values, "bank").number};
  if (child_tracks) {
    list.items = View::ChildTracks{.reveal = GetText(values, "reveal")};
  } else {
    View::Routes routes;
    if (std::optional<int> type = GetValue(values, "type").choice) {
      routes.route_type_rule = kRouteTypeRules[*type];
    }
    list.items = routes;
  }
  return list;
}

TemplateSpec ConfigReader::ReadTemplateParams(const gb::ParsedItem& block) {
  TemplateSpec spec = {.name = std::string(block.GetString("name")),
                       .line = GetLine(block)};
  for (const gb::ParsedItem& param : block.GetItems("params")) {
    if (const gb::ParsedItem* widget = param.GetItem("widget")) {
      spec.widget_params.emplace_back(widget->GetString("name"));
    } else {
      const gb::ParsedItem& property = *param.GetItem("property");
      spec.property_params.push_back(
          {.name = std::string(property.GetString("name")),
           .default_value = std::string(property.GetString("default"))});
    }
  }
  return spec;
}

const TemplateSpec* ConfigReader::FindTemplate(std::string_view name) const {
  const auto it = absl::c_find_if(
      spec_.templates,
      [name](const TemplateSpec& spec) { return spec.name == name; });
  return it == spec_.templates.end() ? nullptr : &*it;
}

void ConfigReader::ReadMappings(
    absl::Span<const gb::ParsedItem> items, std::string_view scope,
    const std::optional<ViewCondition::Config>& condition,
    std::vector<MappingSpec>& mappings, std::vector<TemplateUse>& uses) {
  for (const gb::ParsedItem& item : items) {
    const std::string_view kind = item.GetMatchName();
    if (kind == "mapping") {
      mappings.push_back(ReadMapping(*item.GetItem(kind), scope, condition));
    } else if (condition.has_value()) {
      AddError({.line = GetLine(item)},
               kind == "when" ? kWhenInWhen
                              : "a \"use\" can't be in a \"when\", as a use "
                                "has no condition");
    } else if (kind == "use") {
      uses.push_back(ReadUse(*item.GetItem(kind), scope));
    } else {
      const gb::ParsedItem& when = *item.GetItem(kind);
      ReadMappings(when.GetItems("items"), scope,
                   ReadCondition(*when.GetItem("condition")), mappings, uses);
    }
  }
}

TemplateUse ConfigReader::ReadUse(const gb::ParsedItem& item,
                                  std::string_view scope) {
  TemplateUse use = {.name = std::string(item.GetString("name")),
                     .line = GetLine(item)};
  const TemplateSpec* spec = FindTemplate(use.name);
  for (const gb::ParsedItem& arg : item.GetItems("args")) {
    const std::string_view name = arg.GetString("name");
    if (use.widgets.contains(name) || use.properties.contains(name)) {
      AddError(
          {.line = GetLine(arg), .text = scope},
          absl::StrCat("uses ", use.name, " with \"", name, "\" given twice"));
      continue;
    }
    // An argument is sorted by the template's parameter, or for one the
    // template doesn't have, which checking reports, by how it is written: a
    // widget path, or a name with a namespace for a property. Checking also
    // reports an argument the parameter can't be, such as a path for a
    // property.
    const gb::ParsedItem* path = arg.GetItem("path");
    bool is_widget = path != nullptr;
    if (spec != nullptr) {
      if (absl::c_linear_search(spec->widget_params, name)) {
        is_widget = true;
      } else if (absl::c_any_of(spec->property_params,
                                [name](const PropertyParam& param) {
                                  return param.name == name;
                                })) {
        is_widget = false;
      }
    }
    (is_widget ? use.widgets : use.properties)
        .emplace(name, path != nullptr
                           ? GetPathText(*path)
                           : std::string(arg.GetString("qualified_name")));
  }
  return use;
}

MappingSpec ConfigReader::ReadMapping(
    const gb::ParsedItem& item, std::string_view scope,
    std::optional<ViewCondition::Config> condition) {
  MappingSpec mapping = {.control = GetPathText(*item.GetItem("control")),
                         .config = {.condition = std::move(condition)},
                         .line = GetLine(item)};
  const std::string subject = absl::StrCat(scope, ": ", mapping.control);
  const SpecPart what = {.line = mapping.line, .text = subject};
  const std::string_view arrow = item.GetItem("arrow")->GetMatchName();
  mapping.type = arrow == "read"    ? ViewMapping::kReadControl
                 : arrow == "write" ? ViewMapping::kWriteControl
                                    : ViewMapping::kReadWriteControl;
  ReadHow(item.GetItems("reads"), what, mapping.config.read);
  mapping.property = ReadTarget(*item.GetItem("target"), what);
  ReadOptions(item.GetItems("options"), what, mapping.config);
  return mapping;
}

void ConfigReader::ReadHow(absl::Span<const gb::ParsedItem> reads,
                           SpecPart what, ViewMapping::ReadConfig& read) {
  absl::flat_hash_set<std::string_view> given;
  for (const gb::ParsedItem& item : reads) {
    const SpecPart part = {.line = GetLine(item), .text = what.text};
    // Checking reports a modifier given twice, as the spec holds a list.
    if (const gb::ParsedItem* modifier = item.GetItem("modifier")) {
      read.required_modifiers.emplace_back(modifier->GetToken().GetString());
      continue;
    }
    const std::string_view name = item.GetString("name");
    if (!given.insert(name).second) {
      AddError(part, absl::StrCat("\"", name, "\" is given twice"));
      continue;
    }
    if (name == "held") {
      read.press_release = true;
    } else if (name == "touch") {
      read.touch = true;
    } else if (const auto behavior = absl::c_find(kPressBehaviorNames, name);
               behavior != std::end(kPressBehaviorNames)) {
      if (read.press_behavior != InputConfig::PressBehavior::kNormal) {
        AddError(part, "has more than one press behavior");
      }
      read.press_behavior =
          kPressBehaviors[behavior - std::begin(kPressBehaviorNames)];
    } else if (std::optional<ControlInput::Type> input = FindInputType(name)) {
      if (read.input_type.has_value()) {
        AddError(part, "has more than one input");
      }
      read.input_type = input;
    } else {
      AddError(part, absl::StrCat("\"", name,
                                  "\" isn't an input, a press behavior, "
                                  "\"held\", \"touch\", or a modifier"));
    }
  }
}

std::variant<std::string, FixedValue> ConfigReader::ReadTarget(
    const gb::ParsedItem& target, SpecPart what) {
  // A plain name is a property parameter of the template the mapping is in.
  if (const gb::ParsedItem* fixed = target.GetItem("fixed")) {
    return GetFixedValue(ReadFixed(*fixed, what));
  }
  return std::string(target.GetString(target.GetMatchName()));
}

void ConfigReader::ReadOptions(absl::Span<const gb::ParsedItem> options,
                               SpecPart what, ViewMapping::Config& config) {
  for (const gb::ParsedItem& option : options) {
    const SpecPart part = {.line = GetLine(option), .text = what.text};
    const std::string_view keyword = option.GetMatchName();
    const gb::ParsedItem& body = *option.GetItem(keyword);
    if (keyword == "mode") {
      // A mode name is never empty, so an empty one is none given.
      if (!config.write.mode.empty()) {
        AddError(part, "\"mode\" is given twice");
      } else {
        config.write.mode = body.GetString("name");
      }
    } else if (keyword == "when") {
      ReadModeWhen(body, part, config.write);
    } else if (config.read.property_min.has_value()) {
      AddError(part, "has more than one of \"set\", \"range\", and \"toggle\"");
    } else if (keyword == "set") {
      config.read.property_min = ReadConstant(*body.GetItem("value"), part);
      config.read.property_max = config.read.property_min;
    } else {
      config.read.property_min = ReadConstant(*body.GetItem("min"), part);
      config.read.property_max = ReadConstant(*body.GetItem("max"), part);
      config.read.press_toggles = keyword == "toggle";
    }
  }
}

void ConfigReader::ReadModeWhen(const gb::ParsedItem& when, SpecPart what,
                                ViewMapping::WriteConfig& write) {
  const ViewCondition::Config condition =
      ReadCondition(*when.GetItem("condition"));
  bool has_mode = false;
  for (const gb::ParsedItem& option : when.GetItems("items")) {
    const SpecPart part = {.line = GetLine(option), .text = what.text};
    const std::string_view keyword = option.GetMatchName();
    if (keyword == "when") {
      AddError(part, kWhenInWhen);
    } else if (keyword != "mode") {
      AddError(part,
               absl::StrCat("\"", keyword,
                            "\" can't be in a \"when\": only a mode can"));
    } else if (has_mode) {
      AddError(part, "\"mode\" is given twice");
    } else {
      has_mode = true;
      write.mode_overrides.push_back(
          {.property = condition.property,
           .value_to_mode = {
               {condition.value,
                std::string(option.GetItem("mode")->GetString("name"))}}});
    }
  }
}

ViewCondition::Config ConfigReader::ReadCondition(const gb::ParsedItem& item) {
  ViewCondition::Config condition = {
      .property = std::string(item.GetString("property"))};
  if (const gb::ParsedItem* value = item.GetItem("value")) {
    condition.value = ReadConstant(*value, {.line = GetLine(item)});
  }
  return condition;
}

ViewProperty::Value ConfigReader::ReadConstant(const gb::ParsedItem& constant,
                                               SpecPart what) {
  // A name is one of an enumerated property's values.
  if (const gb::ParsedItem* fixed = constant.GetItem("fixed")) {
    return ReadFixed(*fixed, what);
  }
  return std::string(constant.GetString("name"));
}

ViewProperty::Value ConfigReader::ReadFixed(const gb::ParsedItem& fixed,
                                            SpecPart what) {
  if (const gb::ParsedItem* color = fixed.GetItem("color")) {
    const int64_t r = color->GetInt("r");
    const int64_t g = color->GetInt("g");
    const int64_t b = color->GetInt("b");
    if (r > 255 || g > 255 || b > 255) {
      AddError(what, absl::StrCat("rgb(", r, ", ", g, ", ", b,
                                  ") has a value over 255"));
      return {};
    }
    return Color{.r = static_cast<uint8_t>(r),
                 .g = static_cast<uint8_t>(g),
                 .b = static_cast<uint8_t>(b)};
  }
  const std::string_view form = fixed.GetMatchName();
  if (form == "true" || form == "false") {
    return form == "true";
  }
  if (form == "int") {
    return static_cast<int>(fixed.GetInt("int"));
  }
  if (form == "float") {
    return fixed.GetFloat("float");
  }
  return std::string(fixed.GetString("string"));
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
  if (param.form == ValueForm::kInt) {
    result.number = static_cast<int>(value.GetInt("int"));
    return;
  }
  std::string_view text;
  if (const gb::ParsedItem* call = value.GetItem("call")) {
    text = call->GetString("name");
    result.params = call->GetItems("params");
  } else {
    text = value.GetString(value.GetMatchName());
  }
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
    if (std::optional<ControlInput::Type> input = FindInputType(name)) {
      control.inputs.Set(*input);
    } else if (std::optional<ControlOutput::Type> output =
                   FindOutputType(name)) {
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
  if (std::optional<int> filter = GetValue(values, "track_filter").choice) {
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
