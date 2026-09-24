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
- **Size**: a guess. *Small* is a single CL. *Medium* is a few. *Large* wants a
  design and a `docs/worklog/` plan before any code.
- **Depends on**: other items that should come first, or "nothing".
- **Background**: the worklog holding the context, where there is one.

When an item is picked up it moves into its own `docs/worklog/<feature>.md`
plan (see Feature workflow in CLAUDE.md) and comes out of this list.

## Surface config model

- **Layers:** none (design doc)
- **Size:** medium
- **Depends on:** nothing
- **Background:** none; the first step toward a data driven surface

The long term goal is a surface that is entirely driven by a config file, with
the plugin as a thin host that loads it. The items below build toward it, and
this one defines what they build: the config model, as concepts and named
building blocks rather than syntax.

- **Devices:** a device type (X-Touch, X-Touch Extender) and its MIDI ports.
- **Layout:** named, ordered arrays of logical controls (`strip[0..15].fader`)
  assembled from parts of physical devices, so a set of devices is one surface
  and mappings never name a device. Adding a second extender becomes a layout
  change.
- **Views, mappings, and templates:** mappings repeated over a layout array with
  the index substituted, replacing the strip loops and helpers.
- **Behaviors:** the named, parameterized building blocks a config composes
  (modes, anchors, exclusive toggles, polled state). The only logic a config
  expresses is a bool condition on a property. Anything more is a new C++
  building block, and REAPER command IDs (including script commands) are the
  user's escape hatch.

It also names the building blocks the plugin needs today (see *Scene building
blocks for surface behavior*), and audits property names, since a public spec
freezes them (generated names like `anchor_select_3` are the risk).

The result is a durable doc (not a worklog) that ties the items below together
and later seeds the public spec. It also adds a rule to CLAUDE.md so feature
work stops moving away from the goal: a feature is a named building block below
`plugin` plus its use in the surface's spec, and `plugin` gains no new state or
callbacks.

## Extension host in common

- **Layers:** common, plugin
- **Size:** medium
- **Depends on:** nothing
- **Background:** none

Using `common` correctly takes a checklist that every extension must follow:
forward `SetTrackListChange()` and defer `TrackCache::Refresh()` to the next
run, poll `RefreshVisibility()` every second, forward selection and automation
mode changes, set the last touched track, and call `ContinuousUndo::Update()`
every run. The `Extended()` parameter decoding is generic too, but lives in
`ControlSurface`.

A base class in `common` could implement `IReaperControlSurface`, do all of that
plumbing, and expose a small set of targeted overrides (run, track list changed,
visibility changed, selection changed, and so on) that do nothing by default.
An extension that isn't a control surface could still get REAPER's
notifications by registering a hidden instance of it. The design needs to
settle when the plumbing runs relative to the surface's own run, and how it
stays done once per frame if more than one instance exists (see *More than one
JPRSurf instance*).

## Move surface interaction policy out of common

- **Layers:** common, scene, plugin
- **Size:** large
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

This absorbs moving the empty strip check (the plugin checks for an empty strip
before anchoring), and makes *Ranges in Send/Receive mode* a `scene` change
rather than a `common` one. It is best done before *Build the scene from a
SurfaceSpec*, so the spec doesn't name track actions that are about to change.

## Scene building blocks for surface behavior

- **Layers:** device, scene, plugin
- **Size:** large
- **Depends on:** *Surface config model*
- **Background:** [surface_modes.md](worklog/surface_modes.md),
  [ranged_track_actions.md](worklog/ranged_track_actions.md)

Each piece of behavior the plugin writes as C++ callbacks or state becomes a
named, parameterized building block, moved one CL at a time with no change in
behavior:
- **Deferred scene actions:** a queue in `Scene` for work that can't happen
  while the scene runs (enabling and disabling views), replacing
  `requested_mode_` and `ApplyRequestedMode()`.
