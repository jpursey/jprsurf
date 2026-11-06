// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "jpr/common/track.h"
#include "jpr/device/device_spec.h"
#include "jpr/device/spec_line.h"
#include "jpr/device/widget_definition.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/view.h"
#include "jpr/scene/view_mapping.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// Components
//
// A config declares components by kind (see GetComponentKinds()), each with a
// name and its kind's parameters, at the top level or on a view. Each spec's
// fields after its name, up to its line, are its kind's parameters, in order.
//==============================================================================

// A modifier, at the top level: a "mod:" name, such as "mod:send_hold".
struct ModifierSpec {
  friend bool operator==(const ModifierSpec&, const ModifierSpec&) = default;

  std::string name;
  SpecLine line;  // Where it was written, for errors.
};

// An enumerated value, at the top level, such as "user:surface_mode".
struct EnumeratedValueSpec {
  friend bool operator==(const EnumeratedValueSpec&,
                         const EnumeratedValueSpec&) = default;

  std::string name;
  std::vector<std::string> values;  // Such as "track" and "send_receive".
  std::string initial;  // One of the values, or empty for the first.
  SpecLine line;        // Where it was written, for errors.
};

// A track reference, at the top level, such as "user:folder".
struct TrackReferenceSpec {
  friend bool operator==(const TrackReferenceSpec&,
                         const TrackReferenceSpec&) = default;

  std::string name;
  std::string fallback;  // A track reference, such as "state:master_track".
  std::string follow;    // A track reference.
  SpecLine line;         // Where it was written, for errors.
};

// A track anchor, on a view, such as "user:anchor_select".
struct TrackAnchorSpec {
  friend bool operator==(const TrackAnchorSpec&,
                         const TrackAnchorSpec&) = default;

  std::string name;
  TrackBoolProperty action = TrackBoolProperty::kSelected;
  std::string modifier;  // A modifier, such as "mod:select_anchor".
  SpecLine line;         // Where it was written, for errors.
};

// A track pick, at the top level or on a view, such as "user:pick_track".
struct TrackPickSpec {
  friend bool operator==(const TrackPickSpec&, const TrackPickSpec&) = default;

  std::string name;
  std::string reference;  // The track reference it sets.
  std::string source;     // A track reference, at the top level only.
  SpecLine line;          // Where it was written, for errors.
};

// A component, of the kind each alternative is: the kinds of
// GetComponentKinds(), in order.
using ComponentSpec =
    std::variant<ModifierSpec, EnumeratedValueSpec, TrackReferenceSpec,
                 TrackAnchorSpec, TrackPickSpec>;

// Returns the component's kind, from GetComponentKinds().
const ComponentKindInfo& GetComponentKind(const ComponentSpec& component);

// Returns the name the component declares, and where it was written.
std::string_view GetComponentName(const ComponentSpec& component);
SpecLine GetComponentLine(const ComponentSpec& component);

//==============================================================================
// Mappings and templates
//==============================================================================

// A value a write mapping always writes, for which creating the scene adds a
// const: property (see Scene::AddConstProperty()).
struct FixedValue {
  friend bool operator==(const FixedValue&, const FixedValue&) = default;

  ViewProperty::Type type = ViewProperty::Type::kToggle;
  ViewProperty::Value value;
};

// A mapping of a property, or a fixed value, to a control (see
// View::AddMapping()).
struct MappingSpec {
  friend bool operator==(const MappingSpec&, const MappingSpec&) = default;

  std::variant<std::string, FixedValue> property;
  std::string control;  // A widget path (see Names in paths, below).
  ViewMapping::TypeFlags type;
  ViewMapping::Config config;
  SpecLine line;  // Where it was written, for errors.
};

// A use of a template, with an argument for each of its parameters without a
// default, and for any with one that it doesn't use the default of.
struct TemplateUse {
  friend bool operator==(const TemplateUse&, const TemplateUse&) = default;

  std::string name;                                          // The template's.
  absl::flat_hash_map<std::string, std::string> widgets;     // To paths.
  absl::flat_hash_map<std::string, std::string> properties;  // To names.
  SpecLine line;  // Where it was written, for errors.
};

// A property parameter of a template: a property name without a namespace,
// which a use replaces with its argument, or with the default where it gives
// none. A default is a property name, found where the template is used, or an
// earlier property parameter, which stands for what that one does.
struct PropertyParam {
  friend bool operator==(const PropertyParam&, const PropertyParam&) = default;

  std::string name;
  std::string default_value;  // Empty for none, so a use must give it.
};

// A named set of mappings, with parameters, which any number of views can use,
// as if its mappings were written there. A widget parameter is the first name
// of the paths that use it (see PropertyParam for a property parameter).
struct TemplateSpec {
  friend bool operator==(const TemplateSpec&, const TemplateSpec&) = default;

  std::string name;
  std::vector<std::string> widget_params;
  std::vector<PropertyParam> property_params;
  std::vector<MappingSpec> mappings;
  std::vector<TemplateUse> uses;
  SpecLine line;  // Where it was written, for errors.
};

// What a use gives a template's property parameter: its argument, which is
// resolved where the use is, as one in a template may be that template's
// parameter, or a default, which is a name as the template wrote it.
struct PropertyArg {
  std::string_view name;
  bool is_default = false;
};

// Returns what the use gives each of the template's property parameters, in
// their order, or nullopt for one it gives nothing: one with neither an
// argument nor a default, or whose default names it or a later parameter,
// which isn't valid (see ValidateSurfaceSpec()). A default that names an
// earlier parameter gives what that one is given.
std::vector<std::optional<PropertyArg>> GetPropertyArgs(
    const TemplateSpec& spec, const TemplateUse& use);

//==============================================================================
// Views
//==============================================================================

