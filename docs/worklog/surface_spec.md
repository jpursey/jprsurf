# Build the scene from a SurfaceSpec

The plugin builds its scene from a C++ data structure, the `SurfaceSpec`,
instead of imperative calls: its devices, widgets, declared components,
templates, views, mappings, and settings, as the [config
model](../config_model.md) describes them. JPRSurf's own surface becomes a spec
defined in code, and later reading a config file is just filling in the same
struct (*Config file language and loader*).

Checking a spec and building a scene from it are separate. Checking needs no
REAPER or hardware, only the spec and two catalogs: the device types' (done in
*Device types and catalogs*) and a new **catalog of what the scene provides**:
its built in properties and references, and the kinds of component a config
can declare. Building needs REAPER, so it is unit tested on the fake REAPER and
the fake X-Touch.

There is no change in behavior. The surface tests verify it, unchanged, and the
call count bounds don't move.

## Design

Everything is in `scene`, as widgets are in `device`: the spec, the catalog,
checking, and creating a scene from a spec. Checking calls nothing in REAPER,
as `ValidateWidgets()` doesn't, and its tests show it. A spec names scene's own
types, rules, and built in names throughout, so a library of its own below
`scene` would have to take most of `scene`'s vocabulary with it. The config
loader, which parses text into a `SurfaceSpec`, is where a library of its own
pays off later, above `scene`.

The plugin then has JPRSurf's spec, and creates its scene from it. Everything
`InitViews()` does today is in the spec, or in `CreateScene()`.

Everything here runs once, when the surface is created, and a scene built from
a spec is the same objects as one built by hand, so nothing changes per run (see
Performance below).

### Names

| New name                                                                         | Alternatives                        | Existing names it might be confused with                                    |
| -------------------------------------------------------------------------------- | ----------------------------------- | --------------------------------------------------------------------------- |
| `SurfaceSpec`, `ViewSpec`, `MappingSpec`, `TemplateSpec`, `RepeatSpec`           | `*Config`, `*Definition`            | `DeviceSpec`, which it matches; `View::Config` and `ViewMapping::Config`, which they hold |
| `ComponentSpec`: `ModifierSpec`, `EnumeratedValueSpec`, `TrackReferenceSpec`, ... | `*Declaration`                      | The property classes they declare (`EnumeratedValueProperty`, ...)          |
| `TemplateUse`                                                                    | `TemplateCall`                      | The instances of a repeated view                                            |
| `FixedValue`                                                                     | `ConstValue`                        | `const:` properties, which creating a scene adds for each                   |
| `GetPropertyCatalog()`, `PropertyInfo`, `ReferenceInfo`, `PropertyNamespaceInfo` | `PropertyRegistry`, `PropertyEntry` | A device type's catalog, a `WidgetShape`                                    |
| `GetComponentKinds()`, `ComponentKindInfo`                                        | `ComponentCatalog`                  | `ComponentSpec`, a declared instance of a kind                              |
| `ViewProperty::FindValue()`                                                      | `GetValueByName()`                  | `ViewProperty::SetText()`, which also takes a value's name                  |
| `ValidateSurfaceSpec()`                                                          | `Check*()`                          | `ValidateWidgets()`, which it calls                                         |
| `CreateWidgetDevices()`                                                          | `ValidateDeviceSpecs()`             | `CreateDevices()`, which calls it to check the specs before opening a port  |
| `CreateScene()`                                                                  | `BuildScene()`                      | The `Scene` constructor, which it calls. "Create", as it returns a `unique_ptr`. |
| `GetDefaultSpec()` (`plugin/default_spec.h`)                                     | `GetJprSurfSpec()`                  | `DefaultConfigTest`, which tests it                                         |

### What the scene provides

A config sees three kinds of thing:
- **Built in properties and references**, whose names the scene gives:
  `state:can_redo`, `track:mute`, `view:bank_inc`, `mod:shift`,
  `state:master_track`, and `cmd:<id>` for any command.
- **Component kinds**, the C++ classes a config declares instances of:
  modifier, enumerated value, track reference, track anchor, and pick. The
  scene fixes each kind, where it may be declared, its parameters, and what it
  provides.
- **Declared instances**, such as `user:surface_mode` or `mod:send_hold`, whose
  names are the config's. They are in no catalog: checking learns them from the
  spec's declarations.

