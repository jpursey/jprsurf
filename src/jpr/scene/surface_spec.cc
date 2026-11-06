// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/surface_spec.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "absl/algorithm/container.h"
#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"
#include "absl/types/span.h"
#include "jpr/common/modifiers.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"
#include "jpr/scene/command_properties.h"
#include "jpr/scene/const_property.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/view_condition.h"
#include "jpr/scene/view_list.h"
#include "jpr/scene/view_reference.h"

namespace jpr {

namespace {

// Returns true if the name is one a config makes up: a C identifier, as a
// widget name is.
bool IsName(std::string_view name) { return IsWidgetName(name); }

// Returns true if the name is in the namespace, followed by a C identifier.
bool IsNameIn(std::string_view name, std::string_view name_space) {
  return name.starts_with(name_space) && IsName(name.substr(name_space.size()));
}

// Returns the catalog's namespace the name is in, or null if it is in none.
const PropertyNamespaceInfo* FindNamespace(std::string_view name) {
  for (const PropertyNamespaceInfo& info : GetPropertyCatalog()) {
    if (name.starts_with(info.name)) {
      return &info;
    }
  }
  return nullptr;
}

// Returns the row with the name, a property's or a reference's, or null if
// there is none.
template <typename Info>
const Info* FindInfo(absl::Span<const Info> rows, std::string_view name) {
  auto it = std::ranges::find(rows, name, &Info::name);
  return it != rows.end() ? &*it : nullptr;
}

// Returns the name of a template's parameter, a widget's or a property's.
std::string_view GetParamName(const std::string& param) { return param; }
std::string_view GetParamName(const PropertyParam& param) { return param.name; }

// Returns true if a use of the template may leave out the parameter.
bool HasDefault(const std::string&) { return false; }
bool HasDefault(const PropertyParam& param) {
  return !param.default_value.empty();
}

// What checking knows of a view, which its contents and children see.
struct ViewScope {
  // Returns the view, as errors about it start.
  SpecPart GetPart() const { return {.line = line, .text = path}; }

  std::string path;  // From the root, which is "/".
  SpecLine line;     // The view's.

  // The kind of its subject, or nullopt if it isn't known, as the reference
  // it is bound to isn't one.
  std::optional<SubjectKind> subject = SubjectKind::kNone;

  // The kind of its list's items, or nullopt if it has no list.
  std::optional<SubjectKind> list;

  // The needs of view: properties it meets, and those an item view of its
  // list meets (see ViewNeed).
  std::vector<ViewNeed> needs;
  std::optional<ViewNeed> item_need;
};

// The checking of one surface spec.
class SurfaceSpecValidator {
 public:
  explicit SurfaceSpecValidator(std::vector<std::string>* errors);

  bool Validate(const SurfaceSpec& spec);

 private:
  // The element of a repeated view, which paths in it and its descendants can
  // start with.
  struct Element {
    std::string_view name;
    const WidgetShape* shape = nullptr;  // Null if it isn't known.
  };

  // A reference, built in or declared.
  struct Reference {
    SubjectKind kind = SubjectKind::kNone;
    bool declared = false;  // Declared ones are writable.
  };

  // What checking knows of a property a mapping or condition names.
  struct Property {
    // How errors name it, such as "\"user:mode\"".
    std::string label;

    // Its type, or nullopt if it isn't known until the scene is created: a
    // command's, which REAPER decides, or a field of a subject whose kind
    // isn't known. Its access is known exactly when its type is.
    std::optional<ViewProperty::Type> type;
    PropertyAccessFlags access;

    // Its declaration, if it is a declared enumerated value, whose values have
    // names.
    const EnumeratedValueSpec* values = nullptr;
  };

  void ValidateDevicesAndWidgets(const SurfaceSpec& spec);

  // Validates a component declared at the top level if `view` is null, and on
  // the view otherwise. Returns true if it declared its name, which is then
  // visible until it is removed from `visible_names_`.
  bool ValidateComponent(const ComponentSpec& component, const ViewScope* view);
  void ValidateParams(const ModifierSpec& modifier, SpecPart what,
                      const ViewScope* view);
  void ValidateParams(const EnumeratedValueSpec& value, SpecPart what,
                      const ViewScope* view);
  void ValidateParams(const TrackReferenceSpec& reference, SpecPart what,
                      const ViewScope* view);
  void ValidateParams(const TrackAnchorSpec& anchor, SpecPart what,
                      const ViewScope* view);
  void ValidateParams(const TrackPickSpec& pick, SpecPart what,
                      const ViewScope* view);

  // Adds an error unless the parameter, a track reference, is empty or a
  // track reference, built in or declared before it.
  void ValidateTrackReference(std::string_view param, std::string_view name,
                              SpecPart what);

  // Adds an error if the component, on a view, isn't on one with a track
  // subject.
  void ValidateTrackSubject(SpecPart what, const ViewScope* view);

  // Adds an error about `what` unless the name, the `label` of something new
  // (such as "value"), is a C identifier, not already in `names`, which it is
  // added to.
  void ValidateNewName(SpecPart what, std::string_view label,
                       std::string_view name,
                       absl::flat_hash_set<std::string_view>& names);

  void ValidateTemplates(absl::Span<const TemplateSpec> templates);

  // Validates a template's parameters of a kind ("widget" or "property"),
  // whose names are new among `names`, which this adds them to.
  template <typename Param>
  void ValidateTemplateParams(const std::vector<Param>& params,
                              std::string_view kind, SpecPart what,
                              absl::flat_hash_set<std::string_view>& names);

  // Validates the defaults of a template's property parameters.
  void ValidateDefaults(const std::vector<PropertyParam>& params,
                        SpecPart what);

  // Returns true if the template uses the target, directly or through other
  // templates, adding the templates it uses it through to `through`. The
  // templates in `visited` aren't followed again.
  bool UsesTemplate(const TemplateSpec& from, std::string_view target,
                    absl::flat_hash_set<std::string_view>& visited,
                    std::vector<std::string_view>& through) const;

  // Validates a use, in the template or view `where` names, whose errors have
  // the use's line.
  void ValidateUse(const TemplateUse& use, std::string_view where);

