// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/surface_spec.h"

#include <algorithm>
#include <utility>

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_join.h"
#include "absl/types/span.h"
#include "jpr/common/modifiers.h"
#include "jpr/device/widget_path.h"
#include "jpr/device/widget_shape.h"
#include "jpr/scene/scene.h"
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

// What checking knows of a view, which its contents and children see.
struct ViewScope {
  std::string path;  // From the root, which is "/".

  // The kind of its subject, or nullopt if it isn't known, as the reference
  // it is bound to isn't one.
  std::optional<SubjectKind> subject = SubjectKind::kNone;

  // The kind of its list's items, or nullopt if it has no list.
  std::optional<SubjectKind> list;
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

  void ValidateDevicesAndWidgets(const SurfaceSpec& spec);

  // Validates a component declared at the top level if `view` is null, and on
  // the view otherwise. Returns true if it declared its name, which is then
  // visible until it is removed from `visible_names_`.
  bool ValidateComponent(const ComponentSpec& component, const ViewScope* view);
  void ValidateParams(const ModifierSpec& modifier, std::string_view what,
                      const ViewScope* view);
  void ValidateParams(const EnumeratedValueSpec& value, std::string_view what,
                      const ViewScope* view);
  void ValidateParams(const TrackReferenceSpec& reference,
                      std::string_view what, const ViewScope* view);
  void ValidateParams(const TrackAnchorSpec& anchor, std::string_view what,
                      const ViewScope* view);
  void ValidateParams(const TrackPickSpec& pick, std::string_view what,
                      const ViewScope* view);

  // Adds an error unless the parameter, a track reference, is empty or a
  // track reference, built in or declared before it.
  void ValidateTrackReference(std::string_view param, std::string_view name,
                              std::string_view what);

  // Adds an error if the component, on a view, isn't on one with a track
  // subject.
  void ValidateTrackSubject(std::string_view what, const ViewScope* view);

  // Adds an error about `what` unless the name, the `label` of something new
  // (such as "value"), is a C identifier, not already in `names`, which it is
  // added to.
  void ValidateNewName(std::string_view what, std::string_view label,
                       std::string_view name,
                       absl::flat_hash_set<std::string_view>& names);

  void ValidateTemplates(absl::Span<const TemplateSpec> templates);

  // Validates a template's parameters of a kind ("widget" or "property").
  void ValidateTemplateParams(absl::Span<const std::string> params,
                              std::string_view kind, std::string_view what);

  // Returns true if the template uses the target, directly or through other
  // templates, adding the templates it uses it through to `through`. The
  // templates in `visited` aren't followed again.
  bool UsesTemplate(const TemplateSpec& from, std::string_view target,
                    absl::flat_hash_set<std::string_view>& visited,
                    std::vector<std::string_view>& through) const;

  // Validates a use, in the template or view `what` is about.
  void ValidateUse(const TemplateUse& use, std::string_view what);

  // Validates a use's arguments for the template's parameters of a kind
  // ("widget" or "property").
  void ValidateArgs(absl::Span<const std::string> params,
                    const absl::flat_hash_map<std::string, std::string>& args,
                    std::string_view kind, std::string_view template_name,
                    std::string_view what);

  void ValidateRoot(const ViewSpec& root);
  void ValidateView(const ViewSpec& view, const ViewScope& parent);

  // Validates the view's subject, and sets the scope's subject.
  void ValidateSubject(const ViewSpec& view, const ViewScope& parent,
                       ViewScope& scope);

  // Validates the view's list, and sets the scope's list.
  void ValidateList(const View::Config& config, ViewScope& scope);

  // Validates the repeated view's array and element, and returns its element.
  Element ValidateRepeat(const RepeatSpec& repeat, const ViewScope& scope);

  // Validates the view's components, uses, and children.
  void ValidateContents(const ViewSpec& view, const ViewScope& scope);
  void ValidateChildren(const ViewSpec& view, const ViewScope& scope);

  // Returns the shape of the widget at the path's text, looking up its first
  // name from the inside out (see SurfaceSpec). Returns null, setting `error`
  // to why, if it isn't a path, or names nothing, and null, leaving `error`
  // empty, if what it names isn't known.
  const WidgetShape* FindWidget(std::string_view path_text,
                                std::string* error) const;

  // Returns true if the name is a reference whose subject is a track, built in
  // or declared so far, or one declared so far.
  bool IsTrackReference(std::string_view name) const;
  bool IsDeclaredReference(std::string_view name) const;

  // Returns true if the name is a modifier, built in or declared. Modifiers
  // are declared only at the top level, so they are always visible.
  bool IsModifier(std::string_view name) const;

  // Adds an error about `what`, which starts the error if it isn't empty.
  void AddError(std::string_view what, std::string_view message);

  std::vector<std::string>* const errors_;
  bool valid_ = true;

