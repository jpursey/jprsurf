# Backlog

Ideas for future work, collected from the feature worklogs in `docs/worklog/`
and from using the surface. The worklogs describe what was built; this is what
has not been.

The list is a rough stack rank: the order the items look worth doing, as a best
guess rather than a commitment. Re-order it freely. The items near the bottom
are either large and open ended, or only worth doing if a problem actually
turns up.

Each item carries:

- **Layers**: the JPRSurf libraries it touches (`common`, `device`, `scene`,
  `plugin`), in dependency order. More than one usually means more than one CL.
- **Size**: a guess. *Small* is a single CL. *Medium* is a few. *Large* is
  many, usually after a design.
- **Feature workflow**: whether the item follows the Feature workflow in
  CLAUDE.md, with a design and a `docs/worklog/` plan of CLs. Anything that
  comes down to one or two simple CLs doesn't, and is done as an ordinary
  change.
- **Depends on**: other items that should come first, or "nothing".
- **Background**: the worklog holding the context, where there is one.

When an item that follows the feature workflow is picked up, it moves into its
own `docs/worklog/<feature>.md` plan and comes out of this list. Any other item
comes out of this list in the commit that does it.

## Move surface interaction policy out of common

- **Layers:** common, scene, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md)

`common` is meant to be a REAPER data model that any extension could use, but
surface interaction policy has leaked into it:
- `Track::Ui*()` decides what each modifier means (Ctrl ignores grouping, Shift
  selects a range, and so on) by reading the global modifier state.
- `TrackCache` holds the surface filter, the `TrackAnchor` set of anchors, and
  the last touched track as the root for ranges.
- `Track`'s ranged setters read those implicitly from the singleton.

`common` should keep the primitives, with explicit inputs: set a property on a
range of tracks in one `PreventUIRefresh` scope, with or without grouping, and
select a range. The policy should move to `scene`, where a range is computed
with the view's filter and passed down. `TrackCache` already computes indices
for every filter, so `common` would no longer need a surface filter at all. The
modifier behavior could become mappings, or stay one named "standard" track
action per property.

This makes *Ranges in Send/Receive mode* a `scene` change rather than a
`common` one. It is best done before *Build the scene from a SurfaceSpec*, so
the spec doesn't name track actions that are about to change.

## Property namespaces and names

- **Layers:** scene, plugin
- **Size:** medium
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Properties, Names)

Give every property its final name before the components below add more, so
none of them has to be renamed later:
- **Namespaces:** every property is `namespace:name` (`cmd:`, `state:`, `mod:`,
  `track:`, `route:`, `view:`, or `user:`), and the namespace alone decides
  where a name is looked up. This replaces looking in the view first and then
  the scene, and the check in `Scene::AddProperty()` for names that clash with
  built in ones. Properties the plugin adds itself go in `user:` until the
  components below replace them.
- **Named state rows:** `state:0` to `state:18` become names
  (`state:can_redo`, and so on), looked up by name when a mapping is added. The
  timeline and ruler properties move under `state:` too.
- **The rest of the Names audit:** `track:rec_arm`, `state:secondary_ruler_*`,
  and `mod:marker` and the other declared modifiers.

`track_ui_*` only gets its namespace, as *Move surface interaction policy out
of common* settles those names, and device control names wait for *Device
types and catalogs*. The names are C++ constants today, so this is mostly
mechanical, and the smoke test verifies it.

## View conditions and fixed write values

- **Layers:** scene, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Enabled, Mappings)

- **View conditions:** a view is enabled always, or while a condition on a
  property is met, like a mapping's condition. The scene applies the changes
  between runs, as views can't be enabled or disabled while it is running
  them. This is the deferral that `requested_mode_` and `ApplyRequestedMode()`
  do by hand today, and the two mode views move onto conditions on the
  existing `mode_<name>_active` toggles.
- **Fixed write values:** a write mapping can write a constant instead of a
  property, such as a light that is always on in a view. The mode button
  lights use it once modes are exclusive groups.

## View subjects, lists, and references

- **Layers:** scene, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Property namespaces and names*
- **Background:** [config_model.md](config_model.md) (References, Subjects,
  Lists), [surface_modes.md](worklog/surface_modes.md)

Replace the view's track and child context with the model's subjects and
lists:
- **Subjects:** a view's subject has a kind (none, a track, or a route), and
  comes from its parent, a reference it is bound to, or an item of its
  parent's list. A route's other track becomes the `route:other_track`
  reference, rather than the route view's track.
- **Lists:** children or routes, with a scroll position and a bank size, shown
  by the view's children. A routes list's direction is its own state, with a
  rule for picking it when the view's track changes, replacing `kSends`,
  `kReceives`, and `SetSendReceiveTrack()`. A list can reveal a reference,
  replacing `EnsureTrackIsVisible()`.
