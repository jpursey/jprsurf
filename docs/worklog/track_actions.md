# Track Actions in Scene

Moves the surface's track interaction policy out of `common` and into `scene`.
Today `Track::Ui*()` decides what each modifier means by reading the global
modifier state, and `TrackCache` holds the surface filter and the anchors for
ranged track actions. `common` is meant to be a REAPER data model that any
extension could use, so it should only offer the primitives, with explicit
inputs. The policy belongs in `scene`, where the filter and anchors are the
scene's own.

There is no change in behavior. The `track_ui_*` properties keep their names,
which *Property namespaces and names* settles later. Afterwards, *Ranges in
Send/Receive mode* is a `scene` change, and the actions are ready to be split
into separate mappings once *Modifiers a mapping ignores* exists.

## Design

### Decisions

- **One standard action per property.** Each `track_ui_*` property keeps
  today's behavior for every modifier, but the code is in `scene`, written as
  small named operations (toggle, toggle ungrouped, exclusive, clear all,
  range). Splitting them into mappings now would need about 8 mappings per
  button, and would change what unlisted modifier combinations do.
- **One enum for the on/off properties, in `common`.** `TrackBoolProperty`
  lets code that works with any of select, mute, solo, and rec arm (the
  policy's shared toggle and range code, and the anchors) say which, with
  `Track::Get()` and `TrackBatch::Set()`. `Track` keeps its explicit getters
  and setters, and the batch has explicit forwarders, for code that works with
  one property.
- **The last touched track stays in `TrackCache`**, as plain data: REAPER's
  notifications set it, and it is cleared when its track is deleted. `Track`
  stops setting it as a side effect. The scene's track actions set it whenever
  the surface touches a track, and read it as the root for Shift ranges.
- **The anchors move to `scene`.** They are surface state, so the scene owns
  one per `TrackBoolProperty`. *Properties declared on views, and track anchors* then
  builds its component on these.
- **The track filter moves to `scene`**, as the setting described in
  [config_model.md](../config_model.md) (Settings). It is fixed when the scene
  is created, so nothing has to react to it changing.

### Names

- `TrackBoolProperty` (common): `kSelected`, `kMute`, `kSolo`, and `kRecArm`,
  the on/off properties of a track. `Flag` and `Toggle` were rejected: a flag
  suggests one of a set of bits, and these are properties. It replaces
  `TrackAnchor` (in CL2), as the anchors are one per property.
- `TrackGrouping` (common): `kNone` changes only the track, and `kGrouped` also
  changes its grouped and ganged tracks, as REAPER's UI does.
- `TrackBatch` (common): changes to any number of tracks, made as one change.
- `TrackRange` (common): the tracks between two tracks under a filter. This
  is the renamed `SurfaceRange` from `track.cc`, since `common` no longer knows
  about a surface.
- `TrackActions` (scene): the standard behavior behind the `track_ui_*`
  properties. The ranged track actions doc already calls select, mute, solo,
  and rec arm "track actions". It shouldn't be confused with REAPER actions
  (commands), which the scene reaches as `cmd:` properties.

### common: Setters and TrackBatch

```
enum class TrackBoolProperty { kSelected, kMute, kSolo, kRecArm };
enum class TrackGrouping { kNone, kGrouped };

class Track {
  bool Get(TrackBoolProperty property) const;  // Beside GetMute(), and so on.

  // Each is one change, with its own undo point (except selection) and UI
  // refresh. These are not part of any TrackBatch that is alive at the time.
  // The explicit setters forward to Set().
  void Set(TrackBoolProperty property, bool value,
           TrackGrouping grouping = TrackGrouping::kNone);
  void SetSelected(bool selected);
  void SetMute(bool mute, TrackGrouping grouping = TrackGrouping::kNone);
  void SetSolo(bool solo, TrackGrouping grouping = TrackGrouping::kNone);
  void SetRecArm(bool rec_arm, TrackGrouping grouping = TrackGrouping::kNone);
  void SetVolume(double volume, TrackGrouping grouping = TrackGrouping::kNone);
  void SetPan(double pan, TrackGrouping grouping = TrackGrouping::kNone);

  // Selects this track, and unselects every other track, including the
  // master (REAPER's SetOnlyTrackSelected).
  void SelectOnly();
};

// Changes to any number of tracks, made as one change: REAPER refreshes its UI
// once, and one undo point is added when the batch ends, if anything changed.
class TrackBatch final {
 public:
  TrackBatch();
  ~TrackBatch();

  // Each sets the track's value if it exists and its value in REAPER is
  // different. The explicit setters forward to Set().
  void Set(Track* track, TrackBoolProperty property, bool value,
           TrackGrouping grouping = TrackGrouping::kNone);
  void SetSelected(Track* track, bool selected);
  void SetMute(Track* track, bool mute,
               TrackGrouping grouping = TrackGrouping::kNone);
  void SetSolo(Track* track, bool solo,
               TrackGrouping grouping = TrackGrouping::kNone);
  void SetRecArm(Track* track, bool rec_arm,
                 TrackGrouping grouping = TrackGrouping::kNone);
};

// The tracks between two tracks, in one filter's global index space.
struct TrackRange {
  // Returns nullopt if either end has no place in the filter.
  static std::optional<TrackRange> Between(const Track* from, const Track* to,
                                           TrackFilter filter,
                                           bool same_parent);
  bool Contains(const Track* track) const;
};
```

- **Undo:** the batch adds one undo point when it ends, if anything changed.
  One kind of change gets its own name ("JPR:Toggle Mute", and so on), a mix
  gets a generic one, and selection alone adds none, as today. Callers pass
  nothing. The goal is no empty undo points: a batch that sets nothing adds
  none, and the one case left that can still add one (Shift+Alt) is checked
  under To confirm.
- **UI refresh:** the batch holds a `PreventUIRefresh` scope for its lifetime.
  It replaces the private `ScopedPreventUiRefresh`.
- **Listeners:** notification stays per track and immediate. Each change
  updates that track's cached value and notifies its listeners, as a single
  setter does now. Tracks that REAPER changes through grouping aren't in the
  batch, and catch up as they do today, when a view refreshes the tracks it
  shows.
- **Single setters** are a batch of one, so the undo, refresh, and notification
  logic lives only in the batch.
- **Live values:** the batch compares against REAPER's value rather than the
  cache to decide whether anything changed, as the range code does today. The
  cache is only refreshed for tracks a view shows, so it can be stale. It reads
  all four flags with one `GetTrackState()` call per track, through the same
  table `Refresh()` uses, so the state bits live only in `track.cc`. If
  the live value differs from the cache, the batch updates the cache and
  notifies, even when it then has nothing to set, so any change it notices is
  notified. The single toggle setters get this too, which is slightly more
  correct than their cached check today. Volume and pan only change one track
  at a time, so they keep a cached check and aren't in the batch. `SetPan()`
  gains the check `UiPan()` lacks today, so the two are consistent.
- **Grouping:** a track whose own value already matches is skipped, even with
  `kGrouped`, so its grouped tracks aren't brought into line. Nothing today
  needs that: every grouped change is a toggle, or (Shift+Alt) follows clearing
  the track in the same batch, and REAPER's value reflects that at once.
  Grouping is ignored for selection, which REAPER doesn't group.
- **Order:** the batch ends its `PreventUIRefresh` scope before adding the undo
  point, as the code does today.
- `SelectOnly()` isn't a batch operation: it is REAPER's single call. It
  updates and notifies this track only, and the other tracks catch up when
  views refresh them, as today.
- None of these set the last touched track.
- Performance: the same REAPER calls as today, in the same `PreventUIRefresh`
  scopes.

**Brittleness:** the batch is RAII, so the `PreventUIRefresh` pair and the undo
point can't be forgotten or doubled. A `Track` setter called while a batch is
alive isn't part of it, and adds its own undo point. Joining the batch
automatically would hide global state, so both paths stay explicit, and the
comments say so. A caller could also make several single setter calls instead
of one batch, which is correct but slow. The CLAUDE.md performance rule already
covers that.

### scene: Scene filter and actions

```
class Scene {
  explicit Scene(std::string_view name,
                 TrackFilter track_filter = TrackFilter::kMcp);
  TrackFilter GetTrackFilter() const;  // From its TrackActions.
  TrackActions& GetTrackActions();
};
```

- The scene's `TrackActions` holds the filter, and `Scene::GetTrackFilter()`
  returns it, so there is one copy.
- `View` (child tracks, navigation) reads the filter from its scene, and
  `TrackProperties` (`track_is_folder`) from the `TrackActions` its view gives
  it, rather than from `TrackCache`.
- The filter is a constructor parameter, so it can't change under views that
  have already laid out their tracks.

### scene: TrackActions

```
class TrackActions final {
 public:
  explicit TrackActions(TrackFilter filter);
  TrackFilter GetTrackFilter() const;

  // The anchor for each property. While a property's anchor is held on one
  // track, its action on another track acts on the range between them.
  Anchor<Track>& GetAnchor(TrackBoolProperty property);

  // The standard behavior for each property, with the modifiers exactly as
  // documented on Track::Ui*() today.
  void UiSelect(Track* track);
  void UiToggle(Track* track, TrackBoolProperty property);  // Mute, solo, rec arm.
  void UiSetVolume(Track* track, double volume);
  void UiSetPan(Track* track, double pan);
};
```

- The `Ui` prefix marks the preset modifier behavior. Separate operations for
  each behavior could be added beside these later, without changing them.
- Owned by the `Scene`, so the anchors are released with the scene, and every
  surface has its own.
- `UiSelect()` and `UiToggle()` check the property's anchor first, then the
  modifiers in today's order. Ranges are private functions (`SelectRange()`
  and `SetRange()`), each one batch.