// A view repeated over an array widget: an instance for each element, each
// showing an item of its parent's list. `element` is the first name of the
// paths that use the instance's element, here and in its descendants.
struct RepeatSpec {
  friend bool operator==(const RepeatSpec&, const RepeatSpec&) = default;

  std::string array;    // A widget path, such as "strip".
  std::string element;  // Such as "strip".
};

// A view, and its descendants. Its components are declared, then its uses
// expanded, then its mappings added, then its children built.
struct ViewSpec {
  friend bool operator==(const ViewSpec&, const ViewSpec&) = default;

  std::string name;  // Unique among siblings, and empty for a repeated view.
  View::Config config;
  std::optional<RepeatSpec> repeat;  // Exactly when the subject is a list item.
  std::vector<ComponentSpec> components;
  std::vector<TemplateUse> uses;
  std::vector<MappingSpec> mappings;
  std::vector<ViewSpec> children;
  SpecLine line;  // Where it was written, for errors.
};

//==============================================================================
// SurfaceSpec
//==============================================================================

// A whole surface: its devices, its widgets, and the scene on them (see
// docs/config_model.md). The root view is the scene's own, so it has no name
// or config, and isn't repeated.
//
// Names in paths: a path's first name is looked up from the inside out, and
// the innermost wins: a widget parameter of the template the mapping is in,
// then the element of each repeated view it is in, nearest first, then the
// top-level widgets. A template sees only its own parameters and the top-level
// widgets.
struct SurfaceSpec {
  friend bool operator==(const SurfaceSpec&, const SurfaceSpec&) = default;

  std::vector<DeviceSpec> devices;
  WidgetStructDefinition widgets;
  std::vector<ComponentSpec> components;  // Declared at the top level.
  std::vector<TemplateSpec> templates;
  ViewSpec root;
  TrackFilter track_filter = TrackFilter::kMcp;
};

// Returns true if the spec is valid, adding an error for each problem found
// to `errors` (if provided). Checking doesn't stop at the first problem, and
// each error starts with the line of the part it is about, if it has one (see
// GetSpecError()), then what it is about: a device or a widget (see
// CreateWidgetDevices() and ValidateWidgets()), a top-level component by its
// name, a template (such as "template track_strip"), or a view by its path
// from the root, which is "/" (such as "/track_mode/track_list/strip[]", where
// a repeated view is its element, and "[]"), and a component on a view by its
// name after that. A problem that makes others impossible to check, such as a
// device that isn't valid, skips them rather than adding errors that follow
// from it. An error about a use's arguments has the use's line, and one about
// a mapping in a template the mapping's, with the use's view in its path.
//
// It checks:
// - Devices and widgets: the device specs (see CreateWidgetDevices()), then
//   the widgets on their catalogs (see ValidateWidgets()).
// - Components: each declared where its kind may be (see ComponentKindInfo),
//   with a name in its kind's namespace (see ComponentKindInfo::name_space)
//   followed by a C identifier, that no other declaration visible where it is
//   made has, nor any built in modifier. A track reference's fallback and
//   follow are track references, built in or declared before it, as is a
//   pick's source, which only a pick at the top level has. A pick's reference
//   is a track reference declared before it. An anchor's modifier is a
//   modifier.
//   Enumerated values are C identifiers, with no value repeated, and the
//   initial value one of them. An anchor or a pick on a view needs a track
//   subject. No more than kModUserCount modifiers are declared.
// - Views: each but a repeated view has a name, a C identifier, unique among
//   its siblings. A view is repeated exactly when its subject is an item of its
//   parent's list, and its parent has a list. A view bound to a reference names
//   a track reference, built in or declared. A list is on a view with a track
//   subject, its bank size is positive, and a list that reveals a reference
//   reveals a track reference, and is on a view bound to a declared reference
//   other than that one. A view with a list has exactly one repeated child,
//   whose array is an array widget, and element a C identifier.
// - Templates and uses: each template has a name, a C identifier, that no
//   other template has, and parameters that are C identifiers, none repeated.
//   A property parameter's default doesn't name the parameter or a later one.
//   Each use names a template, and gives an argument for each of its
//   parameters without a default, and none for anything else, and no template
//   uses itself, however indirectly.
// - Mappings, with each use's expanded into the view that uses it, where its
//   widget arguments name widgets, and its property arguments, and the
//   defaults of the parameters it gives none for, are names: the control is a
//   control. The property is one where the mapping is (see below), or a fixed
//   value that is a constant (see IsConstValue()). Each
//   side the mapping asks for is one the scene keeps (see ViewMapping), with
//   the access it needs, and an input or output that suits the property's
//   type on the control (taking a press to have a release, as every catalog's
//   does). Its mode and each override's modes are the control's (see
//   ViewMapping::FindMode()). Its range ends are values of the property's
//   type, which an action, a toggle, or a timeline position has none of, and
//   its required modifiers are modifiers. A mapping in a template is about
//   the view's path, the templates it is expanded through, and its control,
//   such as "/track_mode/track_list/strip[]: track_strip: strip.mute".
// - Conditions, of views and mappings, and mode overrides: the property is
//   one where it is, and can be read, and each value can be read as its type,
//   naming one of its values if it is a declared enumerated value. A view's
//   condition is made before its components are declared, so it doesn't see
//   them.
//
// A property is found as View and Scene find it: a subject's field only on a
// view with that kind of subject, a view: property only on a view that meets
// its need (see ViewNeed), a reference's field, a declaration visible where it
// is used, a built in property, or a command, whose id is checked but whose
// type and access only REAPER knows, so they aren't checked.
bool ValidateSurfaceSpec(const SurfaceSpec& spec,
                         std::vector<std::string>* errors);

}  // namespace jpr
