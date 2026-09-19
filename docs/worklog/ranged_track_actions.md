# Ranged Track Actions

Hold the select, mute, solo, or rec arm button on one track and press the same
button on another track to act on every track between them. Select selects the
range. Mute, solo, and rec arm set the range to the held track's value.

This works alongside Shift, which does the same thing using REAPER's last
touched track. Shift is still useful when both ends of the range are not on the
surface at once.

## Behavior

- **Anchor**: The held track is the "anchor" for its action (select, mute,
  solo, rec arm). Each action has its own anchor, so holding mute on one track
  does not affect select. A strip holds only one anchor at a time, so holding a
  second button on the same strip replaces the first button's anchor.
- **Setting the anchor**:
  - Mute, solo, rec arm: when the button goes down. The press still does its
    normal behavior (toggle, set last touched track).
  - Select: after a long press (350ms). The long press does the normal single
    press behavior (select only this track, or unselect it if it is the only
    selected track) and sets the last touched track. Pressing another select
    button before then is just a normal press of that button; pressing it
    again (while still holding the first) makes the range.
  - An empty strip (no track) never anchors.
- **Ranged press**: While an anchor is held, pressing the same button on a
  different track acts on the range between the two:
  - Select: selects exactly the tracks in the range, and unselects all others.
    It acts on press, and never becomes a double press into a folder.
  - Mute, solo, rec arm: sets every track in the range to the anchor track's
    current value, ignoring grouping and ganging.
  - All modifiers are ignored: an anchor on another track takes precedence over
    every other behavior.
  - The range includes only tracks with the same parent as the anchor (the same
    as Shift without Ctrl), so an expanded folder between the two ends doesn't
    pull in its children.
  - The ranged press does not change the last touched track, the same as Shift.
  - The anchor stays held, so pressing further buttons redoes the range from the
    anchor.
  - The whole range is one undo point.
- **Releasing the anchor**: The anchor is released when the button is released,
  when the strip's view changes context (its track or route changes, such as
  from bank or channel navigation), when the view is deactivated (such as a
  mode change), or when the track is deleted. A later release of the button
  then does nothing.
- **Track mode only**: Send/Receive mode (including its Info strip) has no
  anchors.
- **Global**: Pressing Global goes up one level, centered on the folder that was
  left. Holding it (long press) goes to the root. Select no longer uses long
  press to go to the parent track. Double pressing select still goes into a
  folder.

## Structure

Each layer only knows what it needs to:

- **common**: `Anchor`, `TrackCache`'s anchors, and the ranged behavior in
  `Track`. Knows nothing about controls or views.
- **device**: `Control::IsPressed()` for long press registrations. Knows nothing
  about anchors.
- **scene**: a view holds one anonymous anchor, and `CallbackToggleProperty`.
  Knows nothing about select, mute, solo, or rec arm.
- **plugin**: the X-Touch mappings that tie these together.

### common: Track actions

- `TrackCache::GetAnchor(TrackAnchor)` returns the `Anchor<Track>` for
  `kSelect`, `kMute`, `kSolo`, or `kRecArm`. `TrackCache::Refresh()` clears an
  anchor held on a removed track, the same as the last touched track.
- `Track::UiSelected()` and `DoUiProperty()` (behind `UiMute()`, `UiSolo()`,
  and `UiRecArm()`) check their anchor first. If it is held on a different
  track, they act on the range and ignore modifiers. Otherwise Shift does the
  same using the last touched track (with Ctrl widening it past the parent).
  Both paths share `SelectRange()` and `SetPropertyRange()`, which use
  `GetSurfaceRange()` to find the tracks between the two ends in the surface
  filter.
- Multi-track changes (ranges, and the Alt and Option behaviors) are wrapped in
  a `ScopedPreventUiRefresh`, so REAPER pays its UI refresh once rather than per
  track. See Performance.

### device: long press IsPressed

- `press_release` mappings read `Control::IsPressed(id)`. A long press
  registration reports pressed from when the long press fires until the button
  is released, so `press_release` with `kLongPress` reads as "held past a long
  press".

### scene: View anchor and CallbackToggleProperty

- `View::SetAnchor(AnchorHold)` replaces (and so releases) any anchor the view
  holds. An empty hold is ignored, and an inactive view drops the hold, so an
  inactive view never holds an anchor.
