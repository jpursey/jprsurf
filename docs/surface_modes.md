# Surface Modes

Plan and progress tracker for adding surface modes to JPSurf, starting with
Track mode (current behavior) and Send/Receive mode.

Status key: `[ ]` not started, `[~]` in progress, `[x]` submitted.

## Design summary

### Modes

- `enum class SurfaceMode { kTrack, kSendReceive }` lives in `plugin/`. Named
  by function, not by X-Touch button label. More modes will be added later.
- The surface always starts in Track mode. There is one global mode; the
  extender is just more channels in every mode.
- Mode state and all mode logic (availability, press behavior) live in the
  plugin. The scene provides only generic building blocks.
- Mode changes requested from inside scene sync (press callbacks) are only
  recorded. `ControlSurface` applies them after `scene_runner_.Run()`, so views
  are never enabled or disabled while the scene is iterating them.

### Mode buttons

- Each `kAssign*` button has three light states: off (mode unavailable), solid
  (available), and blinking (active).
- `ControlDValueOutputMidiNote` gets output modes: mode 0 is off/on (velocity
  0/127), mode 1 is off/blink (velocity 0/1). The X-Touch blinks natively.
- Each mode button is written from an `available` toggle property, with a
  `mode_override` on an `active` toggle property selecting the blink mode.
- Track is always available. Send is available when exactly one non-master track
  is selected in REAPER, it is visible on the surface, and it has sends or
  receives.

### View structure

```
root                  global: master fader, transport, modifiers, timecode,
│                             mode buttons
├── TrackMode         today's TrackList (strips + bank/channel/Global nav)
└── SendReceiveMode   one track; shows its sends or its receives
    ├── Routes        route strips (all but the last), kSends/kReceives context
    └── Info          last strip on the X-Touch, bound to the current track
```

Only one mode view is enabled at a time.

### Clearing unmapped controls

- When a control has no active write mappings, its outputs are reset to a
  cleared state, so nothing is left showing stale values from another mode (for
  instance, Rec and Solo lights in Send/Receive mode).
- Each output type has a generic cleared state (off, zero, blank text, black).
  A device overrides it where the hardware needs something else, like the MCU
  encoder ring's all-off mode.
- Write mappings register with their control while active. When the last one
  unregisters, the control checks again on its next run and only clears if
  there are still no writers. A mode switch unregisters and registers mappings
  between two runs, so a control used by both modes is never cleared, and there
  is no flicker or motor fader dip.

### Send/Receive mode

- Context is a single track. The track hierarchy has no meaning in this mode.
- Hardware outputs and the master track are not supported.
- Entering shows the track's sends, or its receives if it has no sends.
- Route strips:
  - Fader, pot, and mute: volume, pan, and mute of the send or receive.
  - Scribble: other track's name on top, route volume on the bottom.
  - Color: the other track's color.
  - Select: navigate to the other end of the route. A send goes to the
    destination track showing its receives; a receive goes to the source track
    showing its sends. This never changes REAPER's track selection.
  - Rec and Solo: unmapped for now.
  - Bank/Channel Left/Right scroll through routes. Banking counts only route
    strips.
- Info strip (last strip):
  - Fader, pot, mute, solo, rec arm, and meter: same as Track mode, for the
    current track.
  - Scribble: current track name on top, "Send" or "Recv" on the bottom.
  - Color: current track's color.
  - Select: unmapped for now.
- Pressing Send again toggles between sends and receives if the track has both.
  Otherwise it does nothing.
- REAPER changing the last touched track moves the view to that track (sends,
  else receives, else empty strips). The surface is not forced out of the mode.
- Pressing Track returns to Track mode showing the current track among its
  siblings (`EnsureTrackIsVisible`).
- If the current track is deleted, return to Track mode, with the track list
  where it was when Send/Receive mode was entered (or the top level, if that
  parent track was deleted too).

### Holding Send in Track mode

- While Send is held, select lights show which tracks have sends or receives,
  and pressing a lit select button enters Send/Receive mode for that track.
