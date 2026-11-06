// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/create_scene.h"

#include <algorithm>
#include <optional>
#include <string_view>
#include <utility>
#include <variant>

#include "absl/container/flat_hash_map.h"
#include "absl/strings/str_cat.h"
#include "jpr/common/modifiers.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/spec_line.h"
#include "jpr/device/widget.h"
#include "jpr/device/widget_definition.h"
#include "jpr/device/widget_path.h"
#include "jpr/scene/track_anchor_property.h"
#include "jpr/scene/track_pick_property.h"
#include "jpr/scene/track_reference.h"
#include "jpr/scene/value_property.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {

namespace {

// The creation of one scene from a spec, which checking has found valid.
class SceneBuilder {
 public:
  explicit SceneBuilder(std::vector<std::string>* errors) : errors_(errors) {}

  std::unique_ptr<Scene> Build(const SurfaceSpec& spec, MidiPorts& ports,
                               RunRegistry& device_registry);

 private:
  // A name that paths can start with, and the path of what it names: the
  // element of a repeated view's instance, or while a template's mappings are
  // added, a widget parameter.
  struct PathName {
    std::string_view name;
    std::string path;
  };

  // Declares a component at the top level if `view` is null, and on the view
  // otherwise. `path` is the view's, or empty at the top level.
  void AddComponent(const ComponentSpec& component, View* view,
                    std::string_view path);
  bool AddComponentProperty(const ModifierSpec& modifier, View* view);
  bool AddComponentProperty(const EnumeratedValueSpec& value, View* view);
  bool AddComponentProperty(const TrackReferenceSpec& reference, View* view);
  bool AddComponentProperty(const TrackAnchorSpec& anchor, View* view);
  bool AddComponentProperty(const TrackPickSpec& pick, View* view);

  // Adds a user: property to the view, or to the scene if `view` is null.
  bool AddUserProperty(std::unique_ptr<ViewProperty> property, View* view);

  // Adds the view's components, uses, mappings, and children, then enables it.
  // `path` is the view's, from the root, as errors name it.
  void BuildView(const ViewSpec& spec, View* view, std::string_view path);

  // Adds the child to the view, with an instance for each element of its array
  // that is present if it is repeated, and builds it.
  void BuildChild(const ViewSpec& child, View* view, std::string_view path);

  // Adds the view as a child of `parent`, with the name, and builds it, unless
  // the scene refuses it.
  void AddView(const ViewSpec& spec, std::string_view name, View* parent,
               std::string_view parent_path);

  // Adds the mappings of a use in the view, with the template's widget
  // parameters resolved where the use is, as are the property arguments, and
  // the mappings of the uses in the template in turn. `where` is the view's
  // path, followed by the templates the use is in.
  void AddUseMappings(const TemplateUse& use, View* view,
                      std::string_view where);

  // Adds a mapping to the view, whose errors have its line, and start with
  // `text` after it.
  void AddMapping(const MappingSpec& mapping, View* view,
                  std::string_view text);

  // Returns the name of the const: property for the fixed value, adding it the
  // first time, or nullopt if the scene refuses it.
  std::optional<std::string> GetFixedValueName(const FixedValue& value);

  // Returns the path's text with its first name looked up from the inside
  // out (see SurfaceSpec): replaced by the path of what it names, if it is a
  // name in `path_names_`, or as it is, if it is a top-level widget.
  std::string ResolvePath(std::string_view text) const;

  // Returns the name a property name in a template stands for: its argument,
  // if it is a property parameter, or itself.
  std::string_view ResolvePropertyName(std::string_view name) const;

  // Returns true if the name isn't a command, or is one REAPER has, adding an
  // error about `what` otherwise.
  bool ValidateCommand(std::string_view name, SpecPart what);

  // Adds an error about `what` (see GetSpecError()).
  void AddError(SpecPart what, std::string_view message);

  std::vector<std::string>* const errors_;
  bool valid_ = true;
  Scene* scene_ = nullptr;