- `View::ReleaseAnchor(const AnchorBase*)` releases only if the view's hold is
  on that anchor, so releasing a button whose anchor was already replaced does
  not release the anchor that replaced it. `ClearAnchor()` releases whatever is
  held.
- The view releases its anchor in `SetTrack()` and `SetRoute()` when the track
  or route actually changes (a refresh to the same one doesn't count), and when
  it is deactivated. `View::SetRoute()` sets the route and the track at its
  other end together.
- The view, not the track, owns the hold, because the release comes from the
  same button after its view may show a different track.
- `CallbackToggleProperty` calls a callback with every value written to it. It
  holds no value (it always reads false), so a lost release can't leave it
  stuck; callbacks must tolerate a repeated value.

### plugin: mappings

- `AddTrackAnchorMapping()` adds a per-strip `anchor_<action>_<n>`
  `CallbackToggleProperty` mapped with `press_release`. True holds the action's
  `TrackCache` anchor on the view's track (unless the strip is empty) and gives
  it to the view; false releases it from the view.
- Select, per Track mode strip:
  - Press: `kUiSelected`. Double press: `kParentTrackChild`.
  - Long press: `kUiSelected` and `anchor_select_<n>`. The select anchor's hold
    turns on `mod_select_anchor`.
  - `kUiSelected` requiring `mod_select_anchor`. While a select anchor is held,
    other select buttons are in a press group with no double press, so the
    ranged press acts immediately. This is the same approach as holding Send
    (`pick_send_receive_track_<n>` requires `mod_send_hold`).
- Mute, solo, rec arm, per Track mode strip: `anchor_<action>_<n>` alongside the
  existing mappings. They have no long or double press, so need no modifier.
- Global: `kTrackParent` on press, `kTrackRoot` on long press.
  `View::kTrackParent` centers the child context on the track that was left,
  sharing code with `kParentTrackParent`.

## Building blocks

- **`Anchor<T>` / `AnchorHold` (common/anchor.h)**: a generic RAII reference to
  a held object.
  - `Anchor<T>` is a slot holding at most one `T*`. `Hold(T*, modifier)` returns
    a move-only `AnchorHold`, or an empty one if the slot is already held (the
    first holder wins). Destroying or resetting the hold releases the slot.
  - `AnchorHold` is not a template, so owners like `View` can store a hold
    without knowing what it anchors. The untyped logic lives in `AnchorBase`.
  - The slot and its hold point at each other. `Clear()` (or destroying the
    slot) empties the hold, so they may be destroyed in any order.
  - The optional modifier bits are on exactly while the hold holds the anchor.
    They belong to the hold rather than the slot, because slots can be global
    (`TrackCache`) while modifier bits are allocated by a surface's scene.
  - Unit tested in `jpr_common_test`.
- **`CallbackToggleProperty` (scene/value_property.h)**: map a control's press
  and release to code.
- **`View` anchor**: tie any hold to a view's context, so it is released when
  the view moves on.
- **Long press `IsPressed`**: act for as long as a button is held past a long
  press.
- **`ScopedPreventUiRefresh` (common/track.cc)**: batch REAPER UI changes to
  more than one track.

## Performance

REAPER's track setters (`SetTrackUIMute`, `SetTrackUISolo`, `SetTrackUIRecArm`,
and the alternatives) each pay a UI refresh: ~2–9ms for mute and solo, and
9–17ms for rec arm. Unbatched, a 15-track range took 70–80ms (mute/solo) or
~205ms (rec arm). With `PreventUIRefresh` around the whole change, on a
150-track project:
- A single mute or solo press: ~4ms. A 150-track range: ~7–8ms.
- Rec arm: ~15ms for one track, ~39ms for 150. The first rec arm of a session
  can take ~400ms once (REAPER setting up inputs).
- `Undo_OnStateChangeEx` adds ~1.2ms per change.

This is REAPER's own UI work on the UI thread, and doesn't affect realtime
audio. The anchor mappings add nothing measurable to the steady state `Run()`.

## Future ideas

- Ranges in Send/Receive mode (route mute, and selecting routes).
- Move the empty strip check into `common`, so any holder of a track anchor
  gets it (for instance, `Track` falling back to a normal press when the range
  is empty).
- Holding Send and a select anchor at the same time leaves select buttons with
  no matching press group, so select does nothing. Unlikely in practice.
- Read-only toggle mappings still register for property change notices, so
  they call `WriteControl()` (which does nothing) on every change. Registering
  only when the mapping writes the control would avoid it.