- **References:** properties whose value is a subject, with fields
  (`state:selected_track.has_routes`). Built in ones for the master, selected,
  and last touched tracks, and a declared one that is writable, with a fallback
  and a reference to follow, which navigation writes.
- **Keeping current:** references and lists react to track list and
  visibility changes themselves, replacing `RefreshTrackViews()`.

The Send/Receive track becomes `user:current_track`, which follows the last
touched track in every mode, and which the track list reveals (see JPRSurf's
surface in the config model). That should look the same as today, but needs
checking in REAPER. This is the largest and riskiest change to `View`, so it
wants a worklog plan.

## Properties declared on views, and track anchors

- **Layers:** scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Property namespaces and names*
- **Background:** [config_model.md](config_model.md) (Declared properties,
  Track anchor), [ranged_track_actions.md](worklog/ranged_track_actions.md)

- **Declared properties:** a component declared on a view gives each view its
  own instance under one `user:` name, visible in the view and its
  descendants, and one declared at the top level is global. Declaring a name
  that is already visible is an error.
- **Track anchor:** the first component declared on a view, replacing
  `AddTrackAnchorMapping()` and its `anchor_<action>_<n>` properties with
  `user:anchor_select` and so on, the same on every strip. The plugin's empty
  strip check moves into it.
- **`state:auto_override_any_latch`:** a polled state row, rather than a
  property the plugin adds itself.

*Move surface interaction policy out of common* may also move the anchors
themselves into `scene`. Whichever of the two comes second builds on the other.

## Modes from exclusive groups and picks

- **Layers:** device, scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *View conditions and fixed write values*, *View subjects,
  lists, and references*, and *Properties declared on views, and track anchors*
- **Background:** [config_model.md](config_model.md) (Modes, Exclusive group,
  Pick, Tap or hold), [surface_modes.md](worklog/surface_modes.md)

The last of the plugin's surface state and callbacks:
- **Exclusive groups:** at most one member is on, however it is turned on, and
  optionally a default (so exactly one is on) and a condition each member
  requires. Marker and Nudge move onto one, replacing
  `AddExclusiveToggleMapping()`, and the surface modes onto another
  (`user:surface_mode`), replacing `SurfaceMode`, `kModeInfo`, `ModeButton`,
  and the mode functions.
- **Pick:** sets a reference and turns a toggle on, if a field of the source
  is true. It enters Send/Receive mode from the Send button and from a strip's
  select button, replacing `requested_send_receive_track_`,
  `TryEnterSendReceiveMode()`, and the `pick_send_receive_track_<n>`
  properties.
- **Tap:** a press behavior in `device`, alongside long and double press. It
  fires on release if the press was short and the control's held modifier
  wasn't used, replacing `send_press_mode_`, `send_press_time_`, and
  `ApplySendRelease()`.
- **Mode lights:** mappings, using fixed values and conditions.

After this, `PluginSurface` has no surface state or callbacks left: only host
plumbing (see [extension_host.md](worklog/extension_host.md)) and its
mappings, which *Build the scene from a SurfaceSpec* turns into data.

## Named command IDs

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (The escape hatch)

`cmd:` properties take a named command ID, such as `_SWS_ABOUT` or a script's
`_RS...` ID, as well as a number, resolved with `NamedCommandLookup()` when the
property is created. It returns 0 for a name nothing has registered, which is
an error. This also confirms that other extensions' commands are registered
before REAPER creates control surfaces, and whether `kbd_getTextFromCmd()` can
tell that a numeric ID doesn't exist. It is useful straight away: a mapping can
run any script or extension action.

## Widgets

- **Layers:** device
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Widgets)

The names mappings use for hardware, independent of which device a control is
on. A widget is a control, a struct of named widgets, or an array of widgets
of the same shape:
- **Shapes:** a control's shape is its inputs, outputs, and output mode names,
  and struct and array shapes follow from their fields and elements. Device
  catalogs (*Device types and catalogs*) are struct shapes too.
- **Definitions:** a path to a device control or struct, a new struct (with
  includes and exclusions), and joined arrays. Checking catches unknown paths,
  fields defined twice, and array elements whose shapes don't match.
- **Resolving:** paths such as `strip[3].fader` resolve to the `Control`s of
  the devices that are present. A joined array drops the elements of an absent
  device.

Zipped arrays are part of the design, but nothing needs them yet, so they wait
for a device that does. It is pure `device` code with no REAPER dependency, so
all of it is unit tested against hand-built shapes, before any real device
publishes one.

