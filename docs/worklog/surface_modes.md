# Surface Modes

JPSurf has two surface modes, selected with the X-Touch assign buttons: Track
mode (`kAssignTrack`) and Send/Receive mode (`kAssignSend`). This describes how
they behave and how they are built, as a reference for adding more modes.

## Modes

- `enum class SurfaceMode { kTrack, kSendReceive }` lives in `plugin/`, named
  by function rather than by X-Touch button label. `kModeInfo` gives each mode
  a name (for property names and logs) and its button.
- There is one global mode. The extender is just more channel strips in every
  mode.
- The surface always starts in Track mode.
- Mode state and all mode logic (availability, button behavior, entering and
  leaving) live in `ControlSurface`. The scene only provides generic building
  blocks.
- Mode buttons are handled while the scene runs, so presses only record a
  request (`requested_mode_`, or the Send press state). `ControlSurface::Run()`
  applies it after `scene_runner_.Run()`, so views are never enabled or
  disabled while the scene is iterating them.
- Each mode switch is logged with its duration (~50us).

### Mode buttons

- Each mode button is off when the mode is unavailable, solid when it is
  available, and blinking when it is the current mode. The current mode stays
  lit even if it is no longer available.
- Each button is written from a `mode_<name>_available` `ToggleValueProperty`,
  with a `mode_overrides` entry on `mode_<name>_active` selecting the blink
  output mode. Presses go to a `mode_<name>_select` `CallbackActionProperty`.
- `McuLight()` note outputs have two output modes: mode 0 is off/on (velocity
  0/127) and mode 1 is off/blink (velocity 0/1). The X-Touch blinks natively.
  `MidiOut` compares note-on velocity, so switching between solid and blinking
  is sent.
- Track is always available. Send/Receive is available when exactly one
  non-master track is selected in REAPER (`TrackCache::GetOnlySelectedTrack()`),
  it is visible on the surface, and it has sends or receives
  (`CanShowRoutes()`).
- Availability is only recomputed when it may have changed: on
  `SetSurfaceSelected`, a track list refresh, or a visibility change.

## View structure

```
root                      global mappings: modifiers (including mod_send_hold),
│                         transport, timecode, misc buttons, mode buttons
├── MasterFader           master track volume
├── TrackMode             enabled in Track mode
│   └── TrackList         kTrack child context; Global, Bank and Channel nav
│       └── Track1..16    one per strip (extender strips first)
└── SendReceiveMode       enabled in Send/Receive mode; its track is the
    │                     Send/Receive track. kSends or kReceives child
    │                     context; Bank and Channel nav. Also holds the Info
    │                     strip mappings directly.
    └── Route1..15        one per strip, skipping the Info strip
```

- Only one of `TrackMode` and `SendReceiveMode` is enabled at a time.
- The Info strip mappings are on `SendReceiveMode` itself rather than a child
  view: a view's own mappings use its own track and aren't affected by its
  child context or banking, and it has the `child_route_type_name` property.
- `SendReceiveMode`'s bank size is its child view count, so Bank Left/Right
  pages through all route strips at once.
- The Send/Receive track is not stored separately: it is always
  `send_receive_mode_view_->GetTrack()`, since route navigation changes it from
  inside the scene.

## Track mode

- Unchanged from before modes, apart from holding Send (below). Each strip's
  controls come from `AddTrackStripMappings()` (mute, solo, rec arm, pan, pot
  button, volume, name, color, meter), plus select and the volume on the bottom
  scribble line.
- Holding select, mute, solo, or rec arm anchors a range of tracks (see
  `docs/worklog/ranged_track_actions.md`). These anchor mappings are only in
  Track mode, not on the Send/Receive Info strip.

### Holding Send

- Send is also a hold modifier (`mod_send_hold`). While it is held:
  - Select lights show which tracks have sends or receives
    (`track_has_routes`) instead of REAPER's selection. The two select light
    mappings switch with conditions on `mod_send_hold`.
  - Pressing a select button enters Send/Receive mode for that track (if it can
    be shown), through a per-strip `pick_send_receive_track_<n>` action with
    `required_modifiers` set to the Send hold modifier. The normal select
    press, double press, and long press are excluded by the modifier masks.
  - Holding Send never changes REAPER's track selection.
- Send acts on release, not press, in every mode. The press records the
  current mode and time (`send_press_mode_`, `send_press_time_`), and
  `ApplySendRelease()` acts once the hold modifier is off, only if:
  - it was held for less than 350ms (`kSendHoldDuration`, the same as a long
    press), so a hold just to look at the select lights does nothing;
  - no track was picked with select while it was held (the pick clears
    `send_press_mode_`); and
  - the mode is unchanged since the press.
- A tap shorter than one frame never turns on the modifier, but the press is
  still recorded, so it is handled as a release.

## Send/Receive mode

- The context is a single track. The track hierarchy has no meaning in this
  mode. Hardware outputs and the master track are not supported.
- Entering shows the track's sends, or its receives if it has only receives
  (`SetSendReceiveTrack()`).

### Route strips

- Fader, pot, and mute control the route's volume, pan, and mute. The pot
  button resets pan to center. A strip past the last route has its pot ring
  off.
- Scribble: the other track's name on top, the route volume on the bottom, and
  the other track's color.
- Select navigates to the other end of the route (`parent_route_other_track`):
  a send goes to the destination track showing its receives, and a receive goes
  to the source track showing its sends. This never changes REAPER's
  selection.