- It reads values only through `Track`'s getters, never REAPER's track state.
  A track a view shows was refreshed this run, so its cache is current. Before
  reading a track that may not be shown, it calls `Refresh()` on it:
  - The root of a range. For an anchor this is always on a strip, but the last
    touched track for Shift can be anywhere in the filter, including scrolled
    off the surface.
  - Opt's selected tracks, which can be anywhere. This is only the selected
    tracks, on one press.
- Where today's code clears every track and then sets one, it sets each track
  to its final value instead (Ctrl+Alt: this track on, every other track off),
  so a track isn't turned off and back on. Shift+Alt still clears every track,
  including this one, and then sets this one with grouping, as that is what
  turns its grouped tracks back on.
- It sets `TrackCache`'s last touched track exactly where `Track` does today,
  and nowhere else:
  - Select: when a plain press selects the track, when a plain press leaves it
    the only selected track, and when Ctrl selects it. Not when a track is
    unselected, and not for ranges.
  - Mute, solo, and rec arm: a plain press, Ctrl, Ctrl+Alt, Shift+Alt, and Opt
    only if the track is selected. Not for plain Alt, and not for ranges.
  - Volume and pan: never.
- The `track_ui_*` properties call it. `TrackProperties` gets the scene's
  `TrackActions` from its view.