## Device types and catalogs

- **Layers:** device, scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Widgets*
- **Background:** [config_model.md](config_model.md) (Devices)

A registry of device types by name, each with:
- A **catalog**: its controls as a struct widget, with arrays of structs for
  the controls that repeat (the X-Touch's `strip` array), and named output
  modes (solid and blink for lights, the ring styles and off for encoders). It
  is plain data, without ports or REAPER, so it is unit tested.
- A **factory** that creates the `Device` from its ports and **control
  overrides**: inputs or outputs of particular controls that a unit doesn't use.
  `Device` drops them before creating each `Control`, so the extender's fader
  without touch sensing stops being hard coded.

`DeviceXTouch` takes its control names from its catalog, and `Device` checks
once, when it is created, that the controls it built match the catalog,
logging an error if they don't.

The last CL moves the plugin onto device types and widgets. It creates its
devices through the registry, with the X-Touch required and the extender
optional (so the extender alone no longer loads, deliberately). It defines
JPRSurf's widgets in C++, and the scene maps through widget paths and output
mode names rather than `"XTouch/..."` control names and mode numbers. That
removes the device prefixes and the device checks in the strip loops. Apart
from the extender alone, behavior doesn't change, so the smoke test verifies
it.

## Build the scene from a SurfaceSpec

- **Layers:** a new spec library, scene, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Device types and catalogs*, *Modes from exclusive groups and
  picks*, and *Named command IDs*; ideally *Move surface interaction policy out
  of common*
- **Background:** [config_model.md](config_model.md)

The plugin builds its scene from a C++ data structure, the `SurfaceSpec`,
instead of imperative calls: devices (by type, required or optional), widgets,
views, templates, mappings, declared components, and settings (the track
filter). JPRSurf's own surface becomes a spec defined in code. Behavior doesn't
change, so the smoke test verifies it, and later reading a config file is just
filling in the same struct.

Checking a spec and building a scene from it are separate:
- **Checking** has no REAPER dependency, so it is unit tested. It checks
  widgets against the device catalogs, and everything that names a property
  against a **property catalog**: plain data giving each built in property's
  namespace, name, type, and whether it reads, writes, or triggers. `scene`
  checks once, when the scene is built, that each catalog entry resolves to a
  property of the same type, logging an error if not. `cmd:` properties are
  only checked for syntax, as only REAPER knows which commands exist.
- **Building** needs REAPER. It creates the devices, stops if a required device
  is absent or a command isn't installed, expands templates and repeated views,
  and creates the scene.

This settles where the spec library sits: checking depends on `device` (for
the catalogs) but not on `scene`, and building depends on `scene`. No state in
the plugin outlives the scene, which is what makes *Reload the config without
restarting REAPER* cheap. If anything still isn't a component by then, the
plugin can register it as a named component the spec refers to, as a stopgap.

## Config file language and loader

- **Layers:** spec library, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Build the scene from a SurfaceSpec*
- **Background:** none

A bespoke language, parsed with `gb/parse` into a `SurfaceSpec`. It is a DSL
rather than `gb::ReadConfigFromText()` for three reasons: maps in `gb::Config`
are unordered, it keeps no source locations for errors, and mappings are too
dense to be one object each.

The config is its own file (or set of files). REAPER's config string only
refers to it, and `ShowConfig()` grows a file selector. The design settles how
errors are reported (log, REAPER console, partial or total failure), and
includes a schema version. A parity test checks that JPRSurf's own config,
parsed, equals the in-code spec, so the translation is verified without REAPER.

## Public config spec and user guide split

- **Layers:** none (docs)
- **Size:** medium
- **Feature workflow:** no
- **Depends on:** *Config file language and loader*
- **Background:** [config_model.md](config_model.md)

The config model doc becomes a public specification of the config language. The
user guide becomes the guide to JPRSurf's own config, and the reference for how
it uses the spec. This comes after the loader, so the spec has been proven by
writing a real config in it.

## Rec and Solo on route strips

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

In Send/Receive mode the route strips leave Rec and Solo unmapped, so they
clear and sit dark. Each route strip's track is the track at the other end of
the route, and the existing `TrackProperties` already cover its solo and rec
arm, so the work is mostly deciding what the buttons should mean on a route
strip.

## Info strip select behavior

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

The Info strip shows the Send/Receive track with the same controls as a Track
mode strip, except that select is unmapped. Decide what select should do there.
It is the one strip whose track is the mode's own subject, so the Track mode
select behavior is not obviously the right answer.

## Ranges in Send/Receive mode

- **Layers:** common, scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Move surface interaction policy out of common*; selecting
  routes also depends on *Local surface-only selection within routes*
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md)