The first two are the catalog, which checking and the user docs read. It is
**scene's own tables**, rather than data kept beside them, so nothing the scene
provides can be missing from it. It is in `scene/catalog.h`, except the
access, which every property declares (CL5), so it is in
`scene/view_property.h`:

```
// What a mapping can do with a property.
enum class PropertyAccess { kRead, kWrite, kTrigger };
using PropertyAccessFlags = gb::Flags<PropertyAccess>;

// What a view: property needs of the view it is used in.
enum class ViewNeed {
  kList,             // Any list: child_inc, bank_inc, ...
  kChildTracksList,  // A child tracks list, bound to a writable reference.
  kChildTracksItem,  // An item view of such a list: parent_track_child, ...
  kRoutesList,       // A routes list: child_route_toggle, ...
  kRoutesItem,       // An item view of a routes list, bound to a writable
                     // reference: parent_route_other_track.
};

// One built in property.
struct PropertyInfo {
  std::string_view name;  // Such as "state:can_redo".
  ViewProperty::Type type;
  PropertyAccessFlags access;
  std::string_view description;
  std::optional<ViewNeed> view_need;  // view: properties only.
};

// One built in reference, whose fields are its kind of subject's properties.
struct ReferenceInfo {
  std::string_view name;  // Such as "state:master_track".
  SubjectKind kind;
  std::string_view description;
};

// The built in properties and references of a namespace.
struct PropertyNamespaceInfo {
  std::string_view name;  // Such as "state:".
  std::string_view description;
  absl::Span<const PropertyInfo> properties;
  absl::Span<const ReferenceInfo> references;
};

// Every namespace a config can name a built in property in: cmd:, state:,
// mod:, track:, route:, and view:. cmd: lists none, as only REAPER knows which
// commands exist, so a command is checked only for syntax until the scene is
// created.
absl::Span<const PropertyNamespaceInfo> GetPropertyCatalog();

// What a component parameter's text is: every parameter is written as text,
// so this is what a config may write in it.
enum class ParamKind {
  kNewName,         // A name the config makes up, such as a value's.
  kNewNameList,     // A list of them.
  kValueName,       // One of the names of the component's kNewNameList.
  kTrackReference,  // A track reference: built in, or declared before it.
  kModifier,        // A modifier: built in, or declared.
  kChoice,          // One of the parameter's choices.
};
enum class ParamUse { kRequired, kOptional, kTopLevelOnly };

// One parameter of a kind of component.
struct ParamInfo {
  std::string_view name;
  ParamKind kind;
  ParamUse use;
  std::string_view description;  // What it does, rather than its form.
  absl::Span<const std::string_view> choices;  // For kChoice only.
};

// One kind of component (see Components in the config model).
struct ComponentKindInfo {
  std::string_view name;  // Such as "enumerated_value".
  bool top_level;         // Where it may be declared.
  bool on_view;
  std::string_view description;
  absl::Span<const ParamInfo> params;
};
absl::Span<const ComponentKindInfo> GetComponentKinds();
```

- **The state rows** (`kStateProperties`) already are a table. Each row gains
  a `PropertyInfo` beside its create function, and the catalog reads them. The
  descriptions move from the header's comments to the rows, so they are
  written once.
- **A track's and a route's fields,** created by if-chains in
  `TrackProperties::GetProperty()` and `RouteProperties`, become tables the
  same way, each row with its create function, and so do the built in
  modifiers, which the scene creates from their rows. The built in references
  are a table of `ReferenceInfo` (in `scene.cc`, and `route:other_track` in
  `route_properties.cc`).
- **`view:` properties** are added by each kind of list (`ViewList`), so their
  table (`kViewProperties`, in `view_list.h`) is the names, types, and needs. A
  list adds a property by a `ViewPropertyName`, whose `consteval` constructor
  finds its row, so a property without a row doesn't compile.
- **A test** creates every row's property and checks that it has the row's
  type, checks the scene has each built in reference, and builds a view of each
  need and checks that it has exactly the rows with that need. That is all that
  is left to agree, so there is no check at run time.
- **Access** is declared by each property, as its type is (CL5), so the test
  checks it too. Checking mappings relies on it, and otherwise a row's access
  would only be what someone typed.
