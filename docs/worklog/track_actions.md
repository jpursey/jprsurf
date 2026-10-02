# Track Actions in Scene

The surface's track interaction policy lives in `scene`, in each scene's
`TrackActions`. This covers what each modifier means for select, mute, solo, rec
arm, volume, and pan, and also the anchors for ranged actions and the track
filter. `common` offers only primitives with explicit inputs: grouping as a
parameter, `TrackBatch` for changing several tracks as one change, and
`TrackRange`. Outside `Anchor`, it no longer reads the modifier state.

Before this, `Track::Ui*()` decided what each modifier meant by reading the
global modifier state, and `TrackCache` held the surface filter and the anchors.
`common` is meant to be a REAPER data model that any extension could use, so the
policy moved out of it.

## Behavior

The surface behaves as it did, except in these rare cases:
- Shift+Alt, when no track had the property on, now adds an undo point.
- Ctrl+Alt, when this track was already the only one on, no longer adds an empty
  undo point. Ctrl+Alt now sets each track straight to its final value, so this
  track isn't turned off and back on.
- A press whose cached value went stale within the run (the property changed in
  REAPER since the view last refreshed) no longer adds an empty undo point.
- Opt skips a selected track that `TrackCache` doesn't know yet. Previously it
  dereferenced null.
- Pressing the pot button on a centered track no longer re-centers its grouped
  tracks: pan skips an unchanged value, as volume does.
- Writing the plain `track:selected` property no longer sets the last touched
  track. Nothing maps it today. `track:ui_selected` still sets it, as before.

## REAPER facts

- REAPER drops an undo point when nothing actually changed. For example,
  Shift+Alt, when only this track and its grouped tracks have the property on,
  clears them and sets them back, and REAPER adds no undo point.
- `GetTrackState()` reflects a change made earlier in the same
  `PreventUIRefresh` scope.
- A track fader move followed by a mute is one undo point, and so is one
  followed by a send fader (seen again in *Fake REAPER*'s CL1). REAPER creates
  the undo point for a surface volume or pan change itself, and folds the
  pending one into the next undo point it is given. `ContinuousUndo::Flush()`
  doesn't help, as it only covers send and receive changes. This was accepted,
  as it only happens when working quickly (see the backlog's *Separate undo
  points for track faders*).
- Moving the master fader (on the surface or in REAPER) makes REAPER report the
  master as the last touched track. The master has no place in the track list,
  so Shift ranges do nothing until another track is touched. This is kept
  deliberately: the last touched track is always whatever REAPER says it is.

## Structure

### common: Track primitives (track.h/.cc, track_cache.h/.cc)

- `TrackBoolProperty` (`kSelected`, `kMute`, `kSolo`, `kRecArm`) is for code
  that works with any of the on/off properties. `kTrackBoolProperties` lists
  them all.
  - `Track::Get()` and `Set()` take one. The explicit getters and setters
    (`GetMute()`, `SetMute()`, and so on) stay, for code that works with one
    property.
- `TrackGrouping` (`kNone`, `kGrouped`) says whether a change also applies to
  the track's grouped and ganged tracks, as REAPER's UI does.
  - Every setter takes one, defaulting to `kNone`. REAPER doesn't group
    selection.
  - It maps to REAPER's `ingroupflags` for the toggles, and `allowgang` for
    volume and pan.
- **Single setters.** `Track::Set()` and its forwarders each make their own
  change.
  - Each is a batch of one, with its own undo point (except selection) and UI
    refresh.
  - They are not part of any `TrackBatch` that is alive at the time.
  - `SetVolume()` and `SetPan()` skip a value equal to the cached one, and
    aren't batched.
  - `SelectOnly()` is REAPER's `SetOnlyTrackSelected()`. It updates and notifies
    this track only, and the other tracks catch up when views refresh them.
- **`TrackBatch`** is RAII, and holds a `PreventUIRefresh` scope for its
  lifetime.
  - Each `Set()` compares against REAPER's live value (one `GetTrackState()`
    reads all four), since the cache is only current for tracks a view shows.
  - A track that already has the value is skipped, even with `kGrouped`, so its
    grouped tracks aren't brought into line.
  - Either way, it brings the cache into line and notifies that track's
    listeners immediately.
  - Before its first change that creates an undo point, it calls
    `ContinuousUndo::Flush()`, so the batch's undo point doesn't absorb pending
    send or receive changes.
  - It ends the `PreventUIRefresh` scope first, and then adds one undo point if
    anything changed:
    - One kind of change gets its own name, such as "JPR:Toggle Mute".
    - A mix gets "JPR:Change Tracks".
    - Selection alone adds none.
- **The property table.** A private `Track::BoolPropertyInfo` table has, for
  each property, its state bit, cached member, REAPER setter, and undo name.
  `DoRefresh()` and `TrackBatch` share it, so the state bits live only in
  `track.cc`.
