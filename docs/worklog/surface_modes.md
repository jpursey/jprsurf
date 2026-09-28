# Surface Modes

JPRSurf has two surface modes, selected with the X-Touch assign buttons: Track
mode (`kAssignTrack`) and Send/Receive mode (`kAssignSend`). This describes how
they behave and how they are built, as a reference for adding more modes.

## Modes

- The mode is `user:surface_mode`, an `EnumeratedValueProperty` with the
  values `track` (`kTrackMode`) and `send_receive` (`kSendReceiveMode`), named
  by function rather than by X-Touch button label (see
  [enumerated_values_and_picks.md](enumerated_values_and_picks.md)).
- There is one global mode. The extender is just more channel strips in every
  mode.
- The surface always starts in Track mode.
- Modes are built entirely from scene pieces, mapped in `PluginSurface`: the
  enumerated value, view conditions, picks, and mappings. The plugin holds no
  mode state.
- Each mode's view has a condition on one value of `user:surface_mode` (see
  `View::AddChildView()`). Entering a mode only sets the value, so it can
  happen at any time, including from a mapping while the scene runs. The scene
  applies the change before it next runs the views, so views are never
  activated or deactivated while the scene is iterating them.
- The scene logs each mode view's activation and deactivation with its
  duration.

### Mode buttons

- The current mode's button blinks. Otherwise, a mode's button is solid when
  the mode is available, and off when it isn't. The current mode stays lit
  even if it is no longer available.
- Each mode view lights both buttons: Track mode blinks Track (a `const:` on
  toggle, in output mode 1), and lights Send from
  `state:selected_track.has_routes`. Send/Receive mode blinks Send, and lights
  Track.
- Track sets `user:surface_mode` to `track` (a range whose ends are the same).
  Tapping Send in Track mode enters Send/Receive mode (see Holding Send).
- `McuLight()` note outputs have two output modes: mode 0 is off/on (velocity
  0/127) and mode 1 is off/blink (velocity 0/1). The X-Touch blinks natively.
  `MidiOut` compares note-on velocity, so switching between solid and blinking
  is sent.
- Track is always available. Send/Receive is available when the selected track
  (`state:selected_track`: exactly one non-master track is selected in REAPER,
  and it is on the surface) has sends or receives. The light follows selection,
  track list, and route changes.

## View structure

```
root                      global mappings: modifiers (including mod:send_hold),
│                         transport, timecode, misc buttons, mode buttons
├── MasterFader           master track volume
├── TrackMode             active in Track mode
│   └── TrackList         kTrack child context; Global, Bank and Channel nav
│       └── Track1..16    one per strip (extender strips first)
└── SendReceiveMode       active in Send/Receive mode; its track is the
    │                     Send/Receive track. kSends or kReceives child
    │                     context; Bank and Channel nav. Also holds the Info
    │                     strip mappings directly.
    └── Route1..15        one per strip, skipping the Info strip
```

- Only one of `TrackMode` and `SendReceiveMode` is active at a time, as
  `user:surface_mode` has one value.
- The Info strip mappings are on `SendReceiveMode` itself rather than a child
  view: a view's own mappings use its own track and aren't affected by its
  child context or banking, and it has the `view:child_route_type_name`
  property.
- `SendReceiveMode`'s bank size is its child view count, so Bank Left/Right
  pages through all route strips at once.
- The Send/Receive track is `user:current_track`, which `SendReceiveMode` is
  bound to, and which picks and route navigation change.

## Track mode

- Unchanged from before modes, apart from holding Send (below). Each strip's
  controls come from `AddTrackStripMappings()` (mute, solo, rec arm, pan, pot
  button, volume, name, color, meter), plus select and the volume on the bottom
  scribble line.
- Holding select, mute, solo, or rec arm anchors a range of tracks (see
  `docs/worklog/ranged_track_actions.md`). These anchor mappings are only in
  Track mode, not on the Send/Receive Info strip.

### Holding Send

- Send is also a hold modifier (`mod:send_hold`). While it is held:
  - Select lights show which tracks have sends or receives
    (`track:has_routes`) instead of REAPER's selection. The two select light
    mappings switch with conditions on `mod:send_hold`.
  - Pressing a select button enters Send/Receive mode for that track, if it has
    sends or receives, and otherwise does nothing. Each strip declares
    `user:pick_track`, which sets `user:current_track` to its track, and maps
    it and a mapping that sets `user:surface_mode` to `send_receive`, both
    with `required_modifiers` set to the Send hold modifier and the condition
    `track:has_routes`. The normal select press, double press, and long press
    are excluded by the modifier masks.
  - Holding Send never changes REAPER's track selection.
