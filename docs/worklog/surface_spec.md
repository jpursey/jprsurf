# Build the scene from a SurfaceSpec

The plugin builds its scene from a C++ data structure, the **`SurfaceSpec`**,
rather than imperative calls: its devices, widgets, declared components,
templates, views, mappings, and settings, as the [config
model](../config_model.md) describes them. JPRSurf's own surface is a spec
written in code (`GetDefaultSpec()`), and reading a config file later is just
filling in the same struct (*Config file language and loader*).

Checking a spec and building a scene from it are separate.
`ValidateSurfaceSpec()` needs no REAPER or hardware, only the spec and two
catalogs: the device types' and the **catalog of what the scene provides** (its
built in properties and references, and the kinds of component a config can
declare). `CreateScene()` checks, then builds, and is unit tested on the fake
REAPER and `FakeXTouch`.

## Behavior

Nothing looks or feels different, and the surface tests verify it, unchanged.
- **A spec never runs half working.** `CreateScene()` reports every error it
  finds and creates nothing if there is one, as when a required device is
  absent or a command isn't installed. The plugin logs each error ("Failed to
  create the scene: ...") and runs no surface, as before when the X-Touch was
  absent.
- **Each namespace and the component kinds have a page** in
  `docs/user/scene/` (`cmd.md`, `state.md`, ..., `components.md`, with an index
  in `README.md`), generated from the catalog, which the user guide links to.
  `update_user_docs` writes every generated page (it replaced
  `update_device_docs`), and `jpr_doc_test` fails when one is out of date.
- **Configs take names** where the scene took numbers: required modifiers are
  names (`"mod:shift"`), and an enumerated property's values in a condition, a
  range end, or a mode override can be its value names (`"send_receive"`),
  resolved to numbers when the mapping or condition is created.
- **Every property declares its access** (read, write, or trigger), as it does
  its type. A mapping drops the side its property's access can't support, as
  it does a side its control or type can't use, and the scene refuses a
  condition or mode override on a property that can't be read.

Everything new runs once, when the surface is created. The scene built from
the spec has the same views, mappings, and properties, so per run nothing
changed: the config model's rule that "a config costs nothing per run". One
call count bound fell (turning the automation override on: 923 to 917 cheap
calls), only because the mappings were added in a different order (see Left
for later), and none rose. The benchmark agreed with the commit before the
feature, run back to back.

### Names

- A spec's parts are `*Spec`, as `DeviceSpec` is: `SurfaceSpec`, `ViewSpec`,
  `MappingSpec`, `TemplateSpec`, `RepeatSpec`, and `ComponentSpec`
  (`ModifierSpec`, `EnumeratedValueSpec`, `TrackReferenceSpec`,
  `TrackAnchorSpec`, `TrackPickSpec`). A use of a template is a `TemplateUse`,
  and a value a mapping always writes is a `FixedValue`.
- Views are named as the model's conventions say (`master_fader`,
  `track_mode`, `track_list`, `send_receive_mode`). A repeated view's instances
  are named by their element's path (`strip[3]`), which a spec never spells, so
  the generated `Track1` and `Route1` names are gone.
- Errors start with what they are about: a device, a widget, a top-level
  component by its name, a template (`template track_strip`), or a view by its
  path from the root, which is `/`. A repeated view is its element and `[]`
  while checking (`/track_mode/track_list/strip[]`), and its instance when
  creating (`/track_mode/track_list/strip[3]`). A mapping adds its control, and
  one in a template the templates it came through
  (`/track_mode/track_list/strip[]: track_strip: strip.mute`).

### Names in paths

A path's first name is looked up from the inside out, and the innermost wins: a
widget parameter of the template the mapping is in, then the element of each
repeated view it is in, nearest first, then the top-level widgets. A template
sees only its own parameters and the top-level widgets, so it means the same
wherever it is used. An inner name may hide an outer one, so JPRSurf's
repeated views call their element `strip`, as the arrays they repeat over are
named. A property parameter is a property name without a namespace, which a
use replaces with its argument, in a mapping's property, its condition, and its
mode overrides.