- **`TrackRange::Between(from, to, filter, same_parent)`** gives the tracks
  between two tracks in one filter's global index space.
  - It returns nullopt if either end has no place in the filter.
  - `same_parent` limits the range to tracks that share `from`'s parent.
  - `Contains()` tests a track.
- **`TrackCache`:**
  - `GetSelectedTracks()` returns REAPER's selected tracks, not including the
    master.
  - The last touched track is plain data. REAPER's notifications set it,
    callers may set it, and it is null once its track is deleted. `Track` never
    sets it as a side effect.
  - `TrackCache` holds no filter and no anchors.

**Brittleness:** a `Track` setter called while a batch is alive isn't part of
the batch, and adds its own undo point. Joining the batch automatically would
hide global state, so both paths stay explicit. A caller could also make several
single setter calls instead of one batch, which is correct but slow. The
CLAUDE.md performance rule covers that.

### scene: Scene and TrackActions (scene.h/.cc, track_actions.h/.cc)

- `Scene(name, track_filter = TrackFilter::kMcp)`.
  - The filter is fixed when the scene is created, so nothing has to react to
    it changing.
  - The scene's `TrackActions` holds the one copy, and
    `Scene::GetTrackFilter()` returns it.
  - `GetTrackActions()` gives the actions to views and the plugin.
- `View` reads the filter from its scene. `TrackProperties` gets its
  `TrackActions` from its view, and `track:is_folder` uses that filter.
- **`TrackActions`:**
  - `GetAnchor(TrackBoolProperty)` returns the anchor for select, mute, solo, or
    rec arm.
  - `UiSelect()`, `UiToggle()` (mute, solo, and rec arm), `UiSetVolume()`, and
    `UiSetPan()` run the standard behavior. The class comment documents every
    modifier.
  - The `Ui` prefix marks this preset modifier behavior. Separate operations for
    each behavior can be added beside these later, without changing them.
  - `UiSelect()` and `UiToggle()` check the property's anchor first, then the
    modifiers.
  - Ranges are private (`SelectRange()` and `SetRange()`), each one batch over
    a `TrackRange` in the scene's filter.
- **Reads.** `TrackActions` reads values only through `Track`'s getters, never
  REAPER's track state. A track a view shows was refreshed this run. It calls
  `Refresh()` on the two kinds of track that may not be shown:
  - The root of a range, since the last touched track can be scrolled off the
    surface.
  - Opt's selected tracks.
- **Final values.** Ctrl+Alt sets each track straight to its final value.
  Shift+Alt still clears every track, including this one, and then sets this one
  with grouping, since that is what turns its grouped tracks back on.
- **The last touched track.** `TrackActions` sets `TrackCache`'s last touched
  track:
  - Select: when a plain press selects the track or leaves it the only selected
    track, and when Ctrl selects it.
  - Mute, solo, and rec arm: a plain press, Ctrl, Ctrl+Alt, Shift+Alt, and Opt
    if the track is selected.
  - Never for ranges, plain Alt, unselecting, volume, or pan.
- **Anchors on deleted tracks.** An anchor held on a track that no longer exists
  is treated as not held, so the press falls through to the modifiers. The
  strip's view releases the hold itself (turning off its modifier, such as
  `mod:select_anchor`) when the track list refreshes and the strip shows another
  track.
- **`track:ui_*` properties.** These call `TrackActions`, and the plain
  `track:*` properties call `Track`'s setters directly.
  - One class covers select, mute, solo, and rec arm (`TrackBoolViewProperty`),
    and `TrackPanProperty` and `TrackVolumeProperty` handle both forms.

**Brittleness:** the plugin must hold anchors from the same `TrackActions` that
acts on them. It gets them from the scene, so there is only one place to get
them from.

### plugin: PluginSurface (plugin_surface.cc)

- `AddTrackAnchorMapping()` takes a `TrackBoolProperty`, and gets its anchor
  from `scene->GetTrackActions()`.
- `CanShowRoutes()`, `OnLastTouchedTrackChanged()`, and `EnsureTrackIsVisible()`
  use the scene's track filter.

## Building blocks

- **`TrackBatch` (common/track.h):** change any number of tracks as one change,
  with one UI refresh and at most one undo point. Any change to more than one
  track should use one.
- **`TrackRange` (common/track.h):** the tracks between two tracks under a
  filter, optionally limited to one parent.
- **`TrackGrouping` (common/track.h):** grouping and ganging as an explicit
  parameter on every setter.
- **`TrackActions` (scene/track_actions.h):** the standard modifier behavior for
  a surface's track controls, with its anchors and filter. When *Modifiers a
  mapping ignores* exists, the individual behaviors can be split into separate
  operations here and mapped one by one.

## Performance

On a 150-track project, a single mute press takes ~3.7ms. The 150-track mute and
rec arm ranges are on par with, or better than, before (see
[ranged_track_actions.md](ranged_track_actions.md), Performance). The steady
state `Run()` averages ~24–28µs. `TrackActions` makes the same REAPER calls as
the code it replaced, in the same `PreventUIRefresh` scopes.