- Send acts on a tap (`PressBehavior::kTap`): a release within 350ms of the
  press, the same as a long press, so a hold just to look at the select lights
  does nothing. Each mode view maps what a tap does. In Track mode it runs
  `user:pick_selected_track` (from `state:selected_track`) and sets
  `user:surface_mode` to `send_receive`, both with the condition
  `state:selected_track.has_routes`.
- Picking a track with select changes the mode, which drops the pending tap, so
  releasing Send afterwards does nothing. Releasing it in the frame right after
  a pick, before the views switch, runs Track mode's tap too.

## Send/Receive mode

- The context is a single track. The track hierarchy has no meaning in this
  mode. Hardware outputs and the master track are not supported.
- The Send/Receive track is `user:current_track`. Entering sets it, and shows
  the track's sends, or its receives if it has only receives (the routes
  list's `kSendsUnlessOnlyReceives` rule). Re-entering for the track it last
  showed keeps the route type and position it had.

### Route strips

- Fader, pot, and mute control the route's volume, pan, and mute. The pot
  button resets pan to center. A strip past the last route has its pot ring
  off.
- Scribble: the other track's name on top, the route volume on the bottom, and
  the other track's color.
- Select navigates to the other end of the route
  (`view:parent_route_other_track`): a send goes to the destination track
  showing its receives, and a receive goes to the source track showing its
  sends. This never changes REAPER's selection.
- Rec and Solo are unmapped, so they are cleared.
- Bank and Channel Left/Right scroll through the routes.

### Info strip

- The last X-Touch strip (`kInfoStrip`) shows the Send/Receive track, with the
  same controls as a Track mode strip (fader, pot, mute, solo, rec arm, meter,
  name, color).
- The bottom scribble line shows "Send" or "Recv"
  (`view:child_route_type_name`).
- Select is unmapped.

### Changing the track or route type

- A tap of Send toggles between sends and receives if the track has both
  (`view:child_route_toggle`), and otherwise does nothing. Toggling starts from
  the first route.
- `user:current_track` follows REAPER's last touched track, unless it is the
  master track or isn't on the surface, so the view moves to a newly touched
  track. It shows the track's sends, or its receives if it has only receives.
  A track with no routes shows empty sends, and the mode stays. Touching the
  last touched track again does nothing.
- Hiding the Send/Receive track on the surface doesn't leave the mode.

### Leaving

- Pressing Track returns to Track mode, showing the Send/Receive track among
  its siblings, as the track list reveals `user:current_track` when it becomes
  active.
- If the Send/Receive track is deleted, the surface stays in Send/Receive mode,
  showing nothing until another track is touched.

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
  and pan use "JPR: Adjust send/receive volume/pan". `ControlSurface` (in
  `common`) calls `Update()` after every run.
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
- On shutdown, `PluginSurface` deactivates the scene, runs the devices and
  MIDI output once more, and waits 100ms so the MIDI is sent before the ports
  are destroyed. This clears the whole surface, including the extender.

### Route views (scene)

- A routes list (`View::Routes`, see [view_subjects.md](view_subjects.md))
  gives its list items consecutive routes of the view's track, from its scroll
  position. Each item's subject is a route reference, whose
  `route:other_track` is the track at the other end of its route (or nothing
  past the end), for its name and color.
- `RouteProperties` (`route:volume`, `route:pan`, `route:mute`,
  `route:exists`) are bound to (track, route type, index).
- View properties: `view:parent_route_other_track` (navigate across a route),
  `view:child_route_toggle` (toggle sends and receives), and
  `view:child_route_type_name` ("Send" / "Recv"). Send/Receive mode's Send tap
  runs `view:child_route_toggle`.
- `track:has_routes`: a read-only track property, true if the track has any
  sends or receives.

### Plugin properties and conditions (scene)

- `Scene::AddUserProperty()` and `View::AddUserProperty()` declare
  plugin-defined properties, failing on a name collision.
  `EnumeratedValueProperty` and `ToggleValueProperty` hold values set by
  mappings or code, and `TrackPickProperty` sets a track reference.
- `ViewMapping::Config::condition` makes a mapping act only while a property in
  its view's scope equals a value. A write mapping writes only while it is met.
  A read mapping keeps its input, and checks the condition when the input
  arrives, so it never loses a pending press (see
  [enumerated_values_and_picks.md](enumerated_values_and_picks.md)).

## Testing

The general process, smoke test, and performance budget are in CLAUDE.md. For
modes, check the steady state `Run()` cost in every mode. Measured when the
modes were finished: ~200us to refresh 103 tracks with routes, and ~50us per
mode switch.