- An anchor held on a track that no longer exists is treated as not held, as
  `TrackCache` clears it today, so the press falls through to the modifiers.
  The strip's view releases the hold itself (turning off its modifier, such as
  `mod_select_anchor`) when the track list refreshes and the strip shows
  another track.

**Brittleness:** the plugin must hold anchors from the same `TrackActions` that
acts on them. It gets them from the scene, so there is only one place to get
them from.

### Edge cases

Everything else is kept exactly as it is today:
- An anchor on another track takes precedence over every modifier, and an
  anchor on the same track is ignored, so the press behaves as usual.
- Ranges only include tracks with a place in the filter. Select ranges
  unselect every other track in the project, including hidden ones, but leave
  the master alone. Ranges ignore grouping, and don't set the last touched
  track.
- Alt clears every track in the project, including hidden ones, but not the
  master.
- Opt enumerates REAPER's selected tracks, which never include the master.
- A plain select press on a selected track, while other tracks are also
  selected, leaves it the only selected track, and unselects the master too
  (`SelectOnly()`).
- Selection never adds an undo point. Volume and pan add none either, and
  aren't batched.

These change, all in rare cases:
- Shift+Alt, when no track had the property on, sets it on this track but adds
  no undo point today (`this_track_changed = this_track_changed`). The batch
  adds one.