Anchors are Track mode only today. `Anchor<T>`, `AnchorHold`, and the view's
anchor are all generic, but the ranged behavior itself lives in `Track` and has
no route equivalent. Once that policy moves to `scene`, ranged route mute is a
`scene` change built on `common` range primitives.

## Local surface-only selection within routes

- **Layers:** scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

A selection of route strips that lives on the surface only, for grouped volume,
pan, and mute changes across several routes at once. REAPER has no notion of a
selected send or receive, so this is entirely a scene and plugin concept, and
needs its own lights and clearing rules.

## More polled lights

- **Layers:** scene, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [utility_buttons.md](worklog/utility_buttons.md)

Anything REAPER can answer cheaply is a row in the `kPolledToggles` table and a
write mapping to a button. Each one costs a poll every run, so weigh it against
*Cheaper polling for the polled toggles* below. A row can also have a write
function, so the button can change the state too.

## Modes for the other assign buttons

- **Layers:** scene, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** ideally *Modes from exclusive groups and picks*, so new modes
  don't add plugin state
- **Background:** [config_model.md](config_model.md) (Modes),
  [surface_modes.md](worklog/surface_modes.md)

Track and Send/Receive use two of the X-Touch assign buttons. Once modes are
exclusive groups, another mode is another toggle in `user:surface_mode`, a view
enabled by it, and perhaps a pick and a new kind of subject (a track's FX, for
the Plugin button). What is missing is what the other modes should do, which
is the design work.

## Reload the config without restarting REAPER

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** *Config file language and loader*
- **Background:** none

Tear down the scene and build a new one from the config file, perhaps from a
REAPER action. This makes iterating on a config much faster. It should be cheap
if nothing in the plugin outlives the scene.

## Generic MIDI devices defined in config

- **Layers:** device, spec library
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Config file language and loader*
- **Background:** none

Devices are C++ today, which suits the X-Touch (scribble strip sysex, meters,
timecode). A generic device whose controls are defined in the config (MIDI
message to control, with input and output types) would support simple
controllers without any code, much like CSI's surface files. These define
device controls, not widgets (see [config_model.md](config_model.md)).

## Choose a config by the devices present

- **Layers:** spec library, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Config file language and loader*
- **Background:** [config_model.md](config_model.md) (Devices)

A direction rather than a plan. Each device in a config is required or
optional, so one config covers a set of hardware with some units missing. A
config set would go further: several configs, with the one that best fits the
devices actually connected chosen at startup. With only the X-Touch, for
instance, a different config could drop the Info strip so all 8 strips show
routes, rather than just losing the extender's strips. It needs a definition of
"best fits" (such as the config with the most devices present whose required
devices are all present), and a way to say which config was chosen.

## Modifiers a mapping ignores

- **Layers:** device, scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Modifier sets)

A read mapping fires only when exactly its required modifiers are on, among all
the modifiers the read mappings on its control mention. That keeps Undo and
Redo, or Rewind by measure, beat, and marker, unambiguous without priorities.
But a mapping meant to fire with a modifier held "whatever else is held" needs
a copy for every combination of the other modifiers on the control. Today that
is the Send/Receive pick on a strip's select button, which needs a second
mapping for Send held along with a select anchor (`InitViews()` in
`plugin_surface.cc`).

A mapping could also list modifiers it ignores, so the pick is one mapping that
requires `mod:send_hold` and ignores `mod:select_anchor`. It is purely
additive: existing mappings and configs mean the same thing either way, so it
can be done at any time, such as when a second case turns up.

## Read-only toggle mappings register for changes they ignore

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md)

A read-only toggle mapping still registers for property change notices, so it
calls `WriteControl()`, which does nothing, on every change. Registering only
when the mapping actually writes the control would skip that. No user-visible
effect, so it needs temporary logging or a performance measurement to show the
difference.

## Cheaper polling for the polled toggles

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [utility_buttons.md](worklog/utility_buttons.md)

Only worth doing if a polled read becomes a problem. `kStateAnyItemSelected`
costs a steady ~10us of the `Run()` average today, which was accepted. In
order:

1. **Throttle.** Give each `kPolledToggles` row a poll interval in runs, and
   have `PolledToggleProperty::UpdateState()` read only every Nth run. Polling
   `kStateAnyItemSelected` every 4th run would cut it to ~2-3us, with the light
   lagging by up to ~130ms.
2. **Gate on `GetProjectStateChangeCount(nullptr)`**, which REAPER increments
   whenever the project changes, and re-read only when it moves. This could be
   one switch for every polled state and command toggle state that can use it.
   It is the second choice because whether a selection change bumps the count
   depends on the user's undo preferences.