## Checking

`ValidateSurfaceSpec()` checks everything under "Checking and building a
config" in the config model, finding every problem rather than the first, and
skipping the checks a problem makes impossible rather than reporting what
follows from it:
- **Devices and widgets:** the checks `CreateDevices()` makes before opening a
  port (now `CreateWidgetDevices()`, which both call), then `ValidateWidgets()`
  on the types' catalogs.
- **Components:** each declared where its kind may be, in its namespace, with a
  name no visible declaration or built in modifier has. References, picks, and
  anchors name what they need, declared before them or built in, an anchor or
  pick on a view needs a track subject, and there are at most `kModUserCount`
  declared modifiers.
- **Views, templates, and uses:** names, subjects and lists, exactly one
  repeated child under a view with a list, each use giving exactly its
  template's parameters, and no template using itself.
- **Mappings,** with uses expanded: the control, the property where the mapping
  is (as `View` and `Scene` find it), its access, the inputs and outputs its
  type can use, modes, range ends, required modifiers, and every condition and
  mode override.

**Shared rules.** Checking calls the scene's own rules rather than copies of
them, so a mapping it accepts can't be one the scene silently ignores:
`ViewMapping::GetReadInput()` and `GetWriteOutput()` (the one input or output a
side uses), `FindMode()`, `CanReadControlInto()` and `CanWriteControlFrom()`
(the access rule), `ViewProperty::CanEqual()`, `IsCommandId()`, `IsConstValue()`,
and `ViewList::GetNeeds()`. Only the lookup of names is written again, over the
spec rather than over objects, and creating the scene is its backstop.

## Creating the scene

`CreateScene()` checks the spec, creates the devices, resolves the widgets,
creates the `Scene` with the track filter, declares the top-level components in
order, and builds the views from the root: each view's components, then its
template uses, then its mappings, then its children. A repeated view has an
instance for each element of its array that is present, so an absent device's
elements have none. Every view is enabled, as conditions decide which are
active. Each distinct fixed value is one `const:` property, which every mapping
of it shares. The scene isn't active yet: the caller activates it.

Only REAPER knows which commands exist, so a command it doesn't have is checked
before the mapping, condition, or view that names it is added, and reported by
name ("command "cmd:_X" isn't one REAPER has"). Anything else the scene
refuses, which checking should have caught, is reported only as "the scene
refused it", as `View` and `Scene` log why rather than return it.

## Structure

### scene

- **`catalog.h`:** `GetPropertyCatalog()`, the built in properties
  (`PropertyInfo`: name, type, access, description, and a `view:` property's
  `ViewNeed`) and references (`ReferenceInfo`) of each namespace, and
  `GetComponentKinds()` (`ComponentKindInfo`, with each parameter's
  `ParamInfo`: what text it takes, and whether it must be given). They are
  scene's own tables: the state rows (`kStateProperties`), a track's and a
  route's fields, the built in modifiers, and `kViewProperties` (`view_list.h`,
  which a list adds properties from by a `ViewPropertyName`, so a property
  without a row doesn't compile). A test creates every row's property and
  checks its type and access.
- **`view_property.h`:** `PropertyAccess` and `GetAccess()`, set by each
  property's constructor, and `FindValue()` and `ResolveValue()` for value
  names.
- **`surface_spec.h`:** the spec, `GetComponentKind()`, `GetComponentName()`,
  and `ValidateSurfaceSpec()`.
- **`create_scene.h`:** `CreateScene()`, built by a `SceneBuilder` that keeps
  the stack of path names and property arguments while it expands templates.

### device

`CreateWidgetDevices()` (`device_spec.h`) checks a device list, and gives each
device's name and catalog, without opening a port. `CreateDevices()` calls it
first.