  // Validates a use's arguments for the template's parameters of a kind
  // ("widget" or "property").
  template <typename Param>
  void ValidateArgs(const std::vector<Param>& params,
                    const absl::flat_hash_map<std::string, std::string>& args,
                    std::string_view kind, std::string_view template_name,
                    SpecPart what);

  void ValidateRoot(const ViewSpec& root);
  void ValidateView(const ViewSpec& view, const ViewScope& parent);

  // Validates the view's subject, and sets the scope's subject.
  void ValidateSubject(const ViewSpec& view, const ViewScope& parent,
                       ViewScope& scope);

  // Validates the view's list, and sets the scope's list.
  void ValidateList(const View::Config& config, ViewScope& scope);

  // Validates the repeated view's array and element, and returns its element.
  Element ValidateRepeat(const RepeatSpec& repeat, const ViewScope& scope);

  // Validates the view's components, uses, mappings, and children.
  void ValidateContents(const ViewSpec& view, const ViewScope& scope);
  void ValidateChildren(const ViewSpec& view, const ViewScope& scope);

  // Validates the mappings of a use in the view, with the template's widget
  // parameters resolved where the use is, as are the property arguments in a
  // template, and the mappings of the uses in the template in turn. `where` is
  // the view's path, followed by the templates the use is in.
  void ValidateUseMappings(const TemplateUse& use, const ViewScope& scope,
                           std::string_view where);

  // Validates a mapping in the view, whose errors have its line, and start
  // with `text` after it.
  void ValidateMapping(const MappingSpec& mapping, const ViewScope& scope,
                       std::string_view text);

  // Validates the mapping's property, its read and write sides, and its
  // modes, on the control of the shape, or null if it isn't known.
  void ValidateMappingProperty(const MappingSpec& mapping,
                               const Property& property,
                               const WidgetShape* control, SpecPart what);

  // Validates each side the mapping asks for, of a property whose type is
  // known, on the control.
  void ValidateSides(const MappingSpec& mapping, const Property& property,
                     const WidgetShape& control, SpecPart what);

  // Adds an error unless the control has the mode, for a property of the type.
  void ValidateMode(ViewProperty::Type type, const WidgetShape& control,
                    std::string_view mode, SpecPart what);

  // Validates an end of a read mapping's range (its `label`, "min" or "max").
  void ValidateRangeEnd(const Property& property,
                        const std::optional<ViewProperty::Value>& end,
                        std::string_view label, SpecPart what);

  // Validates a condition in the view, of a view or a mapping, whose errors
  // start with `what`.
  void ValidateCondition(const ViewCondition::Config& condition,
                         const ViewScope& scope, SpecPart what);

  // Returns the property the name in the view names, the `label` of its use
  // (such as "condition"), adding an error if there is none. Returns a
  // property whose type isn't known if the name is a property parameter
  // without an argument, which validating the use reports.
  std::optional<Property> ValidateProperty(std::string_view name,
                                           const ViewScope& scope,
                                           std::string_view label,
                                           SpecPart what);

  // Adds an error unless the property, the `label` of its use, can be read.
  void ValidateReadable(const Property& property, std::string_view label,
                        SpecPart what);

  // Adds an error unless the property can equal the value (see
  // ViewProperty::CanEqual()), naming one of its values if it is a string for
  // an enumerated property (see ViewProperty::ResolveValue()). `label` is the
  // value's use.
  void ValidateValue(const Property& property, const ViewProperty::Value& value,
                     std::string_view label, SpecPart what);

  // Returns the shape of the widget at the path's text, looking up its first
  // name from the inside out (see SurfaceSpec). Returns null, setting `error`
  // to why, if it isn't a path, or names nothing, and null, leaving `error`
  // empty, if what it names isn't known.
  const WidgetShape* FindWidget(std::string_view path_text,
                                std::string* error) const;

  // Returns the property the name names in the view, looking it up as View
  // and Scene do. Returns nullopt, setting `error` to why, if there is none.
  std::optional<Property> FindProperty(std::string_view name,
                                       const ViewScope& scope,
                                       std::string* error) const;

  // Returns the field of a subject of the kind, such as "name" for a track,
  // or "other_track.name" for a route, as FindProperty() does.
  std::optional<Property> FindField(SubjectKind kind, std::string_view field,
                                    std::string* error) const;

  // Returns the name a property name in a template stands for: its argument,
  // if it is a property parameter, or itself. Returns nullopt if it is a
  // parameter without an argument.
  std::optional<std::string_view> ResolvePropertyName(
      std::string_view name) const;

  // Returns true if the name is a reference whose subject is a track, built in
  // or declared so far, or one declared so far.
  bool IsTrackReference(std::string_view name) const;
  bool IsDeclaredReference(std::string_view name) const;

  // Returns true if the name is a modifier, built in or declared. Modifiers
  // are declared only at the top level, so they are always visible.
  bool IsModifier(std::string_view name) const;

  // Adds an error about `what` (see SpecPart).
  void AddError(SpecPart what, std::string_view message);

  std::vector<std::string>* const errors_;
  bool valid_ = true;

  // The top-level widgets' shape, if they are valid.
  std::optional<WidgetShape> widgets_;

  // The elements of the repeated views being validated, outermost first, or
  // while a template's mappings are, its widget parameters.
  std::vector<Element> elements_;

  // While a template's mappings are validated, the arguments of its property
  // parameters (see ResolvePropertyName()), and the templates being expanded,
  // which aren't expanded again inside themselves.
  absl::flat_hash_map<std::string_view, std::optional<std::string_view>>
      property_args_;
  absl::flat_hash_set<std::string_view> expanding_;

  // The names declared, or built in, that a new declaration may not have: the
  // built in modifiers, the top-level declarations, and the declarations of
  // the view being validated and its ancestors, each with its declaration
  // (null for a built in modifier). Every name is the spec's, or the
  // catalog's, which outlive the validator.
  absl::flat_hash_map<std::string_view, const ComponentSpec*> visible_names_;
  int declared_modifier_count_ = 0;

  // References, built in and declared so far.
  absl::flat_hash_map<std::string_view, Reference> references_;