- **Left as they are,** as not worth the machinery: the three built in
  references are created by the scene's constructor rather than from their
  rows, so only a test checks each row has one (not that each has a row); and
  `TrackProperties::GetProperty()` still picks out the meter, `has_routes`, and
  touched properties by name after creating them, to notify them of the
  track's meter, routes, and touches.
- **`ComponentKindInfo`** is documentation and checking data for the
  `ComponentSpec` kinds below. From a config, every parameter is text, so each
  parameter says what text it takes (its `ParamKind`, and its choices), and
  whether it must be given, as data rather than prose, which the docs show the
  same way for every kind. A test checks the kinds by name, and that the track
  anchor's actions are the track toggles. Once `ComponentSpec` exists, a test
  checks there is one for each of its kinds, with the parameters its spec
  struct takes.
- **A page for each namespace,** and one of the component kinds, in
  `docs/user/scene/` (`cmd.md`, `state.md`, ..., `components.md`), generated
  by `jpr_doc` and checked in, as the device pages are. Each directory has an
  index (`README.md`), which the user guide links to. Every `.md` file in a
  directory of `docs/user/` is generated, so a page left behind is found.
  `GenerateUserDocPages()` lists every generated page, which the
  `update_user_docs` target (in place of `update_device_docs`) writes, and
  `jpr_doc_test` compares with the checked-in pages. A type is named by
  `ViewProperty::GetTypeName()`, as configs and errors will name it.

### Scene's configs take names

`View::Config`, `ViewMapping::Config`, and `ViewCondition::Config` already name
properties and modes as a config writes them. A spec holds them as they are,
rather than structs of its own that a build translates, once they take names in
the three places they still take numbers:
- **Required modifiers** are names (`{"mod:shift"}`), rather than bits, as a
  modifier's bit is only known once the scene declares it. `AddMapping()`
  resolves them with `Scene::GetModifier()`, failing on a name that isn't a
  modifier.
- **An enumerated property's values,** in a condition, a range end, or a mode
  override, can be value names (`"send_receive"`). Each is resolved to its
  number when the condition or mapping is created, with
  `ViewProperty::ResolveValue()`, which asks `ViewProperty::FindValue()`, which
  `EnumeratedValueProperty` answers from its names. So a condition compares numbers, as today, rather than its text, and a
  name that isn't one of its values fails. Built in enumerated properties have
  no value names yet (*Value names for every enumerated property*), so they
  still take numbers.
- **Fixed values** stay spec only (`FixedValue`), as a `const:` property's
  name only exists once the scene adds it.

The plugin's mappings switch to names in the same CL, so there is one way to
write each.

### The SurfaceSpec

```
struct SurfaceSpec {
  std::vector<DeviceSpec> devices;        // From device_spec.h, as it is.
  WidgetStructDefinition widgets;         // From widget_definition.h, as it is.
  std::vector<ComponentSpec> components;  // Declared at the top level.
  std::vector<TemplateSpec> templates;
  ViewSpec root;
  TrackFilter track_filter = TrackFilter::kMcp;
};

// Components, by kind.
struct ModifierSpec { std::string name; };  // "mod:send_hold"
struct EnumeratedValueSpec {
  std::string name;                 // "user:surface_mode"
  std::vector<std::string> values;  // "track", "send_receive"
  std::string initial;              // Empty for the first.
};
struct TrackReferenceSpec {
  std::string name;                 // "user:folder"
  std::string fallback;             // "state:master_track", or empty.
  std::string follow;               // Or empty.
};
struct TrackAnchorSpec {            // On a view only.
  std::string name;
  TrackBoolProperty action;         // Select, mute, solo, or rec arm.
  std::string modifier;             // A mod: name, or empty.
};
struct TrackPickSpec {
  std::string name;
  std::string reference;            // The reference it sets.
  std::string source;               // At the top level only: a reference.
};
using ComponentSpec = std::variant<ModifierSpec, EnumeratedValueSpec,
                                   TrackReferenceSpec, TrackAnchorSpec,
                                   TrackPickSpec>;

// A value a write mapping always writes, for which creating the scene adds a
// const: property.
struct FixedValue {
  ViewProperty::Type type;
  ViewProperty::Value value;
};

struct MappingSpec {
  std::variant<std::string, FixedValue> property;
  std::string control;  // A widget path (see Names in paths).
  ViewMapping::TypeFlags type;
  ViewMapping::Config config;
};

// A named set of mappings, with parameters. A widget parameter is the first
// name of the paths that use it, and a property parameter is a property name
// without a namespace, which a use replaces with its argument.
struct TemplateSpec {
  std::string name;
  std::vector<std::string> widget_params;
  std::vector<std::string> property_params;
  std::vector<MappingSpec> mappings;
  std::vector<TemplateUse> uses;
};
struct TemplateUse {
  std::string name;
  absl::flat_hash_map<std::string, std::string> widgets;     // To paths.
  absl::flat_hash_map<std::string, std::string> properties;  // To names.
};

// A view repeated over an array: an instance for each element present, each
// showing an item of its parent's list. `element` is the first name of the
// paths that use the instance's element, here and in its descendants.
struct RepeatSpec {
  std::string array;    // "strip"
  std::string element;  // "strip"
};

struct ViewSpec {
  std::string name;   // Unique among siblings.
  View::Config config;
  std::optional<RepeatSpec> repeat;  // Exactly when the subject is a list item.
  std::vector<ComponentSpec> components;
  std::vector<TemplateUse> uses;
  std::vector<MappingSpec> mappings;
  std::vector<ViewSpec> children;
};
```