- Rec and Solo are unmapped, so they are cleared.
- Bank and Channel Left/Right scroll through the routes.

### Info strip

- The last X-Touch strip (`kInfoStrip`) shows the Send/Receive track, with the
  same controls as a Track mode strip (fader, pot, mute, solo, rec arm, meter,
  name, color).
- The bottom scribble line shows "Send" or "Recv" (`child_route_type_name`).
- Select is unmapped.

### Changing the track or route type

- A short press of Send toggles between sends and receives if the track has
  both (`View::ToggleChildRouteType()`), and otherwise does nothing. Toggling
  starts from the first route.
- When REAPER's last touched track changes (`OnSetLastTouchedTrack`), the view
  moves to that track, unless it is the master track or isn't visible on the
  surface. It shows the track's sends, or its receives if it has only
  receives. A track with no routes shows empty sends, and the mode stays.
- Hiding the Send/Receive track on the surface doesn't leave the mode.

### Leaving

- Pressing Track returns to Track mode, showing the Send/Receive track among
  its siblings (`EnsureTrackIsVisible()`).
- If the Send/Receive track is deleted, the surface returns to Track mode with
  the track list where it was (or the top level, if its parent was deleted
  too).

## Building blocks

### Routes on `Track` (common)

- `Track::GetSends()`, `GetReceives()`, and `GetRoutes(type)` return
  `TrackRoute`s (other track, volume, pan, mute).
- Route lists and other-track pointers are rebuilt in `TrackCache::Refresh()`.
  REAPER calls `SetTrackListChange()` when routes or hardware outputs are
  added or removed. Hardware outputs and routes to or from the master track are
  skipped.
- Volume, pan, and mute are only polled for the Send/Receive track, through
  `Track::RefreshRoutes()` during the view's sync.
- `TrackListener::OnTrackRoutesChanged()` is called when routes are rebuilt
  with changes, or their values change.
- Setters use the `SetTrackSendUI*` family. REAPER indexes sends after
  hardware outputs, and receives as `-1 - index` when setting; this is kept in
  `Track::GetTrackSendUiIndex()`. The hardware output count is cached per
  track.
- Mute reads REAPER's actual state before toggling
  (`ToggleTrackSendUIMute`, which creates its own undo point).

### Route undo (common)

- `SetTrackSendUIVol` / `SetTrackSendUIPan` with `isend=0` don't create undo
  points, and neither do the `CSurf_On*` route functions.
- `ContinuousUndo` creates an undo point with `Undo_OnStateChangeEx` once a
  series of changes with the same description has stopped for 500ms, or
  earlier when a change with a different description comes in. Route volume
  and pan use "JPR: Adjust send/receive volume/pan". `ControlSurface::Run()`
  calls `Update()` every run.
- Route mute flushes pending changes first, so they aren't folded into its own
  undo point.
- Known limitation: other changes made within the 500ms that don't create
  their own undo point are included in the pending one.

### Clearing unmapped controls (device, scene)

- Mappings that write to a control hold a `ControlOutputHandle` (RAII) while
  they are active. When a control's last writer releases, pending output is
  dropped, and the control clears its outputs on its next `OnRun()` if there
  are still no writers. A mode switch releases and registers mappings between
  two runs, so a control used by both modes never clears or flickers.
- Each output type has a cleared state (off, zero, blank text, black). A device
  overrides it where needed: X-Touch encoder rings clear to their all-off
  mode.
- A mapping that can't write anything for its property doesn't hold a handle,
  so it never keeps a control from clearing.
- On shutdown, `ControlSurface` deactivates the scene, runs the devices and
  MIDI output once more, and waits 100ms so the MIDI is sent before the ports
  are destroyed. This clears the whole surface, including the extender.

### Route views (scene)

- `ChildContextType::kSends` / `kReceives` give child views consecutive routes
  of the view's track, starting at the child context index. Each child view's
  track is the track at the other end of its route (or the stub track past the
  end), so the existing `TrackProperties` show its name and color.
- `RouteProperties` (`route_volume`, `route_pan`, `route_mute`,
  `route_exists`) are bound to (track, route type, index).
- View properties: `parent_route_other_track` (navigate across a route),
  `child_route_toggle` (toggle sends and receives), and
  `child_route_type_name` ("Send" / "Recv"). `View::ToggleChildRouteType()`
  is public for the plugin.
- `track_has_routes`: a read-only track property, true if the track has any
  sends or receives.

### Plugin properties and conditions (scene)

- `Scene::AddProperty()` registers plugin-defined properties, failing on a
  name collision. `ToggleValueProperty` is set by code; `CallbackActionProperty`
  calls a function when triggered.
- `ViewMapping::Config::condition` makes a mapping active only while a
  property in its view's scope has a given bool value. Use conditions for
  write mappings. Read mappings should use `required_modifiers` instead:
  switching a read mapping with a condition re-registers its input, which
  resets any pending press timing.

## Testing

The general process, smoke test, and performance budget are in CLAUDE.md. For
modes, check the steady state `Run()` cost in every mode. Measured when the
modes were finished: ~200us to refresh 103 tracks with routes, and ~50us per
mode switch.

## Future ideas

- Modes for the other assign buttons.
- Local surface-only selection within routes, for grouped volume, pan, and
  mute changes.
- Info strip select button behavior.
- Rec and Solo on route strips.
- Generalizing strip construction for more than one extender.