  // The first template with each name.
  absl::flat_hash_map<std::string_view, const TemplateSpec*> templates_;
};

SurfaceSpecValidator::SurfaceSpecValidator(std::vector<std::string>* errors)
    : errors_(errors) {
  for (const PropertyInfo& modifier : Scene::GetBuiltInModifierInfo()) {
    visible_names_.emplace(modifier.name, nullptr);
  }
  for (const ReferenceInfo& reference : Scene::GetBuiltInReferenceInfo()) {
    references_.emplace(reference.name, Reference{.kind = reference.kind});
  }
}

bool SurfaceSpecValidator::Validate(const SurfaceSpec& spec) {
  ValidateDevicesAndWidgets(spec);
  for (const ComponentSpec& component : spec.components) {
    ValidateComponent(component, nullptr);
  }
  ValidateTemplates(spec.templates);
  ValidateRoot(spec.root);
  return valid_;
}

void SurfaceSpecValidator::ValidateDevicesAndWidgets(const SurfaceSpec& spec) {
  std::optional<std::vector<WidgetDevice>> devices =
      CreateWidgetDevices(spec.devices, errors_);
  if (!devices.has_value()) {
    valid_ = false;
    return;
  }
  widgets_ = ValidateWidgets(spec.widgets, *devices, errors_);
  if (!widgets_.has_value()) {
    valid_ = false;
  }
}

bool SurfaceSpecValidator::ValidateComponent(const ComponentSpec& component,
                                             const ViewScope* view) {
  const ComponentKindInfo& kind = GetComponentKind(component);
  const std::string_view name = GetComponentName(component);
  const SpecLine line = GetComponentLine(component);
  const std::string text = view != nullptr
                               ? absl::StrCat(view->path, ": ", name)
                               : std::string(name);
  const SpecPart what = {.line = line, .text = text};
  if (view == nullptr ? !kind.top_level : !kind.on_view) {
    AddError(
        what,
        absl::StrCat("is of kind \"", kind.name, "\", which can't be declared ",
                     view == nullptr ? "at the top level" : "on a view"));
  }

  bool declared = false;
  if (!IsNameIn(name, kind.name_space)) {
    AddError({.line = line, .text = view != nullptr ? view->path : ""},
             absl::StrCat(kind.name, " name \"", name, "\" isn't \"",
                          kind.name_space, "\" followed by a C identifier"));
  } else if (!visible_names_.emplace(name, &component).second) {
    AddError(what, "is already declared where it is visible, or is built in");
  } else {
    declared = true;
  }
  // Only a declared modifier takes one of the scene's modifier flags.
  if (declared && std::holds_alternative<ModifierSpec>(component) &&
      ++declared_modifier_count_ > kModUserCount) {
    AddError(what,
             absl::StrCat("is modifier ", declared_modifier_count_, ", of the ",
                          kModUserCount, " a config can declare"));
  }
  std::visit([this, what,
              view](const auto& spec) { ValidateParams(spec, what, view); },
             component);
  return declared;
}

void SurfaceSpecValidator::ValidateParams(const ModifierSpec& modifier,
                                          SpecPart what,
                                          const ViewScope* view) {
  // A modifier has no parameters (see ValidateComponent()).
}

void SurfaceSpecValidator::ValidateParams(const EnumeratedValueSpec& value,
                                          SpecPart what,
                                          const ViewScope* view) {
  if (value.values.empty()) {
    AddError(what, "has no values");
  }
  absl::flat_hash_set<std::string_view> names;
  for (const std::string& name : value.values) {
    ValidateNewName(what, "value", name, names);
  }
  if (!value.initial.empty() && !names.contains(value.initial)) {
    AddError(what, absl::StrCat("initial value \"", value.initial,
                                "\" isn't one of its values"));
  }
}

void SurfaceSpecValidator::ValidateParams(const TrackReferenceSpec& reference,
                                          SpecPart what,
                                          const ViewScope* view) {
  ValidateTrackReference("fallback", reference.fallback, what);
  ValidateTrackReference("follow", reference.follow, what);
  references_.emplace(reference.name,
                      Reference{.kind = SubjectKind::kTrack, .declared = true});
}

void SurfaceSpecValidator::ValidateParams(const TrackAnchorSpec& anchor,
                                          SpecPart what,
                                          const ViewScope* view) {
  if (!anchor.modifier.empty() && !IsModifier(anchor.modifier)) {
    AddError(what, absl::StrCat("modifier \"", anchor.modifier,
                                "\" isn't a modifier"));
  }
  ValidateTrackSubject(what, view);
}

void SurfaceSpecValidator::ValidateParams(const TrackPickSpec& pick,
                                          SpecPart what,
                                          const ViewScope* view) {
  if (pick.reference.empty()) {
    AddError(what, "has no reference");
  } else if (!IsDeclaredReference(pick.reference)) {
    AddError(what, absl::StrCat("reference \"", pick.reference,
                                "\" isn't a track reference declared before "
                                "it"));
  }
  if (view != nullptr) {
    if (!pick.source.empty()) {
      AddError(what, "has a source, which only a pick at the top level has");
    }
    ValidateTrackSubject(what, view);
  } else if (pick.source.empty()) {
    AddError(what, "has no source");
  } else {
    ValidateTrackReference("source", pick.source, what);
  }
}

void SurfaceSpecValidator::ValidateTrackReference(std::string_view param,
                                                  std::string_view name,
                                                  SpecPart what) {
  if (!name.empty() && !IsTrackReference(name)) {
    AddError(what, absl::StrCat(param, " \"", name,
                                "\" isn't a track reference, built in or "
                                "declared before it"));
  }
}

void SurfaceSpecValidator::ValidateTrackSubject(SpecPart what,
                                                const ViewScope* view) {
  if (view != nullptr && view->subject.has_value() &&
      *view->subject != SubjectKind::kTrack) {
    AddError(what, "is on a view without a track subject");
  }
}

void SurfaceSpecValidator::ValidateNewName(
    SpecPart what, std::string_view label, std::string_view name,
    absl::flat_hash_set<std::string_view>& names) {
  if (!IsName(name)) {
    AddError(what, absl::StrCat(label, " \"", name, "\" isn't a C identifier"));
  } else if (!names.insert(name).second) {
    AddError(what, absl::StrCat(label, " \"", name, "\" is repeated"));
  }
}

void SurfaceSpecValidator::ValidateTemplates(
    absl::Span<const TemplateSpec> templates) {
  for (const TemplateSpec& spec : templates) {
    const std::string text = absl::StrCat("template ", spec.name);
    const SpecPart what = {.line = spec.line, .text = text};
    if (!IsName(spec.name)) {
      AddError({.line = spec.line}, absl::StrCat("template name \"", spec.name,
                                                 "\" isn't a C identifier"));
    }
    if (!templates_.emplace(spec.name, &spec).second) {
      AddError(what, "has the name of another template");
    }
    // A use names its arguments by their parameters, so a widget parameter
    // and a property parameter don't share a name.
    absl::flat_hash_set<std::string_view> names;
    ValidateTemplateParams(spec.widget_params, "widget", what, names);
    ValidateTemplateParams(spec.property_params, "property", what, names);
    ValidateDefaults(spec.property_params, what);
  }

  // Uses can name any template, so they are validated once every template is
  // known.
  for (const TemplateSpec& spec : templates) {
    const std::string text = absl::StrCat("template ", spec.name);
    const SpecPart what = {.line = spec.line, .text = text};
    for (const TemplateUse& use : spec.uses) {
      ValidateUse(use, text);
    }
    absl::flat_hash_set<std::string_view> visited;
    std::vector<std::string_view> through;
    if (UsesTemplate(spec, spec.name, visited, through)) {
      AddError(what, through.empty()
                         ? "uses itself"
                         : absl::StrCat("uses itself, through ",
                                        absl::StrJoin(through, ", ")));
    }
  }
}

template <typename Param>
void SurfaceSpecValidator::ValidateTemplateParams(
    const std::vector<Param>& params, std::string_view kind, SpecPart what,
    absl::flat_hash_set<std::string_view>& names) {
  const std::string label = absl::StrCat(kind, " parameter");
  for (const Param& param : params) {
    ValidateNewName(what, label, GetParamName(param), names);
  }
}

void SurfaceSpecValidator::ValidateDefaults(
    const std::vector<PropertyParam>& params, SpecPart what) {
  for (auto param = params.begin(); param != params.end(); ++param) {
    if (HasDefault(*param) &&
        std::ranges::find(param, params.end(), param->default_value,
                          &PropertyParam::name) != params.end()) {
      AddError(what, absl::StrCat("property parameter \"", param->name,
                                  "\" defaults to \"", param->default_value,
                                  "\", which isn't an earlier parameter"));
    }
  }
}

bool SurfaceSpecValidator::UsesTemplate(
    const TemplateSpec& from, std::string_view target,
    absl::flat_hash_set<std::string_view>& visited,
    std::vector<std::string_view>& through) const {
  for (const TemplateUse& use : from.uses) {
    if (use.name == target) {
      return true;
    }
    auto it = templates_.find(use.name);
    if (it == templates_.end() || !visited.insert(use.name).second) {
      continue;
    }
    through.push_back(use.name);
    if (UsesTemplate(*it->second, target, visited, through)) {
      return true;
    }
    through.pop_back();
  }
  return false;
}

void SurfaceSpecValidator::ValidateUse(const TemplateUse& use,
                                       std::string_view where) {
  const SpecPart what = {.line = use.line, .text = where};
  auto it = templates_.find(use.name);
  if (it == templates_.end()) {
    AddError(what,
             absl::StrCat("uses \"", use.name, "\", which isn't a template"));
    return;
  }
  ValidateArgs(it->second->widget_params, use.widgets, "widget", use.name,
               what);
  ValidateArgs(it->second->property_params, use.properties, "property",
               use.name, what);
}

template <typename Param>
void SurfaceSpecValidator::ValidateArgs(
    const std::vector<Param>& params,
    const absl::flat_hash_map<std::string, std::string>& args,
    std::string_view kind, std::string_view template_name, SpecPart what) {
  for (const Param& param : params) {
    if (!HasDefault(param) && !args.contains(GetParamName(param))) {
      AddError(what, absl::StrCat("uses ", template_name, " without its ", kind,
                                  " parameter \"", GetParamName(param), "\""));
    }
  }
  // The arguments are in no order, so they are sorted for errors to be.
  std::vector<std::string_view> others;
  for (const auto& [arg, value] : args) {
    if (std::ranges::none_of(params, [&](const Param& param) {
          return GetParamName(param) == arg;
        })) {
      others.push_back(arg);
    }
  }
  std::ranges::sort(others);
  for (std::string_view arg : others) {
    AddError(what,
             absl::StrCat("uses ", template_name, " with \"", arg,
                          "\", which isn't one of its ", kind, " parameters"));
  }
}

void SurfaceSpecValidator::ValidateRoot(const ViewSpec& root) {
  const ViewScope scope = {.path = "/", .line = root.line};
  if (!root.name.empty()) {
    AddError(scope.GetPart(), "the root view has no name");
  }
  if (root.config.condition.has_value()) {
    AddError(scope.GetPart(), "the root view has no condition");
  }
  if (!std::holds_alternative<View::ParentSubject>(root.config.subject)) {
    AddError(scope.GetPart(), "the root view has no subject");
  }
  if (root.config.list.has_value()) {
    AddError(scope.GetPart(), "the root view has no list");
  }
  if (root.repeat.has_value()) {
    AddError(scope.GetPart(), "the root view isn't repeated");
  }
  ValidateContents(root, scope);
}

void SurfaceSpecValidator::ValidateView(const ViewSpec& view,
                                        const ViewScope& parent) {
  ViewScope scope;
  scope.path = GetViewPath(parent.path, view);
  scope.line = view.line;
  if (view.repeat.has_value()) {
    if (!view.name.empty()) {
      AddError(scope.GetPart(),
               absl::StrCat("is repeated, so it has no name, but "
                            "is named \"",
                            view.name, "\""));
    }
  } else if (!IsName(view.name)) {
    AddError(
        {.line = view.line, .text = parent.path},
        absl::StrCat("view name \"", view.name, "\" isn't a C identifier"));
  }

  ValidateSubject(view, parent, scope);
  ValidateList(view.config, scope);

  // A view's condition is made with its config, before its own components
  // are declared.
  if (view.config.condition.has_value()) {
    ValidateCondition(*view.config.condition, scope, scope.GetPart());
  }

  // An item view gets its parent list's view: properties once it is made.
  if (std::holds_alternative<View::ListItemSubject>(view.config.subject) &&
      parent.item_need.has_value()) {
    scope.needs.push_back(*parent.item_need);
  }
  if (!view.repeat.has_value()) {
    ValidateContents(view, scope);
    return;
  }
  elements_.push_back(ValidateRepeat(*view.repeat, scope));
  ValidateContents(view, scope);
  elements_.pop_back();
}

void SurfaceSpecValidator::ValidateSubject(const ViewSpec& view,
                                           const ViewScope& parent,
                                           ViewScope& scope) {
  const bool is_item =
      std::holds_alternative<View::ListItemSubject>(view.config.subject);
  if (view.repeat.has_value() != is_item) {
    AddError(scope.GetPart(),
             view.repeat.has_value()
                 ? "is repeated, but its subject isn't an item "
                   "of its parent's list"
                 : "shows an item of its parent's list, but isn't "
                   "repeated");
  }
  if ((view.repeat.has_value() || is_item) && !parent.list.has_value()) {
    AddError(scope.GetPart(),
             "shows an item of its parent's list, but its parent has no list");
  }

  if (is_item) {
    scope.subject = parent.list;
  } else if (const auto* subject =
                 std::get_if<View::ReferenceSubject>(&view.config.subject)) {
    auto it = references_.find(subject->name);
    if (it == references_.end()) {
      AddError(scope.GetPart(), absl::StrCat("is bound to \"", subject->name,
                                             "\", which isn't a reference"));
      scope.subject = std::nullopt;
    } else {
      scope.subject = it->second.kind;
    }
  } else {
    scope.subject = parent.subject;
  }
}

void SurfaceSpecValidator::ValidateList(const View::Config& config,
                                        ViewScope& scope) {
  if (!config.list.has_value()) {
    return;
  }
  const View::ListConfig& list = *config.list;
  if (scope.subject.has_value() && *scope.subject != SubjectKind::kTrack) {
    AddError(scope.GetPart(), "has a list, but no track subject");
  }
  if (list.bank_size.has_value() && *list.bank_size <= 0) {
    AddError(scope.GetPart(),
             absl::StrCat("has a bank size of ", *list.bank_size,
                          ", which isn't positive"));
  }

  // A view may set the reference it is bound to only if it is declared.
  const auto* bound = std::get_if<View::ReferenceSubject>(&config.subject);
  const bool writable = bound != nullptr && IsDeclaredReference(bound->name);
  ViewList::Needs needs = ViewList::GetNeeds(list, writable);
  scope.needs = std::move(needs.view);
  scope.item_need = needs.item;
  if (const auto* child_tracks = std::get_if<View::ChildTracks>(&list.items)) {
    const std::string& reveal = child_tracks->reveal;
    if (!reveal.empty()) {
      // Revealing moves the reference the view is bound to.
      if (!IsTrackReference(reveal)) {
        AddError(scope.GetPart(),
                 absl::StrCat("reveals \"", reveal,
                              "\", which isn't a track reference"));
      } else if (!writable || bound->name == reveal) {
        AddError(scope.GetPart(),
                 absl::StrCat("reveals \"", reveal,
                              "\", but isn't bound to a declared "
                              "reference other than it"));
      }
    }
    scope.list = SubjectKind::kTrack;
  } else {
    scope.list = SubjectKind::kRoute;
  }
}

SurfaceSpecValidator::Element SurfaceSpecValidator::ValidateRepeat(
    const RepeatSpec& repeat, const ViewScope& scope) {
  if (!IsName(repeat.element)) {
    AddError(scope.GetPart(), absl::StrCat("element \"", repeat.element,
                                           "\" isn't a C identifier"));
  }
  std::string error;
  const WidgetShape* array = FindWidget(repeat.array, &error);
  if (!error.empty()) {
    AddError(scope.GetPart(),
             absl::StrCat("array \"", repeat.array, "\" ", error));
  } else if (array != nullptr && array->GetKind() != WidgetKind::kArray) {
    AddError(scope.GetPart(),
             absl::StrCat("array \"", repeat.array, "\" isn't an array"));
    array = nullptr;
  }
  // The element is returned even if it isn't known, so that it hides any
  // outer name, as it will once it is.
  return {.name = repeat.element,
          .shape = array != nullptr ? array->GetElement() : nullptr};
}

void SurfaceSpecValidator::ValidateContents(const ViewSpec& view,
                                            const ViewScope& scope) {
  std::vector<std::string_view> declared;
  for (const ComponentSpec& component : view.components) {
    if (ValidateComponent(component, &scope)) {
      declared.push_back(GetComponentName(component));
    }
  }
  for (const TemplateUse& use : view.uses) {
    ValidateUse(use, scope.path);
  }
  for (const MappingSpec& mapping : view.mappings) {
    ValidateMapping(mapping, scope,
                    absl::StrCat(scope.path, ": ", mapping.control));
  }
  for (const TemplateUse& use : view.uses) {
    ValidateUseMappings(use, scope, scope.path);
  }
  ValidateChildren(view, scope);

  // A view's declarations are visible only in it, and its descendants.
  for (std::string_view name : declared) {
    visible_names_.erase(name);
  }
}

void SurfaceSpecValidator::ValidateChildren(const ViewSpec& view,
                                            const ViewScope& scope) {
  absl::flat_hash_set<std::string_view> names;
  int repeated_count = 0;
  for (const ViewSpec& child : view.children) {
    if (child.repeat.has_value()) {
      ++repeated_count;
    } else if (IsName(child.name) && !names.insert(child.name).second) {
      AddError({.line = child.line, .text = scope.path},
               absl::StrCat("has more than one child view named \"", child.name,
                            "\""));
    }
    ValidateView(child, scope);
  }
  if (scope.list.has_value() && repeated_count != 1) {
    AddError(scope.GetPart(),
             absl::StrCat("has a list, so it has one repeated child, but has ",
                          repeated_count));
  }
}

void SurfaceSpecValidator::ValidateUseMappings(const TemplateUse& use,
                                               const ViewScope& scope,
                                               std::string_view where) {
  // A use of a template that isn't one, or that uses itself, was reported by
  // ValidateUse() or ValidateTemplates().
  auto it = templates_.find(use.name);
  if (it == templates_.end() || expanding_.contains(use.name)) {
    return;
  }
  const TemplateSpec& spec = *it->second;

  // The arguments are resolved where the use is. One that is missing was
  // reported by ValidateUse(), so it isn't known.
  std::vector<Element> widget_params;
  for (const std::string& param : spec.widget_params) {
    const WidgetShape* shape = nullptr;
    if (auto arg = use.widgets.find(param); arg != use.widgets.end()) {
      std::string error;
      shape = FindWidget(arg->second, &error);
      if (!error.empty()) {
        AddError({.line = use.line, .text = where},
                 absl::StrCat("uses ", use.name, " with widget \"", param,
                              "\" as \"", arg->second, "\", which ", error));
      }
    }
    widget_params.push_back({.name = param, .shape = shape});
  }
  // So is a default that isn't valid, which ValidateTemplates() reported.
  absl::flat_hash_map<std::string_view, std::optional<std::string_view>>
      property_args;
  const std::vector<std::optional<PropertyArg>> args =
      GetPropertyArgs(spec, use);
  for (int i = 0; i < std::ssize(args); ++i) {
    std::optional<std::string_view>& name =
        property_args[spec.property_params[i].name];
    if (args[i].has_value()) {
      name = args[i]->is_default ? args[i]->name
                                 : ResolvePropertyName(args[i]->name);
    }
  }

  // A template sees only its own parameters and the top-level widgets.
  std::swap(elements_, widget_params);
  std::swap(property_args_, property_args);
  expanding_.insert(use.name);
  const std::string what = absl::StrCat(where, ": ", use.name);
  for (const MappingSpec& mapping : spec.mappings) {
    ValidateMapping(mapping, scope, absl::StrCat(what, ": ", mapping.control));
  }
  for (const TemplateUse& inner : spec.uses) {
    ValidateUseMappings(inner, scope, what);
  }
  expanding_.erase(use.name);
  std::swap(property_args_, property_args);
  std::swap(elements_, widget_params);
}

void SurfaceSpecValidator::ValidateMapping(const MappingSpec& mapping,
                                           const ViewScope& scope,
                                           std::string_view text) {
  const SpecPart what = {.line = mapping.line, .text = text};
  if (mapping.type.IsEmpty()) {
    AddError(what, "neither reads nor writes its control");
  }

  std::string error;
  const WidgetShape* control = FindWidget(mapping.control, &error);
  if (!error.empty()) {
    AddError(what, error);
  } else if (control != nullptr && control->GetKind() != WidgetKind::kControl) {
    AddError(what, "isn't a control");
    control = nullptr;
  }

  const ViewMapping::Config& config = mapping.config;
  std::optional<Property> property;
  if (const auto* fixed = std::get_if<FixedValue>(&mapping.property)) {
    if (!IsConstValue(fixed->type, fixed->value)) {
      AddError(what, absl::StrCat("has a fixed value that isn't a ",
                                  ViewProperty::GetTypeName(fixed->type),
                                  " constant"));
    }
    property = Property{
        .label = "its fixed value", .type = fixed->type, .access = kReadAccess};
  } else {
    property = ValidateProperty(std::get<std::string>(mapping.property), scope,
                                "property", what);
  }
  if (property.has_value()) {
    ValidateMappingProperty(mapping, *property, control, what);
  }

  absl::flat_hash_set<std::string_view> modifiers;
  for (const std::string& modifier : config.read.required_modifiers) {
    if (!IsModifier(modifier)) {
      AddError(what, absl::StrCat("requires \"", modifier,
                                  "\", which isn't a modifier"));
    } else if (!modifiers.insert(modifier).second) {
      AddError(what, absl::StrCat("requires \"", modifier, "\" twice"));
    }
  }
  for (const ViewMapping::ModeOverride& mode_override :
       config.write.mode_overrides) {
    std::optional<Property> override_property =
        ValidateProperty(mode_override.property, scope, "mode override", what);
    if (!override_property.has_value()) {
      continue;
    }
    ValidateReadable(*override_property, "mode override", what);
    for (const auto& [value, mode] : mode_override.value_to_mode) {
      ValidateValue(*override_property, value, "mode override", what);
    }
  }
  if (config.condition.has_value()) {
    ValidateCondition(*config.condition, scope, what);
  }
}

void SurfaceSpecValidator::ValidateMappingProperty(const MappingSpec& mapping,
                                                   const Property& property,
                                                   const WidgetShape* control,
                                                   SpecPart what) {
  const ViewMapping::Config& config = mapping.config;
  if (property.type.has_value()) {
    ValidateRangeEnd(property, config.read.property_min, "min", what);
    ValidateRangeEnd(property, config.read.property_max, "max", what);
    if (control != nullptr) {
      ValidateSides(mapping, property, *control, what);
    }
  }
  if (control == nullptr) {
    return;
  }

  // Only a timeline position's modes are formats, and a property whose type
  // isn't known is never one.
  const ViewProperty::Type mode_type =
      property.type.value_or(ViewProperty::Type::kAction);
  ValidateMode(mode_type, *control, config.write.mode, what);
  for (const ViewMapping::ModeOverride& mode_override :
       config.write.mode_overrides) {
    for (const auto& [value, mode] : mode_override.value_to_mode) {
      ValidateMode(mode_type, *control, mode, what);
    }
  }
}

void SurfaceSpecValidator::ValidateSides(const MappingSpec& mapping,
                                         const Property& property,
                                         const WidgetShape& control,
                                         SpecPart what) {
  // Each side the mapping asks for must be one the scene keeps (see
  // ViewMapping), and access is checked first for its own error. A shape
  // doesn't say whether a press has a release, which every press in a device
  // type's catalog has.
  const std::string_view type_name = ViewProperty::GetTypeName(*property.type);
  if (mapping.type.IsSet(ViewMapping::kReadControl)) {
    if (!ViewMapping::CanReadControlInto(property.access)) {
      AddError(what, absl::StrCat("reads into ", property.label,
                                  ", which can't be written or triggered"));
    } else if (!ViewMapping::GetReadInput(
                    *property.type, property.access, control.GetInputs(),
                    /*has_press_release=*/true, mapping.config.read)
                    .has_value()) {
      AddError(what,
               absl::StrCat("can't read into ", property.label, " (", type_name,
                            ") from the control with its read config"));
    }
  }
  if (mapping.type.IsSet(ViewMapping::kWriteControl)) {
    if (!ViewMapping::CanWriteControlFrom(property.access)) {
      AddError(what, absl::StrCat("writes ", property.label,
                                  ", which can't be read"));
    } else if (!ViewMapping::GetWriteOutput(*property.type, property.access,
                                            control.GetOutputs())
                    .has_value()) {
      AddError(what, absl::StrCat("can't write ", property.label, " (",
                                  type_name, ") to the control"));
    }
  }
}

void SurfaceSpecValidator::ValidateMode(ViewProperty::Type type,
                                        const WidgetShape& control,
                                        std::string_view mode, SpecPart what) {
  if (!ViewMapping::FindMode(type, control, mode).has_value()) {
    AddError(what, absl::StrCat("has no mode \"", mode, "\""));
  }
}

void SurfaceSpecValidator::ValidateRangeEnd(
    const Property& property, const std::optional<ViewProperty::Value>& end,
    std::string_view label, SpecPart what) {
  if (!end.has_value()) {
    return;
  }
  // What a range end of the type is (see ViewMapping::ReadConfig).
  bool valid = false;
  switch (*property.type) {
    case ViewProperty::Type::kPan:
    case ViewProperty::Type::kVolume:
    case ViewProperty::Type::kNormalized:
      valid = std::holds_alternative<double>(*end);
      break;
    case ViewProperty::Type::kText:
      valid = std::holds_alternative<std::string>(*end);
      break;
    case ViewProperty::Type::kColor:
      valid = std::holds_alternative<Color>(*end);
      break;
    case ViewProperty::Type::kEnumerated:
      if (std::holds_alternative<std::string>(*end)) {
        ValidateValue(property, *end, absl::StrCat("range ", label), what);
        return;
      }
      valid = std::holds_alternative<int>(*end);
      break;
    case ViewProperty::Type::kAction:
    case ViewProperty::Type::kToggle:
    case ViewProperty::Type::kTimelinePosition:
      AddError(
          what,
          absl::StrCat("has a range ", label, ", which ", property.label, " (",
                       ViewProperty::GetTypeName(*property.type), ") ignores"));
      return;
  }
  if (!valid) {
    AddError(what, absl::StrCat(
                       "range ", label, " isn't a value of ", property.label,
                       " (", ViewProperty::GetTypeName(*property.type), ")"));
  }
}

void SurfaceSpecValidator::ValidateCondition(
    const ViewCondition::Config& condition, const ViewScope& scope,
    SpecPart what) {
  std::optional<Property> property =
      ValidateProperty(condition.property, scope, "condition", what);
  if (property.has_value()) {
    ValidateReadable(*property, "condition", what);
    ValidateValue(*property, condition.value, "condition", what);
  }
}

std::optional<SurfaceSpecValidator::Property>
SurfaceSpecValidator::ValidateProperty(std::string_view name,
                                       const ViewScope& scope,
                                       std::string_view label, SpecPart what) {
  const std::optional<std::string_view> resolved = ResolvePropertyName(name);
  if (!resolved.has_value()) {
    return Property{.label = absl::StrCat("\"", name, "\"")};
  }
  std::string error;
  std::optional<Property> property = FindProperty(*resolved, scope, &error);
  if (!property.has_value()) {
    AddError(what, absl::StrCat(label, " \"", *resolved, "\" ", error));
    return std::nullopt;
  }
  property->label = absl::StrCat("\"", *resolved, "\"");
  return property;
}

void SurfaceSpecValidator::ValidateReadable(const Property& property,
                                            std::string_view label,
                                            SpecPart what) {
  if (property.type.has_value() &&
      !property.access.IsSet(PropertyAccess::kRead)) {
    AddError(what, absl::StrCat(label, " on ", property.label,
                                ", which can't be read"));
  }
}

void SurfaceSpecValidator::ValidateValue(const Property& property,
                                         const ViewProperty::Value& value,
                                         std::string_view label,
                                         SpecPart what) {
  if (!property.type.has_value()) {
    return;
  }
  if (!ViewProperty::CanEqual(*property.type, value)) {
    AddError(what, absl::StrCat(label, " on ", property.label, " (",
                                ViewProperty::GetTypeName(*property.type),
                                ") has a value it can never equal"));
  } else if (const auto* name = std::get_if<std::string>(&value);

             name != nullptr &&
             *property.type == ViewProperty::Type::kEnumerated &&
             (property.values == nullptr ||
              !absl::c_linear_search(property.values->values, *name))) {
    AddError(what, absl::StrCat(label, " \"", *name, "\" isn't one of ",
                                property.label, "'s values"));
  }
}

const WidgetShape* SurfaceSpecValidator::FindWidget(std::string_view path_text,
                                                    std::string* error) const {
  std::string parse_error;
  std::optional<WidgetPath> path = WidgetPath::Parse(path_text, &parse_error);
  if (!path.has_value()) {
    *error = absl::StrCat("isn't a path: ", parse_error);
    return nullptr;
  }
  absl::Span<const WidgetPath::Step> steps = path->GetSteps();
  const std::string* first =
      steps.empty() ? nullptr : std::get_if<std::string>(&steps[0]);
  if (first == nullptr) {
    *error = "names nothing";
    return nullptr;
  }

  // Elements hide the top-level widgets, and inner elements outer ones.
  const WidgetShape* start = nullptr;
  auto element = std::ranges::find(elements_.rbegin(), elements_.rend(), *first,
                                   &Element::name);
  if (element != elements_.rend()) {
    start = element->shape;
  } else if (widgets_.has_value()) {
    start = widgets_->GetField(*first);
    if (start == nullptr) {
      *error = "names nothing";
    }
  }
  if (start == nullptr) {
    return nullptr;
  }
  const WidgetShape* shape = start->Find(path->GetSubpath(1));
  if (shape == nullptr) {
    *error = "names nothing";
  }
  return shape;
}

std::optional<SurfaceSpecValidator::Property>
SurfaceSpecValidator::FindProperty(std::string_view name,
                                   const ViewScope& scope,
                                   std::string* error) const {
  // A subject's fields are only on a view with that kind of subject.
  for (SubjectKind kind : {SubjectKind::kTrack, SubjectKind::kRoute}) {
    const std::string_view name_space = GetSubjectNamespace(kind);
    if (!name.starts_with(name_space)) {
      continue;
    }
    if (!scope.subject.has_value()) {
      return Property{};
    }
    if (*scope.subject != kind) {
      *error =
          absl::StrCat("needs a view with a ",
                       name_space.substr(0, name_space.size() - 1), " subject");
      return std::nullopt;
    }
    return FindField(kind, name.substr(name_space.size()), error);
  }

  // A view: property is only on the views that meet its need.
  if (name.starts_with(kViewNamespace)) {
    const PropertyInfo* row = FindInfo(FindNamespace(name)->properties, name);
    if (row == nullptr) {
      *error = "isn't a property";
      return std::nullopt;
    }
    if (!absl::c_linear_search(scope.needs, *row->view_need)) {
      *error = "isn't on this view";
      return std::nullopt;
    }
    return Property{.type = row->type, .access = row->access};
  }

  // A name with a '.' is a field of a reference.
  const auto [owner, owner_field] =
      std::pair<std::string_view, std::string_view>(
          absl::StrSplit(name, absl::MaxSplits('.', 1)));
  if (!owner_field.empty()) {
    auto it = references_.find(owner);
    if (it == references_.end()) {
      *error = "isn't a property";
      return std::nullopt;
    }
    return FindField(it->second.kind, owner_field, error);
  }

  if (auto it = visible_names_.find(name);
      it != visible_names_.end() && it->second != nullptr) {
    const ComponentKindInfo& kind = GetComponentKind(*it->second);
    if (kind.access.IsEmpty()) {
      *error = "is a reference, whose fields are properties";
      return std::nullopt;
    }
    return Property{.type = kind.type,
                    .access = kind.access,
                    .values = std::get_if<EnumeratedValueSpec>(it->second)};
  }

  // Only REAPER knows which commands exist, and their types.
  if (name.starts_with(kCmdNamespace)) {
    if (!IsCommandId(name.substr(kCmdNamespace.size()))) {
      *error = "isn't a command id";
      return std::nullopt;
    }
    return Property{};
  }
  // Any other is a built in property of its namespace, such as state:.
  if (const PropertyNamespaceInfo* info = FindNamespace(name)) {
    if (const PropertyInfo* row = FindInfo(info->properties, name)) {
      return Property{.type = row->type, .access = row->access};
    }
  }
  *error = "isn't a property";
  return std::nullopt;
}

std::optional<SurfaceSpecValidator::Property> SurfaceSpecValidator::FindField(
    SubjectKind kind, std::string_view field, std::string* error) const {
  // A subject's fields are the properties of its namespace, and the fields of
  // its references.
  const PropertyNamespaceInfo* info =
      kind != SubjectKind::kNone ? FindNamespace(GetSubjectNamespace(kind))
                                 : nullptr;
  if (info != nullptr) {
    const auto [owner, owner_field] =
        std::pair<std::string_view, std::string_view>(
            absl::StrSplit(field, absl::MaxSplits('.', 1)));
    if (!owner_field.empty()) {
      if (const ReferenceInfo* reference =
              FindInfo(info->references, absl::StrCat(info->name, owner))) {
        return FindField(reference->kind, owner_field, error);
      }
    } else if (const PropertyInfo* row = FindInfo(
                   info->properties, absl::StrCat(info->name, field))) {
      return Property{.type = row->type, .access = row->access};
    }
  }
  *error = "isn't a property";
  return std::nullopt;
}

std::optional<std::string_view> SurfaceSpecValidator::ResolvePropertyName(
    std::string_view name) const {
  auto it = property_args_.find(name);
  return it != property_args_.end() ? it->second : name;
}

bool SurfaceSpecValidator::IsTrackReference(std::string_view name) const {
  auto it = references_.find(name);
  return it != references_.end() && it->second.kind == SubjectKind::kTrack;
}

bool SurfaceSpecValidator::IsDeclaredReference(std::string_view name) const {
  auto it = references_.find(name);
  return it != references_.end() && it->second.declared;
}

bool SurfaceSpecValidator::IsModifier(std::string_view name) const {
  return name.starts_with(kModNamespace) && visible_names_.contains(name);
}

void SurfaceSpecValidator::AddError(SpecPart what, std::string_view message) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(GetSpecError(what, message));
  }
}

}  // namespace