- **Names in paths.** A path's first name is looked up from the inside out, and
  the innermost wins: a widget parameter of the template the mapping is in, then
  the element of each repeated view it is in, nearest first, then the config's
  top-level widgets. A template sees only its own parameters and the top-level
  widgets, not the elements of a view that uses it, so it means the same
  wherever it is used. An inner name may hide an outer one, as in most
  languages, so a device type that gains a field a config also uses as an
  element never breaks it. So JPRSurf's arrays keep their names, and the track
  list's instances call their element `strip`, as the route list's do.
- **Instances are named by their element's path** (`strip[3]`), which a config
  never spells. Every other view is named, as a sibling's name must be unique.
- **Order.** A view's components are declared, then its uses expanded, then its
  mappings added, then its children built. The order of mappings on a control
  doesn't matter (conditions, modifier sets, and press behaviors keep them
  apart), so a surface test that fails only from the order is a bug in `scene`,
  fixed in its own CL before the plugin's.

### Checking

```
// Returns true if the spec is valid, adding an error for each problem found
// to `errors` (if provided). Checking doesn't stop at the first problem, and
// each error starts with what it is about: a device, a widget, a component, a
// template, or a view by its path from the root, which is "/" (such as
// "/track_mode/track_list/strip[]"), and for a mapping, its control.
bool ValidateSurfaceSpec(const SurfaceSpec& spec,
                         std::vector<std::string>* errors);
```

It checks everything under "Checking and building a config" in the config
model:
- **Devices and widgets:** each device's type, overrides, ports, and name (the
  checks `CreateDevices()` makes before opening a port, moved to a
  `CreateWidgetDevices()` in `device` that both call, which returns the devices'
  names and catalogs), and the widgets with `ValidateWidgets()` on the types'
  catalogs.
- **Components:** each kind declared where it may be, no name declared twice or
  hiding a visible one (the model's rule for declarations, unlike paths), a
  reference's fallback and follow declared before it (or built in), a pick's
  reference writable, an anchor's modifier a modifier, enumerated values unique
  with the initial one among them, an anchor or pick on a view with a track
  subject, and at most 60 declared modifiers (`kModUserCount`). A built in
  modifier is visible everywhere, so a declared modifier can't take its name,
  as the scene doesn't let one replace it yet (the model's precedence rule, see
  Declared properties).
- **Views:** names (C identifiers), the subject's kind from its reference or
  its parent's list, each list on a track subject with a positive bank size,
  its reveal a track reference other than the one it's bound to, exactly one
  repeated child under each view with a list (none elsewhere), over an array
  widget. The root view is the scene's, so it has no name or config.
- **Templates and uses:** each use names a template and gives exactly its
  parameters, and no template uses itself, however indirectly.
- **Mappings,** after expanding uses: the control is a control, with the inputs
  or outputs the property's type needs in the mapping's direction; the property
  resolves where the mapping is (its namespace, the view's subject kind, the
  view's list, a reference's field, a declared name that is visible), with the
  access the direction needs; the mode and each override's modes are the
  control's; and every condition, range end, and override value can be read as
  the property's type, naming one of its values if it is a declared enumerated
  value.