- Because of this, Send acts on release rather than press, in every mode. A
  release only acts if the mode is the same as when Send was pressed. Otherwise
  entering via hold Send + select would be followed by the release toggling
  from sends to receives.

### Common: routes on `Track`

- `Track` exposes `GetSends()` and `GetReceives()`, each a list of `TrackRoute`
  (other track, volume, pan, mute).
- Route lists and other-track pointers are rebuilt in `TrackCache::Refresh()`.
  REAPER calls `SetTrackListChange()` when routes are added or removed.
- Volume, pan, and mute are polled only for the track Send/Receive mode is
  showing, through `Track::RefreshRoutes()`.
- Setters use `SetTrackSendUIVol` / `SetTrackSendUIPan` so REAPER's UI and undo
  behave normally.

### Future ideas (not planned)

- Local surface-only selection within routes, for grouped volume/pan/mute
  changes.
- Info strip select button behavior.
- Rec and Solo on route strips.
- Generalizing strip construction for more than one extender.

## Verification

### Every CL

- Builds cleanly in Release (`out/build/x64-Release`). REAPER must be closed
  first, or the copy into the REAPER plugin directory fails.
- Touched files pass `clang-format --dry-run -Werror`.
- Code review by Claude and by John.
- REAPER loads the extension and `jprsurf.log` has no new errors.
- **Smoke test:** Track mode still works: faders, pots, pot buttons, mute, solo,
  rec arm, select (press, double press, long press), bank/channel navigation,
  Global, master fader, transport, timecode, meters, and scribble names/colors.

Many CLs have no user-visible effect until a later plugin CL uses them. For
those, verification is the above only.

### Performance

Checked by running REAPER and reading `jprsurf.log`.

- **Steady state:** the periodic `Run()` log line stays in the low hundreds of
  microseconds (avg), in both modes, and doesn't regress from before the CL.
- **Refresh or mode change:** no single refresh or mode change exceeds the low
  milliseconds. A frame is ~33ms, shared with REAPER's own UI thread work.
  - Refresh cost is measured by the log added in P0. Test with a large project.
  - Mode change cost is measured by the log added in P3.
  - The `max` value in the periodic `Run()` log line also catches spikes.

## CLs

Each CL is limited to one library where possible. Dependencies are listed by
CL id.

### Phase 1: Mode buttons

- [x] **C4 (common): MIDI note state tracks on velocity**
  - `MidiOut::UpdateState` only sent note changes between on and off, so
    changing a light from on (velocity 127) to blinking (velocity 1) was
    dropped. Off messages stay equivalent; on messages now also compare
    velocity.
  - Also fixes a stale pending change being sent: an update matching the last
    sent state now cancels any pending change for that key (for example, a
    light going on, off, and on again within one frame no longer ends off).
  - **Verify:** every CL checks, paying attention to all button lights still
    turning fully on and off. Blinking is tested in D1.

- [x] **P0 (plugin): Log refresh duration**
  - Log how long `TrackCache::Refresh()` plus `RefreshTrackViews()` take, and
    the track count, replacing the "Refreshing TrackCache!" log. This is an
    infrequent event, so the log stays.
  - **Verify:**
    - Every CL checks.
    - Add, delete, and reorder tracks in a large project, and record the logged
      refresh times as a baseline.
    - Baseline (after C2): ~200us for 103 tracks with sends and receives.

- [x] **D1 (device): Blink output mode for MIDI note lights**
  - Depends on: C4.
  - Add output modes to `ControlDValueOutputMidiNote`: mode 0 sends 0/127,
    mode 1 sends 0/1. `max_value` stays 1, so existing toggle mappings are
    unchanged.
  - **Verify:**
    - Every CL checks, paying attention to all button lights still turning
      fully on and off.
    - Local-only check, not committed: map a button light with
      `.write = {.mode = 1}` and confirm it blinks on the X-Touch.

- [x] **S1 (scene): Plugin-registered properties**
  - `Scene::AddProperty(std::unique_ptr<ViewProperty>)`, failing on a name
    collision, including built-in names created on demand.
  - Generic property types in `value_property.h`: `ToggleValueProperty` (set by
    code, notifies on change) and `CallbackActionProperty` (invokes a function
    on trigger).
  - **Verify:** every CL checks. Tested with a temporary Send button mapping
    (removed before submitting): duplicate and built-in names are rejected, and
    a callback cycling the light through off, solid, and blinking covered every
    transition. First used in P2.