  // The top-level widgets' shape, if they are valid.
  std::optional<WidgetShape> widgets_;

  // The elements of the repeated views being validated, outermost first.
  std::vector<Element> elements_;

  // The names declared, or built in, that a new declaration may not have: the
  // built in modifiers, the top-level declarations, and the declarations of
  // the view being validated and its ancestors. Every name is the spec's, or
  // the catalog's, which outlive the validator.
  absl::flat_hash_set<std::string_view> visible_names_;
  int declared_modifier_count_ = 0;

  // References, built in and declared so far.
  absl::flat_hash_map<std::string_view, Reference> references_;

  // The first template with each name.
  absl::flat_hash_map<std::string_view, const TemplateSpec*> templates_;
};

SurfaceSpecValidator::SurfaceSpecValidator(std::vector<std::string>* errors)
    : errors_(errors) {
  for (const PropertyInfo& modifier : Scene::GetBuiltInModifierInfo()) {
    visible_names_.insert(modifier.name);
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
  const std::string_view where = view != nullptr ? view->path : "";
  const std::string what = view != nullptr
                               ? absl::StrCat(view->path, ": ", name)
                               : std::string(name);
  if (view == nullptr ? !kind.top_level : !kind.on_view) {
    AddError(
        what,
        absl::StrCat("is of kind \"", kind.name, "\", which can't be declared ",
                     view == nullptr ? "at the top level" : "on a view"));
  }

  bool declared = false;
  if (!IsNameIn(name, kind.name_space)) {
    AddError(where,
             absl::StrCat(kind.name, " name \"", name, "\" isn't \"",
                          kind.name_space, "\" followed by a C identifier"));
  } else if (!visible_names_.insert(name).second) {
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
  std::visit([this, &what,
              view](const auto& spec) { ValidateParams(spec, what, view); },
             component);
  return declared;
}

void SurfaceSpecValidator::ValidateParams(const ModifierSpec& modifier,
                                          std::string_view what,
                                          const ViewScope* view) {
  // A modifier has no parameters (see ValidateComponent()).
}

void SurfaceSpecValidator::ValidateParams(const EnumeratedValueSpec& value,
                                          std::string_view what,
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
                                          std::string_view what,
                                          const ViewScope* view) {
  ValidateTrackReference("fallback", reference.fallback, what);
  ValidateTrackReference("follow", reference.follow, what);
  references_.emplace(reference.name,
                      Reference{.kind = SubjectKind::kTrack, .declared = true});
}

void SurfaceSpecValidator::ValidateParams(const TrackAnchorSpec& anchor,
                                          std::string_view what,
                                          const ViewScope* view) {
  if (!anchor.modifier.empty() && !IsModifier(anchor.modifier)) {
    AddError(what, absl::StrCat("modifier \"", anchor.modifier,
                                "\" isn't a modifier"));
  }
  ValidateTrackSubject(what, view);
}

void SurfaceSpecValidator::ValidateParams(const TrackPickSpec& pick,
                                          std::string_view what,
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
                                                  std::string_view what) {
  if (!name.empty() && !IsTrackReference(name)) {
    AddError(what, absl::StrCat(param, " \"", name,
                                "\" isn't a track reference, built in or "
                                "declared before it"));
  }
}

void SurfaceSpecValidator::ValidateTrackSubject(std::string_view what,
                                                const ViewScope* view) {
  if (view != nullptr && view->subject.has_value() &&
      *view->subject != SubjectKind::kTrack) {
    AddError(what, "is on a view without a track subject");
  }
}

void SurfaceSpecValidator::ValidateNewName(
    std::string_view what, std::string_view label, std::string_view name,
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
    const std::string what = absl::StrCat("template ", spec.name);
    if (!IsName(spec.name)) {
      AddError("", absl::StrCat("template name \"", spec.name,
                                "\" isn't a C identifier"));
    }
    if (!templates_.emplace(spec.name, &spec).second) {
      AddError(what, "has the name of another template");
    }
    ValidateTemplateParams(spec.widget_params, "widget", what);
    ValidateTemplateParams(spec.property_params, "property", what);
  }

  // Uses can name any template, so they are validated once every template is
  // known.
  for (const TemplateSpec& spec : templates) {
    const std::string what = absl::StrCat("template ", spec.name);
    for (const TemplateUse& use : spec.uses) {
      ValidateUse(use, what);
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

void SurfaceSpecValidator::ValidateTemplateParams(
    absl::Span<const std::string> params, std::string_view kind,
    std::string_view what) {
  const std::string label = absl::StrCat(kind, " parameter");
  absl::flat_hash_set<std::string_view> names;
  for (const std::string& param : params) {
    ValidateNewName(what, label, param, names);
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
                                       std::string_view what) {
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

void SurfaceSpecValidator::ValidateArgs(
    absl::Span<const std::string> params,
    const absl::flat_hash_map<std::string, std::string>& args,
    std::string_view kind, std::string_view template_name,
    std::string_view what) {
  for (const std::string& param : params) {
    if (!args.contains(param)) {
      AddError(what, absl::StrCat("uses ", template_name, " without its ", kind,
                                  " parameter \"", param, "\""));
    }
  }
  // The arguments are in no order, so they are sorted for errors to be.
  std::vector<std::string_view> others;
  for (const auto& [arg, value] : args) {
    if (std::ranges::find(params, arg) == params.end()) {
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
  const ViewScope scope = {.path = "/"};
  if (!root.name.empty()) {
    AddError(scope.path, "the root view has no name");
  }
  if (root.config.condition.has_value()) {
    AddError(scope.path, "the root view has no condition");
  }
  if (!std::holds_alternative<View::ParentSubject>(root.config.subject)) {
    AddError(scope.path, "the root view has no subject");
  }
  if (root.config.list.has_value()) {
    AddError(scope.path, "the root view has no list");
  }
  if (root.repeat.has_value()) {
    AddError(scope.path, "the root view isn't repeated");
  }
  ValidateContents(root, scope);
}

void SurfaceSpecValidator::ValidateView(const ViewSpec& view,
                                        const ViewScope& parent) {
  ViewScope scope;
  scope.path = absl::StrCat(parent.path, parent.path == "/" ? "" : "/",
                            view.repeat.has_value()
                                ? absl::StrCat(view.repeat->element, "[]")
                                : view.name);
  if (view.repeat.has_value()) {
    if (!view.name.empty()) {
      AddError(scope.path, absl::StrCat("is repeated, so it has no name, but "
                                        "is named \"",
                                        view.name, "\""));
    }
  } else if (!IsName(view.name)) {
    AddError(parent.path, absl::StrCat("view name \"", view.name,
                                       "\" isn't a C identifier"));
  }

  ValidateSubject(view, parent, scope);
  ValidateList(view.config, scope);
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
    AddError(scope.path, view.repeat.has_value()
                             ? "is repeated, but its subject isn't an item "
                               "of its parent's list"
                             : "shows an item of its parent's list, but isn't "
                               "repeated");
  }
  if ((view.repeat.has_value() || is_item) && !parent.list.has_value()) {
    AddError(scope.path,
             "shows an item of its parent's list, but its parent has no list");
  }

  if (is_item) {
    scope.subject = parent.list;
  } else if (const auto* subject =
                 std::get_if<View::ReferenceSubject>(&view.config.subject)) {
    auto it = references_.find(subject->name);
    if (it == references_.end()) {
      AddError(scope.path, absl::StrCat("is bound to \"", subject->name,
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
    AddError(scope.path, "has a list, but no track subject");
  }
  if (list.bank_size.has_value() && *list.bank_size <= 0) {
    AddError(scope.path, absl::StrCat("has a bank size of ", *list.bank_size,
                                      ", which isn't positive"));
  }
  if (const auto* child_tracks = std::get_if<View::ChildTracks>(&list.items)) {
    const std::string& reveal = child_tracks->reveal;
    if (!reveal.empty()) {
      // Revealing moves the reference the view is bound to.
      const auto* bound = std::get_if<View::ReferenceSubject>(&config.subject);
      if (!IsTrackReference(reveal)) {
        AddError(scope.path, absl::StrCat("reveals \"", reveal,
                                          "\", which isn't a track reference"));
      } else if (bound == nullptr || !IsDeclaredReference(bound->name) ||
                 bound->name == reveal) {
        AddError(scope.path, absl::StrCat("reveals \"", reveal,
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
    AddError(scope.path, absl::StrCat("element \"", repeat.element,
                                      "\" isn't a C identifier"));
  }
  std::string error;
  const WidgetShape* array = FindWidget(repeat.array, &error);
  if (!error.empty()) {
    AddError(scope.path, absl::StrCat("array \"", repeat.array, "\" ", error));
  } else if (array != nullptr && array->GetKind() != WidgetKind::kArray) {
    AddError(scope.path,
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
      AddError(scope.path, absl::StrCat("has more than one child view named \"",
                                        child.name, "\""));
    }
    ValidateView(child, scope);
  }
  if (scope.list.has_value() && repeated_count != 1) {
    AddError(scope.path,
             absl::StrCat("has a list, so it has one repeated child, but has ",
                          repeated_count));
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

void SurfaceSpecValidator::AddError(std::string_view what,
                                    std::string_view message) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(what.empty() ? std::string(message)
                                    : absl::StrCat(what, ": ", message));
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

bool ValidateSurfaceSpec(const SurfaceSpec& spec,
                         std::vector<std::string>* errors) {
  return SurfaceSpecValidator(errors).Validate(spec);
}

}  // namespace jpr