**Shared rules.** Which inputs and outputs a property type can use is decided
today inside `ViewMapping`'s `InitRead*()` and `InitWrite*()` functions, and
how a mode name is found in its private `FindMode()`. Checking calls the same
functions, made available to it, rather than its own copies, so a mapping that
checking accepts can't be one the scene silently ignores. The same goes for a
property's access (`GetAccess()`, CL5), which `ViewMapping` uses as well as
its type: it reads a control into a property only if the property can be
written or triggered, and writes a property to a control only if it can be
read.

**Brittleness:** checking resolves property names by the same rules as `View`
and `Scene` do, written again over the spec rather than over objects. Creating
the scene is the backstop: any mapping or view the scene refuses fails it with
the scene's error. `CreateScene()`'s tests create a valid spec with each kind of
thing checking accepts, and JPRSurf's own spec is both checked and created.

### Creating the scene

```
// Creates the scene the spec describes, on its devices: the scene, which owns
// them, or null, adding why to `errors` (if provided), if the spec isn't
// valid, a required device is absent, or anything fails to be added, such as a
// mapping to a command REAPER doesn't have. The scene isn't active yet.
std::unique_ptr<Scene> CreateScene(const SurfaceSpec& spec, MidiPorts& ports,
                                   RunRegistry& device_registry,
                                   std::vector<std::string>* errors);
```

- **In `create_scene.h`.** It checks the spec, creates the devices
  (`CreateDevices()`), resolves the widgets, creates the `Scene` with the track
  filter, declares the top-level components in order, then builds the views
  depth first: each view's config, components, uses, and mappings, and each
  repeated view once for each element of its array that is present. Every view
  is enabled, as conditions decide which are active.
- **Fixed values** become `const:` properties (`Scene::AddConstProperty()`), one
  for each distinct type and value, and an anchor's modifier name becomes its
  bit.
- **A command that isn't installed** fails its mapping, as `Scene` finds no
  property, so creating the scene fails with an error naming it: a config never
  runs half working.
- **Tests** create scenes on the fake REAPER's ports, with `FakeXTouch` units,
  so a required device can be absent, and a command missing.

### The plugin

- `GetDefaultSpec()` (`plugin/default_spec.h`) returns JPRSurf's surface as a
  spec: what [JPRSurf's surface in the model](../config_model.md#jprsurfs-surface-in-the-model)
  sketches, with the `track_strip` and `auto_mode_button` templates. The
  constants and helpers in `plugin_surface.cc` go with `InitViews()`.
- `PluginSurface` creates its scene with `CreateScene()`, logging each error,
  and activates it. It keeps only the host plumbing: the MIDI ports, the
  runners, and the scene.
- The config model's **Today** notes and its JPRSurf section name the spec, and
  "Getting there" marks this done.

### Performance

Nothing runs per run that didn't before: the scene created from the spec has
the same views, mappings, and properties, so the call count bounds stay exactly
as they are. Conditions on value names compare numbers, as today. As this is
where the model's rule that "a config costs nothing per run" is first proven,
the benchmark runs once at the end, against the bar. Creating the surface isn't
timed, as it happens once, when REAPER loads it; the load check at the end shows
it still loads without errors.

### To confirm

Nothing. Creating the scene relies on one REAPER fact, that a command that
isn't installed has no property (`NamedCommandLookup()` returns 0, and
`kbd_getTextFromCmd()` empty text), which `CreateCommandProperty()` already
relies on, and the fake already models.

## CLs

### CL1 [x] scene: The catalog of what the scene provides

Depends on: nothing.

- The state rows gain their type, access, and description, and a track's and a
  route's fields, the built in modifiers and references, and the `view:`
  properties become tables the same way (see What the scene provides).
- `GetPropertyCatalog()`, and `GetComponentKinds()`.
- The tests of the tables.
- No visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- The table tests, and that they fail when a row's type or a `view:` row's
  need is wrong. A list that adds a `view:` property its table lacks doesn't
  compile.
- The call count bounds don't move, as the same properties are created.

### CL2 [x] doc: Pages for the scene's properties and components

Depends on: CL1.

- The pages' generators in `jpr_doc`, the pages in `docs/user/scene/`, and
  `update_user_docs` in place of `update_device_docs`, with CLAUDE.md to match.
  The user guide links to the pages.