- [x] **C1 (common): Single selected track**
  - `TrackCache` exposes the selected track when exactly one non-master track
    is selected, or null otherwise. It doesn't filter by visibility; callers
    check that themselves.
  - **Verify:** every CL checks. Tested with temporary logging in
    `SetSurfaceSelected` (removed before submitting). First used in P2.

- [x] **C2 (common): Route lists on `Track`**
  - Depends on: P0 (for measuring).
  - `TrackRoute` struct and `Track::GetSends()` / `GetReceives()`.
  - Built in `TrackCache::Refresh()`: other track pointer per route. Hardware
    outputs and the master track are skipped.
  - Volume/pan/mute values come in C3.
  - **Verify:**
    - Every CL checks.
    - Performance: repeat the P0 refresh measurements with a project that has
      many sends. Refresh times stay in the low milliseconds.
    - Route correctness is tested in P2 (availability) and P4 (route strips).

- [x] **P1 (plugin): Restructure views for modes** (no behavior change)
  - Move the track list under a `TrackMode` view.
  - **Verify:**
    - Every CL checks. The smoke test is the main check here, since everything
      it covers was moved.
    - Also check folder navigation (double press into a folder, long press out,
      Global to the top), and hidden tracks in the MCP.

- [x] **P2 (plugin): Surface mode state and mode button lights**
  - Depends on: D1, S1, C1, C2, P1.
  - `SurfaceMode { kTrack, kSendReceive }`, current mode, and pending request
    handling after the scene runs.
  - `available` / `active` properties for Track and Send, mapped to
    `kAssignTrack` and `kAssignSend`.
  - Pressing an available mode button changes the mode and moves the blink.
    No views change yet.
  - Don't call `GetOnlySelectedTrack()` every run. Recompute Send availability
    only when REAPER reports a selection change (`SetSurfaceSelected`) or the
    track list refreshes.
  - **Verify:**
    - Every CL checks, including steady state performance. Availability should
      only be recomputed on selection changes and refreshes, so the steady
      state cost shouldn't change.
    - On REAPER start, Track blinks and Send is off.
    - Send light in Track mode (Track blinking throughout):
      - One track with sends selected: Send is solid.
      - One track with only receives selected: Send is solid.
      - One track with no sends or receives selected: Send is off.
      - Two tracks with sends selected: Send is off.
      - Only the master track selected: Send is off.
      - One track with sends selected, but hidden in the MCP: Send is off.
      - No tracks selected: Send is off.
      - Add a send to the selected track in REAPER: Send turns solid. Remove it:
        Send turns off.
    - Press Send while it is solid: Send blinks and Track turns solid.
    - Press Send while it is off: nothing changes.
    - Press Track in Send/Receive mode: Track blinks and Send returns to solid
      or off based on the selection.
    - In Send/Receive mode, deselect the track in REAPER: Send keeps blinking.
    - Track mode controls keep working in both modes (no view switching yet).

### Phase 2: Mode switching

- [x] **D2 (device): Clear control outputs with no writers**
  - Output types get a cleared state: CValue 0, DValue a configurable value and
    mode (default 0), empty text, and black. The X-Touch encoder rings clear to
    their all-off mode.
  - `Control::RegisterOutputWriter()` returns a `ControlOutputHandle` (RAII,
    like `ControlInputHandle`), and the control counts registered writers. When
    the count drops to zero, pending output is dropped, and the next `OnRun()`
    clears the outputs if there are still no writers.
  - **Verify:** every CL checks. First used in S6.