- Ctrl+Alt, when this track was already the only one on, adds an undo point
  today although nothing changed. With final values nothing is set, so no undo
  point is added.
- A press whose cached value went stale within the run (the property changed
  in REAPER since the view last refreshed) adds an empty undo point today. The
  batch sees that nothing changed, and adds none.
- Opt dereferences `TrackCache::GetTrack()` for each selected track without a
  null check. `TrackActions` skips a track the cache doesn't know yet.
- `UiPan()` has no "value unchanged" check, unlike `UiVolume()`, so pressing
  the pot button on a centered track still re-centers its grouped tracks. Pan
  gets the same check as volume. (Volume needs it, so motorized faders aren't
  disturbed; pan only lights LEDs, so the difference was never noticed.)

### To confirm

- **Confirmed (CL1):** REAPER drops an undo point when nothing actually
  changed. Shift+Alt, when only this track and its grouped tracks have the
  property on, clears them and sets them back, and REAPER adds no undo point,
  so the batch needs nothing more.
- **Confirmed (CL1):** `GetTrackState()` reflects a change made earlier in the
  same `PreventUIRefresh` scope. Shift+Alt on a muted, grouped track leaves the
  group muted.
- **Confirmed (CL1):** the batch notifying listeners of the other tracks it
  changes shows ranges correctly on the surface, with no flicker.
- **Found (CL1):** a track fader move followed by a mute is one undo point.
  REAPER creates the undo point for surface volume and pan changes itself, and
  folds the pending one into the next undo point it is given. This is not new
  (the old code added the mute's undo point the same way), and
  `ContinuousUndo::Flush()` doesn't help, as it only covers send and receive
  changes.
- **Found (CL2):** moving the master fader (on the surface or in REAPER) makes
  REAPER report the master as the last touched track. The master has no place
  in the track list, so Shift ranges do nothing until another track is
  touched. This is kept deliberately: the last touched track stays whatever
  REAPER says it is.
- The strip's view releases an anchor on a deleted track in the same run as
  the track list refresh. CL4 checks this, with the anchor's modifier.

## CLs

### CL1 [x] common: Grouping, TrackBatch, and TrackRange

Depends on: nothing.

- `track.h`: `TrackBoolProperty`, `TrackGrouping`, `TrackBatch`, `TrackRange`,
  `Track::Get()` and `SelectOnly()`, and the grouping parameter on `SetMute()`,
  `SetSolo()`, `SetRecArm()`, `SetVolume()`, and `SetPan()`.
- `TrackCache::GetSelectedTracks()`, for Opt.
- The single toggle setters become batches of one. `SetSelected()` keeps its
  last touched side effect until CL5, as the plain `track_selected` property
  still relies on it.
- `Track::Ui*()` rebuilt on the new setters, `TrackBatch`, and `TrackRange`,
  already the way `TrackActions` will do it (getters and `Refresh()` rather
  than `GetTrackState()`, final values, and the null check), so CL2 is a move.
  This is where the edge case changes above first appear. `DoUiProperty()`,
  `SelectRange()`, `SetPropertyRange()`, and `SurfaceRange` go away.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).
- Every modifier behavior documented on `Track::Ui*()`, for select, mute, solo,
  and rec arm: plain press, Ctrl, Shift, Ctrl+Shift, Alt, Ctrl+Alt, Shift+Alt,
  and Opt, including with grouped tracks.
