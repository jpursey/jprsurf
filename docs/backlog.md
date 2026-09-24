# Backlog

Ideas for future work, collected from the feature worklogs in `docs/worklog/`
and from using the surface. The worklogs describe what was built; this is what
has not been.

The list is a rough stack rank: the order the items look worth doing, as a best
guess rather than a commitment. Re-order it freely. The items near the bottom
are either large and open ended, or only worth doing if a problem actually
turns up.

Each item carries:

- **Layers**: the JPSurf libraries it touches (`common`, `device`, `scene`,
  `plugin`), in dependency order. More than one usually means more than one CL.
- **Size**: a guess. *Small* is a single CL. *Medium* is a few. *Large* wants a
  design and a `docs/worklog/` plan before any code.
- **Depends on**: other items that should come first, or "nothing".
- **Background**: the worklog holding the context, where there is one.

When an item is picked up it moves into its own `docs/worklog/<feature>.md`
plan (see Feature workflow in CLAUDE.md) and comes out of this list.

## Data driven surface configuration

- **Layers:** scene, plugin
- **Size:** large
- **Depends on:** nothing
- **Background:** none; raised while making Nudge and Marker exclusive

Everything above the `scene` layer is hard coded in C++ today: which devices
connect and on which MIDI ports (`ConnectDevices()`), and every view, mapping,
modifier, and condition (`InitViews()`, `InitModeButtons()`, and the strip
helpers in `control_surface.cc`). Changing what a button does means a rebuild
and a REAPER restart. The goal is for that configuration to come from an
external config file instead, leaving the plugin as a thin host that loads it.

Most mappings are already plain data (a property name, a control name, and a
`ViewMapping::Config`), so they would move over directly. The hard part is the
behavior the plugin writes as C++ callbacks: surface modes and the mode buttons,
track anchors, picking the Send/Receive track, and exclusive toggles like Nudge
and Marker. Each needs to become a named, reusable building block in `scene`
(or a new layer between `scene` and `plugin`) that the config can refer to,
which is also a question of where the line between those layers should sit.

The design should settle the file format and where it lives (REAPER already
passes a config string to `ControlSurface`, and `gb::ReadConfigFromText()`
parses it, but nothing uses the result), how errors are reported, and whether
it reloads without restarting REAPER. It would absorb *Generalizing strip
construction for more than one extender*, and make *Modes for the other assign
buttons* mostly a config change.

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
- **Depends on:** nothing for route mute; selecting routes depends on *Local
  surface-only selection within routes*
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md)

Anchors are Track mode only today. `Anchor<T>`, `AnchorHold`, and the view's
anchor are all generic, but the ranged behavior itself lives in `Track` and has
no route equivalent, so ranged route mute needs the same treatment in `common`.

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

## Move the empty strip check into common

- **Layers:** common, plugin
- **Size:** small
- **Depends on:** nothing
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md)

The plugin checks for an empty strip before anchoring. Moving the check into
`common` would give it to every holder of a track anchor, and would let `Track`
fall back to a normal press when the range turns out to be empty.

## Modes for the other assign buttons

- **Layers:** scene, plugin
- **Size:** large
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

Track and Send/Receive use two of the X-Touch assign buttons. The mode
machinery is already generic: `kModeInfo` gives a mode its name and button,
availability is recomputed only when it can change, and a mode is a view that
is enabled or disabled between runs. What is missing is what the other modes
should do, which is the design work.

## Generalizing strip construction for more than one extender

- **Layers:** plugin
- **Size:** medium
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

Strips are built for a fixed surface: one extender ahead of the X-Touch, with
`Track1..16`, `Route1..15`, and the Info strip as the last X-Touch strip. Making
the strip count and the Info strip position follow the connected devices would
allow a second extender.

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