- [x] **S6 (scene): Mappings register as output writers**
  - Depends on: D2.
  - A mapping that writes to its control holds a `ControlOutputHandle` while it
    is active, and releases it when it becomes inactive.
  - A mapping that can't write anything for its property (an action property,
    or no suitable output) is no longer treated as a write mapping, so it never
    keeps a control from being cleared.
  - **Verify:**
    - Every CL checks, paying attention to startup and track navigation, which
      activate and deactivate mappings.
    - Local-only check, not committed: a button that toggles the `TrackMode`
      view. Disabling it clears every strip (faders down, pot rings off, lights
      off, scribbles blank, meters off); enabling it restores them without
      flicker.

- [x] **P10 (plugin): Clear the surface on shutdown**
  - Depends on: S6.
  - When the control surface is destroyed, deactivate the scene and run the
    devices and MIDI output once more, so every mapped control is cleared
    instead of showing the last state.
  - **Verify:**
    - Every CL checks.
    - Close REAPER: faders drop, all lights and pot rings turn off, scribble
      strips and the timecode display go blank, and meters stop.
    - Remove the surface in Preferences > Control/OSC/web: the surface clears.
      Add it back: it initializes normally.
    - Both the X-Touch and the Extender clear. Without a wait after the final
      send, the Extender didn't: MIDI still being sent is lost when a port is
      destroyed, and its port is destroyed first. The destructor now waits
      100ms, as Klinke does.