const ComponentKindInfo& GetComponentKind(const ComponentSpec& component) {
  return GetComponentKinds()[component.index()];
}

std::string_view GetComponentName(const ComponentSpec& component) {
  return std::visit(
      [](const auto& spec) -> std::string_view { return spec.name; },
      component);
}

SpecLine GetComponentLine(const ComponentSpec& component) {
  return std::visit([](const auto& spec) { return spec.line; }, component);
}

std::string GetViewPath(std::string_view parent_path, const ViewSpec& view) {
  const std::string_view separator = parent_path == "/" ? "" : "/";
  return view.repeat.has_value()
             ? absl::StrCat(parent_path, separator, view.repeat->element, "[]")
             : absl::StrCat(parent_path, separator, view.name);
}

std::vector<std::optional<PropertyArg>> GetPropertyArgs(
    const TemplateSpec& spec, const TemplateUse& use) {
  const std::vector<PropertyParam>& params = spec.property_params;
  std::vector<std::optional<PropertyArg>> args(params.size());
  for (int i = 0; i < std::ssize(params); ++i) {
    const PropertyParam& param = params[i];
    if (auto arg = use.properties.find(param.name);
        arg != use.properties.end()) {
      args[i] = PropertyArg{.name = arg->second};
    } else if (HasDefault(param)) {
      const int other = static_cast<int>(
          std::ranges::find(params, param.default_value, &PropertyParam::name) -
          params.begin());
      if (other == std::ssize(params)) {
        args[i] = PropertyArg{.name = param.default_value, .is_default = true};
      } else if (other < i) {
        args[i] = args[other];
      }
    }
  }
  return args;
}

bool ValidateSurfaceSpec(const SurfaceSpec& spec,
                         std::vector<std::string>* errors) {
  return SurfaceSpecValidator(errors).Validate(spec);
}

}  // namespace jpr