  // The templates by name, and the track references declared, by name, which
  // picks set.
  absl::flat_hash_map<std::string_view, const TemplateSpec*> templates_;
  absl::flat_hash_map<std::string_view, TrackReference*> references_;

  // The names paths can start with, outermost first, and while a template's
  // mappings are added, the arguments of its property parameters.
  std::vector<PathName> path_names_;
  absl::flat_hash_map<std::string_view, std::string_view> property_args_;

  // The fixed values added, and their const: properties' names.
  std::vector<std::pair<FixedValue, std::string>> fixed_values_;
};

std::unique_ptr<Scene> SceneBuilder::Build(const SurfaceSpec& spec,
                                           MidiPorts& ports,
                                           RunRegistry& device_registry) {
  if (!ValidateSurfaceSpec(spec, errors_)) {
    return nullptr;
  }
  std::optional<DeviceList> devices =
      CreateDevices(spec.devices, ports, device_registry, errors_);
  if (!devices.has_value()) {
    return nullptr;
  }
  std::optional<WidgetRoot> widgets =
      ResolveWidgets(spec.widgets, devices->widget_devices, errors_);
  if (!widgets.has_value()) {
    return nullptr;
  }
  auto scene = std::make_unique<Scene>("Scene", std::move(devices->devices),
                                       std::move(*widgets), spec.track_filter);
  scene_ = scene.get();

  for (const TemplateSpec& spec_template : spec.templates) {
    templates_.emplace(spec_template.name, &spec_template);
  }
  for (const ComponentSpec& component : spec.components) {
    AddComponent(component, nullptr, "");
  }
  BuildView(spec.root, scene->GetRootView(), "/");
  if (!valid_) {
    return nullptr;
  }
  return scene;
}

void SceneBuilder::AddComponent(const ComponentSpec& component, View* view,
                                std::string_view path) {
  if (!std::visit(
          [this, view](const auto& spec) {
            return AddComponentProperty(spec, view);
          },
          component)) {
    const std::string_view name = GetComponentName(component);
    const std::string text =
        path.empty() ? std::string(name) : absl::StrCat(path, ": ", name);
    AddError({.line = GetComponentLine(component), .text = text},
             "the scene refused it");
  }
}

bool SceneBuilder::AddComponentProperty(const ModifierSpec& modifier,
                                        View* view) {
  return scene_->AddModifierProperty(modifier.name) != 0;
}

bool SceneBuilder::AddComponentProperty(const EnumeratedValueSpec& value,
                                        View* view) {
  // An empty initial value is the first.
  auto initial = std::ranges::find(value.values, value.initial);
  const int initial_value =
      initial != value.values.end()
          ? static_cast<int>(initial - value.values.begin())
          : 0;
  return AddUserProperty(std::make_unique<EnumeratedValueProperty>(
                             value.name, value.values, initial_value),
                         view);
}

bool SceneBuilder::AddComponentProperty(const TrackReferenceSpec& reference,
                                        View* view) {
  TrackReference* added = scene_->AddTrackReference(
      reference.name,
      {.fallback = reference.fallback, .follow = reference.follow});
  if (added == nullptr) {
    return false;
  }
  references_.emplace(reference.name, added);
  return true;
}

bool SceneBuilder::AddComponentProperty(const TrackAnchorSpec& anchor,
                                        View* view) {
  const Modifiers modifier =
      anchor.modifier.empty() ? 0 : scene_->GetModifier(anchor.modifier);
  return AddUserProperty(
      std::make_unique<TrackAnchorProperty>(
          anchor.name, TrackAnchorProperty::Config{.action = anchor.action,
                                                   .modifier = modifier}),
      view);
}

bool SceneBuilder::AddComponentProperty(const TrackPickSpec& pick, View* view) {
  auto reference = references_.find(pick.reference);
  if (reference == references_.end()) {
    return false;
  }
  const TrackReference* source =
      pick.source.empty() ? nullptr : scene_->GetTrackReference(pick.source);
  return AddUserProperty(std::make_unique<TrackPickProperty>(
                             pick.name, *reference->second, source),
                         view);
}

bool SceneBuilder::AddUserProperty(std::unique_ptr<ViewProperty> property,
                                   View* view) {
  return view != nullptr
             ? view->AddUserProperty(std::move(property)) != nullptr
             : scene_->AddUserProperty(std::move(property)) != nullptr;
}

void SceneBuilder::BuildView(const ViewSpec& spec, View* view,
                             std::string_view path) {
  for (const ComponentSpec& component : spec.components) {
    AddComponent(component, view, path);
  }
  for (const TemplateUse& use : spec.uses) {
    AddUseMappings(use, view, path);
  }
  for (const MappingSpec& mapping : spec.mappings) {
    AddMapping(mapping, view, absl::StrCat(path, ": ", mapping.control));
  }
  for (const ViewSpec& child : spec.children) {
    BuildChild(child, view, path);
  }
  view->Enable();
}

void SceneBuilder::BuildChild(const ViewSpec& child, View* view,
                              std::string_view path) {
  if (!child.repeat.has_value()) {
    AddView(child, child.name, view, path);
    return;
  }

  // The array is looked up where the repeated view is, and each instance's
  // element hides any outer name.
  const std::string array_path = ResolvePath(child.repeat->array);
  std::optional<WidgetPath> array_widget_path = WidgetPath::Parse(array_path);
  const Widget* array =
      array_widget_path.has_value()
          ? scene_->GetWidgets().widget->Find(*array_widget_path)
          : nullptr;
  const int count =
      array != nullptr ? static_cast<int>(array->GetElements().size()) : 0;
  for (int i = 0; i < count; ++i) {
    std::string element_path = GetElementPathText(array_path, i);
    path_names_.push_back(
        {.name = child.repeat->element, .path = element_path});
    AddView(child, element_path, view, path);
    path_names_.pop_back();
  }
}

void SceneBuilder::AddView(const ViewSpec& spec, std::string_view name,
                           View* parent, std::string_view parent_path) {
  const std::string path =
      absl::StrCat(parent_path, parent_path == "/" ? "" : "/", name);

  // A view's condition is made with it, so a command it names is checked
  // first.
  if (spec.config.condition.has_value() &&
      !ValidateCommand(spec.config.condition->property,
                       {.line = spec.line, .text = path})) {
    return;
  }
  View* view = parent->AddChildView(name, spec.config);
  if (view == nullptr) {
    AddError({.line = spec.line, .text = path}, "the scene refused it");
    return;
  }
  BuildView(spec, view, path);
}

void SceneBuilder::AddUseMappings(const TemplateUse& use, View* view,
                                  std::string_view where) {
  const TemplateSpec& spec = *templates_.at(use.name);

  // The arguments are resolved where the use is.
  std::vector<PathName> widget_params;
  for (const std::string& param : spec.widget_params) {
    widget_params.push_back(
        {.name = param, .path = ResolvePath(use.widgets.at(param))});
  }
  absl::flat_hash_map<std::string_view, std::string_view> property_args;
  const std::vector<std::optional<PropertyArg>> args =
      GetPropertyArgs(spec, use);
  for (int i = 0; i < std::ssize(args); ++i) {
    const PropertyArg& arg = *args[i];
    property_args.emplace(
        spec.property_params[i].name,
        arg.is_default ? arg.name : ResolvePropertyName(arg.name));
  }

  // A template sees only its own parameters and the top-level widgets.
  std::swap(path_names_, widget_params);
  std::swap(property_args_, property_args);
  const std::string what = absl::StrCat(where, ": ", use.name);
  for (const MappingSpec& mapping : spec.mappings) {
    AddMapping(mapping, view, absl::StrCat(what, ": ", mapping.control));
  }
  for (const TemplateUse& inner : spec.uses) {
    AddUseMappings(inner, view, what);
  }
  std::swap(property_args_, property_args);
  std::swap(path_names_, widget_params);
}

void SceneBuilder::AddMapping(const MappingSpec& mapping, View* view,
                              std::string_view text) {
  const SpecPart what = {.line = mapping.line, .text = text};
  std::string property;
  if (const auto* fixed = std::get_if<FixedValue>(&mapping.property)) {
    std::optional<std::string> name = GetFixedValueName(*fixed);
    if (!name.has_value()) {
      AddError(what, "the scene refused its fixed value");
      return;
    }
    property = std::move(*name);
  } else {
    property = ResolvePropertyName(std::get<std::string>(mapping.property));
  }

  // Property names in a template stand for its arguments.
  ViewMapping::Config config = mapping.config;
  bool valid = ValidateCommand(property, what);
  if (config.condition.has_value()) {
    config.condition->property =
        std::string(ResolvePropertyName(config.condition->property));
    valid = ValidateCommand(config.condition->property, what) && valid;
  }
  for (ViewMapping::ModeOverride& mode_override : config.write.mode_overrides) {
    mode_override.property =
        std::string(ResolvePropertyName(mode_override.property));
    valid = ValidateCommand(mode_override.property, what) && valid;
  }
  if (valid &&
      !view->AddMapping(mapping.type, property, ResolvePath(mapping.control),
                        std::move(config))) {
    AddError(what, "the scene refused it");
  }
}

std::optional<std::string> SceneBuilder::GetFixedValueName(
    const FixedValue& value) {
  auto it = std::ranges::find(fixed_values_, value,
                              &std::pair<FixedValue, std::string>::first);
  if (it != fixed_values_.end()) {
    return it->second;
  }
  ViewProperty* property = scene_->AddConstProperty(value.type, value.value);
  if (property == nullptr) {
    return std::nullopt;
  }
  return fixed_values_.emplace_back(value, property->GetName()).second;
}

std::string SceneBuilder::ResolvePath(std::string_view text) const {
  // Checking found the path valid, so one that isn't is left for the scene to
  // refuse.
  std::optional<WidgetPath> path = WidgetPath::Parse(text);
  if (!path.has_value() || path->GetSteps().empty()) {
    return std::string(text);
  }
  const std::string* first = std::get_if<std::string>(&path->GetSteps()[0]);
  if (first == nullptr) {
    return std::string(text);
  }
  auto name = std::ranges::find(path_names_.rbegin(), path_names_.rend(),
                                *first, &PathName::name);
  if (name == path_names_.rend()) {
    return std::string(text);
  }
  std::string resolved = name->path;
  for (const WidgetPath::Step& step : path->GetSteps().subspan(1)) {
    if (const std::string* field = std::get_if<std::string>(&step)) {
      resolved = GetFieldPathText(resolved, *field);
    } else {
      resolved = GetElementPathText(resolved, std::get<int>(step));
    }
  }
  return resolved;
}

std::string_view SceneBuilder::ResolvePropertyName(
    std::string_view name) const {
  auto it = property_args_.find(name);
  return it != property_args_.end() ? it->second : name;
}

bool SceneBuilder::ValidateCommand(std::string_view name, SpecPart what) {
  if (!name.starts_with(kCmdNamespace) ||
      scene_->GetProperty(name) != nullptr) {
    return true;
  }
  AddError(what, absl::StrCat("command \"", name, "\" isn't one REAPER has"));
  return false;
}

void SceneBuilder::AddError(SpecPart what, std::string_view message) {
  valid_ = false;
  if (errors_ != nullptr) {
    errors_->push_back(GetSpecError(what, message));
  }
}

}  // namespace

std::unique_ptr<Scene> CreateScene(const SurfaceSpec& spec, MidiPorts& ports,
                                   RunRegistry& device_registry,
                                   std::vector<std::string>* errors) {
  return SceneBuilder(errors).Build(spec, ports, device_registry);
}

}  // namespace jpr