- Ranged actions: hold select, mute, solo, and rec arm on one strip and press
  another. Each range is one undo point, and selection adds none.
- A Shift range from a last touched track that is scrolled off the surface,
  after changing its mute in REAPER: the range takes its current value.
- Shift+Alt with nothing muted adds an undo point, and Ctrl+Alt on the only
  muted track adds none. Shift+Alt when only this track and its group are
  muted: record whether REAPER adds an undo point (see To confirm).
- Pressing the pot button on a centered track does nothing, and on an
  off-center track centers it and its grouped tracks.
- The last touched track after each behavior matches the list in
  TrackActions, checked with a Shift range afterwards.
- Ctrl on a fader and pan pot ignores grouping, and without it grouped tracks
  follow.
- Performance: on a 150-track project, a single mute press and a 150-track
  mute and rec arm range take the same time as before (see
  [ranged_track_actions.md](ranged_track_actions.md), Performance).

### CL2 [x] scene: TrackActions

Depends on: CL1.

- `TrackActions`, with the behavior moved from `Track::Ui*()`, and
  `Scene::GetTrackFilter()` and `GetTrackActions()`. For now the scene builds
  its `TrackActions` from `TrackCache::GetSurfaceFilter()`, and
  `TrackActions::GetAnchor()` returns `TrackCache`'s anchor, as the plugin holds
  those until CL3.
- `TrackAnchor` is removed: `TrackCache::GetAnchor()` and the plugin's
  `AddTrackAnchorMapping()` take a `TrackBoolProperty` (a rename in `common`
  and `plugin`).
- The `track_ui_*` properties call `TrackActions`, which `TrackProperties`
  gets from its view. The four toggle property classes become one.
- `View` reads the scene's filter, and `TrackProperties` its `TrackActions`'.
- `Track::Ui*()` is unused, and removed in CL5.

**Verify**
- Standard checks.
- Every check from CL1: modifiers, ranges, undo points, and grouping.
- The master fader (plain `track_volume`) still ignores grouping.
- Performance: as CL1.

### CL3 [x] plugin: Anchors and filter from the scene

Depends on: CL2.

- `AddTrackAnchorMapping()` gets its anchor from
  `scene->GetTrackActions().GetAnchor()`.
- `CanShowRoutes()`, `OnLastTouchedTrackChanged()`, and
  `EnsureTrackIsVisible()` use `scene->GetTrackFilter()`.

**Verify**
- Standard checks.
- Ranged select, mute, solo, and rec arm, and releasing each anchor by
  releasing the button, by banking, and by a mode change.
- Send/Receive mode follows the last touched track, and Track mode reveals it.

### CL4 [x] scene: The scene owns its filter and anchors

Depends on: CL3.

- `Scene`'s constructor takes the track filter for its `TrackActions`, which
  holds its own anchors. `TrackCache`'s filter and anchors are now unused.

**Verify**
- Standard checks.
- Ranged actions, and releasing anchors, as CL3.
- Delete a track while its anchor is held (with an action from REAPER's
  keyboard, as the button is held): pressing the same button on another strip
  acts normally rather than as a range, and a select anchor's
  `mod_select_anchor` turns off.

### CL5 [x] common: Remove the policy from common

Depends on: CL4.

- Remove `Track::Ui*()`, and `SetSelected()`'s last touched side effect.
- Remove `TrackCache::GetSurfaceFilter()`, `SetSurfaceFilter()`, and the
  anchors, and update comments that refer to them
  (`GetOnlySelectedTrack()`, `anchor.h`).
- `common` no longer uses the modifier state outside `anchor` and `modifiers`
  itself.
- Docs: [config_model.md](../config_model.md) (the track filter setting), and
  the Structure section of
  [ranged_track_actions.md](ranged_track_actions.md).

**Verify**
- Standard checks.
- The full modifier and range checks from CL1, once more.
- `grep` finds no `AreModifiersOn` in `common` outside `modifiers` and
  `anchor`.