**Verify**
- Standard checks.
- `jpr_doc_test` compares each checked-in page, and fails when a table changes
  without its page.
- The pages read correctly, with working links.

### CL3 [x] scene, plugin: Scene's configs take names

Depends on: nothing.

- Required modifiers by name, and an enumerated property's values by name, with
  `ViewProperty::FindValue()` (see Scene's configs take names).
- The plugin, and the scene tests, write modifiers and value names.
- No visible change.

**Verify**
- Standard checks. The surface tests and call count bounds unchanged.
- Tests of a modifier name, a name that isn't a modifier, a value name in a
  condition, a range, and an override, and a name that isn't a value.

### CL4 [x] device, scene: The SurfaceSpec, and checking its structure

Depends on: CL1, CL3.

- `SurfaceSpec` and its parts.
- `CreateWidgetDevices()` in `device`, which `CreateDevices()` calls.
- `ValidateSurfaceSpec()`, checking devices, widgets, components, views, and
  templates and their uses. Mappings are left to CL6.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Tests of each rule, valid and not, with every error reported rather than the
  first.
- Each component kind's `ParamInfo` matches its spec struct: a parameter for
  each of its fields, with the kind and use checking enforces.

### CL5 [x] scene: Properties declare their access

Depends on: CL1.

- `ViewProperty` takes its access in its constructor, as it does its type, and
  `GetAccess()` returns it. Each property class passes what it supports: a
  `PolledToggleProperty` is writable exactly when it has a write function, an
  action is triggered, and so on. `PropertyAccess` moves from `catalog.h` to
  `view_property.h`, which the constructor needs.
- A toggle that always reads off is write only (`kWriteAccess`): a track
  anchor, and a `CallbackToggleProperty`. A constant is read only.
- The catalog test checks each row's access against its property's, as it does
  its type.
- No visible change.

**Verify**
- Standard checks.
- The catalog test fails when a row's access is wrong.

### CL6 [x] scene: Checking mappings

Depends on: CL4, CL5.

- The rules for a type's inputs and outputs, and for finding a mode, are made
  available outside `ViewMapping`, which still uses them.
- The rule for a property's access joins them (see Shared rules): `ViewMapping`
  drops the read side of a mapping whose property can't be written or
  triggered, and the write side of one whose property can't be read, as it
  does a side the control or type can't use.
- `ValidateSurfaceSpec()` checks mappings, after expanding uses, with names in
  paths resolved from the inside out (see Checking).
- No visible change: no mapping in the default config uses a property against
  its access.

Done as planned, with these details:
- The rules are `ViewMapping::GetReadInput()` and `GetWriteOutput()`, which
  return the one input or output a side uses (or none), and `FindMode()`,
  made public and taking a type. Each `Init*SyncFunction()` now builds its
  function for the input or output they chose, so the preference order is
  written once. A side that reads or writes nothing is dropped from
  `GetType()`. A press release mapping of a timeline position now reads
  nothing, as `ReadConfig` already said any type but a press would.
- A widget shape doesn't say whether a press has a release, so checking takes
  every press to have one, as every catalog's does.
- A declared component's property is checked by its kind's type and access,
  which `ComponentKindInfo` now has (no access for a track reference), and
  the catalog test checks against the classes. A fixed value is checked with
  `IsConstValue()`, made public from `const_property.cc`.
- Checking also covers what the scene would refuse or silently ignore around
  a mapping: view conditions (made before the view's own components, so they
  don't see them), conditions and mode overrides on a property that can't be
  read, range ends that aren't values of the type (or on a type that ignores
  them), required modifiers, and a mapping that neither reads nor writes.
- More rules are shared rather than written again for checking:
  `ViewMapping::CanReadControlInto()` and `CanWriteControlFrom()` (the access
  rule), `ViewProperty::CanEqual()` (which `Equals()` uses), `IsCommandId()`
  (which looking a command up uses), and `ViewList::GetNeeds()` (which the
  catalog test checks against the properties lists add). Built in properties
  are found through `GetPropertyCatalog()`'s namespaces.
- The scene now refuses a condition or mode override on a property that
  can't be read, as checking does, so creating the scene backs that rule up.
- A mapping whose write side is dropped no longer watches its property.
- Left as they are: a range end the scene ignores for the input it reads
  (text from a value input, color from a delta) isn't reported, as the rule
  depends on the input, and the template expansion in checking isn't shared
  with `CreateScene()`, which CL7 decides.

**Verify**
- Standard checks.
- Tests of each mapping rule, including paths through template parameters and
  elements, and an inner name hiding an outer one. `ViewMapping`'s tests still
  pass.
- `ViewMapping` tests of each side dropped for the property's access, and the
  default config's tests still pass.

### CL7 [x] scene: Creating a scene from a spec

Depends on: CL6.

- `CreateScene()` (see Creating the scene).
- Unused, so no visible change.

Done as planned, with these details:
- A command REAPER doesn't have is checked before the mapping, condition, or
  view that names it is added, so its error names it ("command "cmd:_X"
  isn't one REAPER has"). Anything else the scene refuses, which checking
  should have caught, is reported only as "the scene refused it", as `View`
  and `Scene` log why rather than return it. Each error names the view by its
  instance ("/tracks/strip[3]"), so a problem in a repeated view is reported
  for each instance.
- Template expansion is written again rather than shared with checking:
  checking walks a repeated view once over its element's shape, and copes
  with a spec that isn't valid, while creation walks each instance over
  concrete paths, and assumes a valid spec. Only the lookup rules overlap.
- The track references a pick may set are the builder's own (the ones the
  spec declares), as `Scene` gives out only const ones, so nothing else can
  set a built in reference.
- Left as they are: a mapping of a command whose type doesn't suit its
  control (an action written to a light) is checked by neither, and the scene
  drops the side it can't do, so a mapping that does nothing at all is
  possible. The scene is named "Scene", as the plugin's is.

**Verify**
- Standard checks.
- Tests on the fake REAPER and `FakeXTouch`: a spec that isn't valid, a
  required device absent, an optional one absent (its repeated views have
  fewer instances), a command not installed, components, templates, repeated
  views and their instances' subjects, and fixed values sharing one `const:`
  property.

### CL8 [x] plugin, docs: JPRSurf's surface from its spec

Depends on: CL7.

- `GetDefaultSpec()`, and `PluginSurface` creating its scene from it (see The
  plugin).
- The config model's notes and JPRSurf section, as above. The two rows of its
  name audit that the spec settles (modifiers declared by the plugin, and
  generated view names) are gone.
- No visible change: the surface tests pass unchanged.
- Found while doing it:
  - One call count bound falls, and none rises. Turning the automation
    override on makes 917 cheap calls, not 923 (58 reads of the override, not
    65, and one more `CountSelectedTracks2`), as a view's uses are expanded
    before its mappings, so the automation buttons' mappings now come before
    Group's. Putting Group's mapping first again restores 923, and every other
    surface test passes unchanged.
  - A third template, `list_navigation` (Bank and Channel), which both list
    views use, rather than repeating its four mappings.
  - The benchmark, run back to back against the commit before CL1 (9a346e5),
    agrees within the noise. Both are about 10% slower per run than the bar,
    which is Game Bits' profiler, not JPRSurf: the bar's own commit (9d5ec65)
    reproduces the bar on the Game Bits of its day (9a9c661), and is as slow
    as this feature on today's (68a311e), run back to back. Its frames are
    faster on today's (10.5us, not 12.2us), but about 7ns of each REAPER call
    is now counted in the scope that makes it, not in the call. The bar is
    now this CL's run, on today's Game Bits.
  - `PluginTest` adds REAPER's actions to its fake, as REAPER has them: a
    surface's scene now isn't created without them, where before it lost the
    mappings of the missing commands.

**Verify**
- Standard checks. The surface tests, and the call count bounds, unchanged.
- A test that the default spec is valid, with no errors.
- The load check, as this changes what the plugin does as it loads.
- The benchmark, against the bar (see Performance).

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow).

## Checks in REAPER

Nothing should look or feel different. In the user's REAPER, at the end:
- REAPER loads the extension: the load check passed first, and once the surface
  is created, `jprsurf.log` has no new errors, "Slow run" warnings included.
- A quick pass over both modes on the hardware: banking, folders, Send/Receive
  mode, a range, the automation and transport buttons.
- No idle profile, as nothing per run changed: the call counts are exact, and
  the benchmark covers JPRSurf's own time.