### doc

`GenerateUserDocPages()` lists every generated page: each device type's, and
the scene's, from the catalog. A type is named by
`ViewProperty::GetTypeName()`, as configs and errors name it.

### plugin

- **`default_spec.h`:** `GetDefaultSpec()`, JPRSurf's surface, as
  [JPRSurf's surface in the model](../config_model.md#jprsurfs-surface-in-the-model)
  sketches it. It has three templates: `track_strip` (a strip that shows a
  track, used by every track strip and the Info strip), `auto_mode_button`
  (five uses, from the `kAutoModeButtons` table), and `list_navigation` (Bank
  and Channel, used by both list views). The transport steps are a C++ loop
  over `kTransportSteps`, as each step passes a value, which a template can't.
  Small builders (`Read()`, `Write()`, `ReadWrite()`, `Light()`, `Touch()`,
  `When()`, `SetTo()`, and `Held()`) keep it readable.
- **`PluginSurface`** keeps only the host plumbing: the ports, the runners, and
  the scene it creates from `GetDefaultSpec()` and activates.
- **Tests:** `DefaultSpecTest` checks the spec is valid, and the surface tests
  check the scene built from it behaves as the C++ surface did. `PluginTest`
  adds REAPER's actions to its fake, as a scene isn't created without them.

## Building blocks

- **A new feature in JPRSurf's surface** is an edit to `GetDefaultSpec()`:
  mappings, templates, and components, checked by `DefaultSpecTest` before any
  surface test runs.
- **A new component kind** is a `ComponentSpec` alternative, a
  `ComponentKindInfo` row (which the catalog test checks against it), its
  checking in `ValidateSurfaceSpec()`, and its creation in `SceneBuilder`.
- **A new built in property** is a row in its namespace's table, which gives
  its page, its checking, and its test.
- **Checking with the scene's own rules:** a rule the scene enforces is made
  available to checking, rather than written again, so the two can't disagree.
- **Errors that say where:** every error starts with what it is about, so a
  config loader can report them as they are.

## Performance

Nothing new runs per run. The benchmark, run back to back against the commit
before the feature, agreed within the noise. Both were about 10% over the old
bar per run, which was Game Bits' profiler, not JPRSurf: the bar's own commit
reproduced the bar on the Game Bits of its day, and showed the gap on today's.
Its frames are faster, but about 7ns of each REAPER call is now counted in the
scope that makes it, rather than in the call. The bar was replaced with this
feature's run, on today's Game Bits.

## Checked facts

Nothing new about REAPER. Creating the scene relies on a command that isn't
installed having no property (`NamedCommandLookup()` returns 0, and
`kbd_getTextFromCmd()` empty text), which the fake already modeled.

## Left for later

In the backlog:
- *Mapping order changes REAPER calls*: a view's uses are expanded before its
  mappings, so the order a spec writes them in is lost, and call counts depend
  on it.
- *Specific errors from the scene's add functions*, so creating the scene
  reports why it refused something, rather than "the scene refused it".
- *Refuse a mapping that does nothing*: a command whose type doesn't suit its
  control, which neither checking nor the scene reports.
- *Modifiers a mapping ignores*, already there, which the spec's track view
  still works around.
- In *Config file language and loader*: template parameters with defaults, as
  every use of `auto_mode_button` passes `override_light`, which only Latch's
  differs in.

Left as they are, as not worth the machinery: the three built in references
are created by the scene's constructor rather than from their rows, and
`TrackProperties::GetProperty()` still picks out the meter, `has_routes`, and
touched properties by name. Template expansion is written twice, once for
checking (over a repeated view's element shape, coping with a spec that isn't
valid) and once for creating (over each instance's concrete paths). Checking
runs again each time the surface is created, though `DefaultSpecTest` already
proves the built in spec valid, as it costs little, once, and a loaded config
will need it.