- [x] **P3 (plugin): Switch mode views**
  - Depends on: P2, S6.
  - Add an empty `SendReceiveMode` view. Enable only the view for the current
    mode.
  - Track mode press returns via `EnsureTrackIsVisible` on the Send/Receive
    track.
  - If the Send/Receive track is deleted, return to Track mode with the track
    list where it was.
  - Log mode changes and how long they take. This is an infrequent event, so
    the log stays.
  - **Verify:**
    - Every CL checks, including the logged mode change times.
    - Enter Send/Receive mode: all strips go blank (faders down, pot rings off,
      scribbles blank, meters off, select/mute/solo/rec lights off). Master
      fader, transport, timecode, and modifiers still work.
    - Strip controls do nothing in Send/Receive mode (moving a fader or pressing
      mute doesn't change REAPER).
    - Bank/Channel/Global buttons do nothing in Send/Receive mode.
    - Press Track: all strips restore to the current REAPER state.
    - Return position: returning always shows the Send/Receive track among its
      siblings, scrolled into view, even if the surface showed other tracks
      before entering.
    - Measured: mode switches take ~50us, and steady state is unchanged.
    - Delete the Send/Receive track while in the mode: the surface returns to
      Track mode, showing the tracks it showed before entering.
    - Enter on a track inside a folder, then delete the whole folder: the
      surface returns to Track mode at the top level.
    - Hide the Send/Receive track in the MCP while in the mode: the mode stays.
    - Rapidly toggle modes several times: no stuck lights or motor fights.

### Phase 3: Send/Receive mode

- [x] **C3 (common): Route values and setters**
  - Depends on: C2.
  - `TrackRouteType` (send or receive), and `TrackRoute` volume, pan, and mute,
    read when the route lists are rebuilt.
  - `Track::RefreshRoutes()` polls volume, pan, and mute for each route.
  - Setters for volume, pan, and mute for sends and receives, using the
    `SetTrackSendUI*` family. REAPER's UI route functions index sends after
    hardware outputs, and receives as -1 - index when setting; this is kept in
    one place in `track.cc`.
  - `TrackListener::OnTrackRoutesChanged()` when routes are rebuilt with
    changes, or their values change.
  - `Track::OnRemoved()` resets a track that no longer exists in REAPER, instead
    of the TrackCache changing its members directly.
  - Verified with temporary logging and a button that changed the first route
    (removed before submitting): values match REAPER for sends and receives,
    changes go both ways, the right route changes with hardware outputs
    present, and mute changes can be undone (volume and pan changes can't; see
    C5). REAPER calls
    `SetTrackListChange()` when hardware outputs are added or removed, so the
    cached hardware output count stays current.
  - **Verify:** every CL checks. First used in P4.

- [x] **S2 (scene): Route properties**
  - Depends on: C3.
  - `RouteProperties`, analogous to `TrackProperties`, bound to
    (track, sends or receives, index): volume, pan, mute, exists.
  - The other track's name and color are not route properties. S3 gives each
    route view the other track as its track context, so the existing
    `TrackProperties` show them (and `View::SyncMappings()` keeps that track
    refreshed).
  - **Verify:** every CL checks. First used in P4.

- [x] **S3 (scene): Route child context**
  - Depends on: S2.
  - `ChildContextType::kSends` / `kReceives`. Child views get consecutive
    routes starting at the child context index, and resolve route properties.
    Each child view's track context is the track at the other end of its route
    (or the stub track past the end), so its name, color, and so on come from
    `TrackProperties`.
  - Max child context index and banking based on the route count.
  - The view with this context refreshes its track's routes during sync.
  - **Verify:** every CL checks. First used in P4.

- [x] **S4 (scene): Route view actions and properties**
  - Depends on: S3.
  - Route navigation action on a child view: points the parent view at the
    other track and flips between sends and receives.
  - Action to toggle between sends and receives, only if the track has both.
  - Text property for the current category ("Send" / "Recv").
  - **Verify:** every CL checks. First used in P4–P6.

- [x] **P4 (plugin): Route strips**
  - Depends on: P3, S4.
  - `Routes` view with fader, pot, mute, scribble, color, and select mappings.
  - Bank/Channel navigation over routes.
  - Entering shows sends, else receives.
  - Call `RefreshChildContext()` on the routes view when the track list changes
    (routes are only added or removed then), as `RefreshTrackViews()` does for
    the track list.
  - Route navigation (`parent_route_other_track`) changes the routes view's
    track without the plugin knowing, so use the routes view's track as the
    Send/Receive track rather than storing it separately.
  - **Verify:**
    - Every CL checks, including steady state performance in Send/Receive mode
      on a track with many sends.
    - Test project: track A with sends to B, C, and D; track E sending to A;
      track F with receives only; a track with 20+ sends.
    - Enter on A: strips show B, C, D with their names, send volumes, and
      track colors. Remaining route strips are blank.
    - Enter on F: strips show its receives.
    - Faders, pots, and mute lights match the send settings in REAPER's routing
      window.
    - Move a fader, turn a pot, press mute: REAPER's send volume, pan, and mute
      change to match. Each change can be undone in REAPER.
    - Change send volume, pan, and mute in REAPER: the surface follows.
    - Scribble bottom line updates while moving a fader.
    - Rec and Solo do nothing and stay unlit.
    - Navigation:
      - Press select on A's send to B: the surface shows B's receives, including
        A.
      - Press select on B's receive from A: the surface shows A's sends again.
      - Press select on E's send to A (enter on E): shows A's receives.
    - Banking on the track with 20+ sends: Bank and Channel Left/Right scroll,
      stopping at the first and last routes.
    - Add a send to the current track in REAPER: it appears. Delete a send: it
      disappears and later strips shift left.
    - Delete a destination track while its send is shown: the strips update.

- [x] **C5 (common): Undo for route volume and pan**
  - Depends on: C3 (found while testing P4).
  - `SetTrackSendUIVol` / `SetTrackSendUIPan` with `isend=0` don't create undo
    points. (Mute changes do, as `ToggleTrackSendUIMute` creates its own.)
    Klinke ends the edit with `isend=1` when the fader is released, but
    mappings don't know when an edit ends.
  - REAPER creates "(via surface)" undo points for track volume and pan, but
    not for routes: `CSurf_OnSendVolumeChange` and friends didn't create undo
    points either, even with `CSurf_FlushUndo`.
  - `ContinuousUndo` (common) creates an undo point with
    `Undo_OnStateChangeEx` once a series of changes with the same description
    has stopped for 500ms, or earlier when a change with a different
    description comes in. Route volume and pan use "JPR: Adjust send/receive
    volume/pan". `ControlSurface::Run()` calls `Update()` every run.
  - Route mute flushes pending changes first, so they aren't included in its
    own undo point.
  - Known limitation: other changes made within the 500ms that don't create
    their own undo point are included in the pending one.
  - **Verify:**
    - Every CL checks.
    - Change a send's and a receive's volume and pan from the surface, then
      undo in REAPER: each change is undone, and a continuous fader move
      doesn't create an undo point per step.
    - The right route still changes with hardware outputs present.

- [x] **P5 (plugin): Info strip**
  - Depends on: P4.
  - Extract the Track mode per-strip track mappings (fader, pot, mute, solo,
    rec arm, meter, name, color) into a helper shared with the Info strip.
  - Info strip mappings on the last X-Touch strip, using that helper. They go
    on the routes view itself: its track context is the Send/Receive track, and
    it has the `child_route_type_name` property.
  - Scribble shows the track name and "Send" / "Recv". Select is unmapped.
  - **Verify:**
    - Every CL checks.
    - Enter on a track: the last strip shows its name, "Send" or "Recv", and
      its color, and its meter moves during playback.
    - Info strip fader, pot, mute, solo, and rec arm control the current track
      exactly as in Track mode, including modifier behaviors.
    - Info strip select does nothing.
    - Route strips now number 15 (and banking pages by 15).
    - Navigate via select: the info strip updates to the new track and
      category.

- [x] **P6 (plugin): Toggle sends and receives**
  - Depends on: P4.
  - Pressing Send in Send/Receive mode toggles if the track has both.
  - **Verify:**
    - Every CL checks.
    - Track with sends and receives: pressing Send switches between them; the
      route strips and the info strip label update, and the bank resets to the
      start.
    - Track with only sends, or only receives: pressing Send does nothing.
    - After navigating to another track via select, toggling reflects that
      track's routes.

- [ ] **P7 (plugin): Follow REAPER's last touched track**
  - Depends on: P4.
  - In Send/Receive mode, `OnSetLastTouchedTrack` moves the view to that track
    (sends, else receives, else empty).
  - **Verify:**
    - Every CL checks.
    - In Send/Receive mode, click a track with sends in REAPER: the surface
      shows its sends.
    - Click a track with only receives: shows its receives.
    - Click a track with neither: route strips go blank, the info strip shows
      that track, and Send keeps blinking.
    - Moving route faders and pots on the surface does not move the view to
      another track. (Checks whether REAPER reports a last touched track when
      send values change.)
    - Changing the Info strip's fader does not change the view.
    - After navigating via select, the surface stays put until REAPER's last
      touched track changes.

### Phase 4: Holding Send in Track mode

- [ ] **S5 (scene): Same-track child context**
  - `ChildContextType::kSameTrack`: child views get this view's track.
  - **Verify:** every CL checks. First used in P8.

- [ ] **P8 (plugin): Split Track mode strips into sub-views** (no behavior
  change)
  - Depends on: S5.
  - Move the select mappings into a per-strip sub-view so a different select
    mapping can replace them while Send is held.
  - **Verify:**
    - Every CL checks.
    - Select buttons specifically: lights follow REAPER selection; press,
      double press into a folder, long press out; Shift and Ctrl selection
      behaviors; banking keeps select lights correct.

- [ ] **P9 (plugin): Hold Send to pick a track**
  - Depends on: P3, P8.
  - Send becomes a hold modifier. Its normal action (enter, or toggle sends and
    receives) moves to release, and only runs if the mode is unchanged since
    the press.
  - While held, select lights show tracks with sends or receives. Pressing a lit
    select enters Send/Receive mode for that track.
  - **Verify:**
    - Every CL checks.
    - In Track mode, hold Send: select lights switch to showing which tracks
      have sends or receives, regardless of the Send light's state.
    - Release Send without pressing select:
      - If Send was available, the surface enters Send/Receive mode on release
        (not on press).
      - If Send was unavailable, nothing happens and select lights return to
        showing REAPER selection.
    - Hold Send and press a lit select: the surface enters Send/Receive mode for
      that track immediately. Releasing Send afterwards does not toggle to
      receives.
    - Hold Send and press an unlit select: nothing happens.
    - Hold Send, bank left/right: the select lights update for the new bank.
    - Mute, solo, rec arm, and faders still work while Send is held.
    - In Send/Receive mode, Send toggles sends/receives on release, not press.
    - REAPER's track selection never changes from any of the above.