- **Mode groups:** exclusive views with availability and active properties, and
  an optional context on entry (picking the Send/Receive track is entering a
  mode with a view's track). Replaces `SurfaceMode` and `ModeButton`.
- **Tap on release:** a press behavior alongside long and double press,
  replacing the Send/Receive button's press timing state.
- **Property types** for track anchors, exclusive toggles, and named polled
  state, replacing `AddTrackAnchorMapping()`, `AddExclusiveToggleMapping()`,
  and the Latch lambda.
- **Views own their track list refresh:** a view reacts to track list and
  visibility changes itself, and can scroll to show a track, replacing
  `RefreshTrackViews()` and `EnsureTrackIsVisible()`.

This makes *Modes for the other assign buttons* mostly a matter of using mode
groups.

## Build the scene from a SurfaceSpec

- **Layers:** scene (or a new spec library above it), plugin
- **Size:** large
- **Depends on:** *Surface config model*; ideally *Scene building blocks for
  surface behavior* and *Move surface interaction policy out of common*
- **Background:** none

The plugin builds its scene from a C++ data structure, the `SurfaceSpec`,
instead of imperative calls: device types from a registry by name, the layout,
views, templated mappings, and behaviors. JPRSurf's own surface becomes a spec
defined in code. Behavior doesn't change, so the smoke test verifies it, and
later reading a config file is just filling in the same struct.

Validating a spec and building a scene from it should be separate, so that
validation has no REAPER dependency and can be unit tested. Any behavior not yet
moved into `scene` can be a named behavior the plugin registers, which the spec
refers to by name. This absorbs *Generalizing strip construction for more than
one extender*. No state in the plugin should outlive the scene, which is what
makes *Reload the config without restarting REAPER* cheap.

## Config file language and loader

- **Layers:** spec library, plugin
- **Size:** large
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
- **Depends on:** *Config file language and loader*
- **Background:** none

The config model doc becomes a public specification of the config language. The
user guide becomes the guide to JPRSurf's own config, and the reference for how
it uses the spec. This comes after the loader, so the spec has been proven by
writing a real config in it.

## Rec and Solo on route strips

- **Layers:** plugin
- **Size:** small
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
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

The Info strip shows the Send/Receive track with the same controls as a Track
mode strip, except that select is unmapped. Decide what select should do there.
It is the one strip whose track is the mode's own subject, so the Track mode
select behavior is not obviously the right answer.

## Ranges in Send/Receive mode

- **Layers:** common, scene, plugin
- **Size:** medium
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
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

A selection of route strips that lives on the surface only, for grouped volume,
pan, and mute changes across several routes at once. REAPER has no notion of a
selected send or receive, so this is entirely a scene and plugin concept, and
needs its own lights and clearing rules.

## More polled lights

- **Layers:** scene, plugin
- **Size:** small
- **Depends on:** nothing
- **Background:** [utility_buttons.md](worklog/utility_buttons.md)

Anything REAPER can answer cheaply is a row in the `kPolledToggles` table and a
write mapping to a button. Each one costs a poll every run, so weigh it against
*Cheaper polling for the polled toggles* below. A row can also have a write
function, so the button can change the state too.

## Modes for the other assign buttons

- **Layers:** scene, plugin
- **Size:** large
- **Depends on:** ideally *Scene building blocks for surface behavior*, so new
  modes don't add plugin state
- **Background:** [surface_modes.md](worklog/surface_modes.md)

Track and Send/Receive use two of the X-Touch assign buttons. The mode
machinery is already generic: `kModeInfo` gives a mode its name and button,
availability is recomputed only when it can change, and a mode is a view that
is enabled or disabled between runs. What is missing is what the other modes
should do, which is the design work.

## Reload the config without restarting REAPER

- **Layers:** plugin
- **Size:** small
- **Depends on:** *Config file language and loader*
- **Background:** none

Tear down the scene and build a new one from the config file, perhaps from a
REAPER action. This makes iterating on a config much faster. It should be cheap
if nothing in the plugin outlives the scene.

## Refuse a second JPRSurf instance

- **Layers:** plugin
- **Size:** small
- **Depends on:** nothing
- **Background:** none

REAPER lets the user add JPRSurf more than once, and nothing defines what
happens then. Until *More than one JPRSurf instance* makes it well defined, a
second instance should fail to initialize, with an error saying why, and leave
the first instance untouched. This is the trivially valid version of that
design.

## More than one JPRSurf instance

- **Layers:** common, scene, plugin
- **Size:** medium
- **Depends on:** *Extension host in common*, *Refuse a second JPRSurf instance*
- **Background:** none

Allow more than one JPRSurf instance, perhaps with different configs. Whatever
the design, the behavior must be well defined when instances conflict: two
configs fighting over the same device controls (or MIDI ports), or the same
REAPER state or actions. Either such a set of configs is invalid and fails to
initialize with a clear error, or the design says exactly how they share, which
is more complex.

`TrackCache`, `ContinuousUndo`, and the global modifier state are singletons,
so each is a decision: shared across instances (with the plumbing done once per
frame), or per instance. Worth deciding on purpose rather than by accident.

## Generic MIDI devices defined in config

- **Layers:** device, spec library
- **Size:** large
- **Depends on:** *Config file language and loader*
- **Background:** none

Devices are C++ today, which suits the X-Touch (scribble strip sysex, meters,
timecode). A generic device whose controls are defined in the config (MIDI
message to control, with input and output types) would support simple
controllers without any code, much like the widgets in CSI's surface files.

## Read-only toggle mappings register for changes they ignore

- **Layers:** scene
- **Size:** small
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
