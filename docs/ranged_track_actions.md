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
- **Setting the anchor**: This happens when the button is held.
  - Mute, solo, rec arm: when the button goes down. The press does its normal
    single press behavior (toggle, set last touched track) as it does today.
  - Select: after a long press (350ms). The long press does the normal single
    press behavior (select only this track, or unselect it if it is the only
    selected track) and sets the last touched track. Holding the button short
    of a long press is not an anchor, so pressing another select button too
    soon is just a normal press of that button. Pressing it again (while still
    holding the first) makes the range.
- **Ranged press**: While an anchor is held, pressing the same button on a
  different track acts on the range between the two:
  - Select: selects exactly the tracks in the range, and unselects all others.
  - Mute, solo, rec arm: sets every track in the range to the anchor track's
    current value, ignoring grouping and ganging.
  - All modifiers are ignored: an anchor on another track takes precedence over
    every other behavior.
  - The range includes only tracks with the same parent as the anchor (the
    same as Shift without Ctrl). Otherwise an expanded folder between the two
    ends would pull in its children.
  - The ranged press does not change the last touched track, the same as Shift.
  - The anchor stays held, so pressing further buttons redoes the range from the
    anchor.
- **Releasing the anchor**: The anchor is released when the button is released,
  when the anchor's view changes context (its track or route changes, such as
  from bank or channel navigation), when the view is deactivated (such as a
  mode change), or when the track is deleted. A later release of the button
  then does nothing.
- **Global**: Pressing Global goes up one level, centered on the folder that was
  left. Holding it (long press) goes to the root, which is what a press does
  today. Select no longer uses long press to go to the parent track. Double
  pressing select still goes into a folder.
- **Out of scope**: Send/Receive mode, where select navigates across the route
  and mute is the route's mute.

## Design

### device: long press IsPressed

`press_release` is a scene concept: the mapping reads `Control::IsPressed(id)`
each frame. `Control` only maintains `is_pressed` for normal registrations in a
press group that has no long or double press, so a select button (which has
both) never reports a press.

A long press registration will report pressed from when the long press fires
until the button is released:
- `DeliverLongPress()` sets `is_pressed` for the group's long press
  registrations.
- On release, the group's long press registrations are cleared, even though the
  group is already idle after the long press fired (today that release is
  ignored).

A scene mapping with `press_release` and `kLongPress` then reads as "held past a
long press", with no scene changes. Nothing that uses long press today reads
`IsPressed()`, so existing behavior is unchanged.

### common: Anchor

A generic RAII anchor, not specific to tracks:
- `Anchor<T>` is a slot holding at most one `T*`. `Hold(T*)` returns a
  move-only `AnchorHold` if the slot is empty (and an empty hold otherwise, so
  the first holder keeps the anchor). Destroying or resetting the hold clears
  the slot.
- `AnchorHold` is not a template, so owners like `View` can store holds without
  knowing what is anchored. The untyped state and logic (held pointer, modifier
  bit, release, clear) live in a non-template `AnchorBase`, which the hold
  points to. `Anchor<T>` is a thin typed wrapper over it that only adds
  `Hold(T*)` and `T* Get()`.
- A slot has at most one live hold, and they point at each other. `Clear()`
  (or destroying the slot) empties the hold, so a hold never refers to a slot it
  no longer holds, and they may be destroyed in any order. Clearing is how an
  anchor on a deleted track is dropped.
- A slot may optionally be given a modifier bit that is on exactly while the
  slot is held. This keeps the modifier in sync with the anchor in every case
  (release, clear, destruction) without any caller having to remember it.

`TrackCache` owns one `Anchor<Track>` per `TrackAnchor` (`kSelect`, `kMute`,
`kSolo`, `kRecArm`), and clears a track's anchors when it is removed, the same as
the last touched track.

`Track::UiSelected()` and `DoUiProperty()` first check the anchor for their
action. If it is held by a different track, they do the ranged behavior using it
and ignore modifiers. The existing Shift range code is shared by both paths, and
only differs in where the anchor comes from and whether Ctrl widens the range.

### scene: View anchor

A view holds a single anonymous `AnchorHold`, and knows nothing about what it
anchors or why:
- `SetAnchor(AnchorHold)` replaces (and so releases) any anchor the view already
  holds. Only one anchor is needed at a time, so starting an anchor of another
  type replaces the last one.
- `ReleaseAnchor(const AnchorBase&)` releases the view's anchor only if it is
  held on that slot. That way releasing a button whose anchor was already
  replaced does not release the anchor that replaced it.
- The view releases its anchor whenever its context actually changes (track or
  route; a refresh that leaves them the same does not count) or it is
  deactivated.

The view, not the track, owns the hold. This matters because the release comes
from the same button after its view may show a different track.

A new generic `CallbackToggleProperty` (next to `CallbackActionProperty`) calls
a callback with the written value, so the plugin can map press and release to
code.

`View::kTrackParent` (currently unused) moves up one level, centered on the
track that was left, sharing code with `kParentTrackParent`.

### plugin: mappings

- Global: `kTrackParent` on press, `kTrackRoot` on long press.
- Anchor properties: for each track strip and action, a
  `CallbackToggleProperty` (`anchor_<action>_<n>`, like the existing
  `pick_send_receive_track_<n>`). True calls `SetAnchor()` on the strip's view
  with a hold of that action's `TrackCache` anchor on the view's track, unless
  the strip is empty (the stub track has no place in a range, so an anchor on
  it would make the other track's press do nothing). False calls
  `ReleaseAnchor()` with that anchor slot.
- Select, per track strip:
  - Remove the long press to `kParentTrackParent`.
  - Long press: `kUiSelected` (normal select behavior) and
    `anchor_select_<n>` with `press_release`.
  - `kUiSelected` requiring the `mod_select_anchor` modifier (set by the select
    anchor slot). This puts other select buttons in a press group without a
    double press while an anchor is held, so the ranged press acts immediately
    rather than after release, and cannot become a double press into a folder.
    This is the same approach as holding Send.
- Mute, solo, rec arm, per track strip: `anchor_<action>_<n>` with
  `press_release`. These buttons have no long or double press, so they need no
  modifier.

## CLs

### CL1 [x] scene: kTrackParent centers on the track that was left

Depends on: none.

- `View::kTrackParent` goes to the parent track with the child context index
  centered on the track it came from, sharing code with `kParentTrackParent`.
- It is unused, so there is no visible change yet.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).
- Covered by CL2.

### CL2 [x] plugin: Global goes up one level, long press to root

Depends on: CL1.

- Global: `kTrackParent` on press, `kTrackRoot` on long press.
- Remove the select long press to `kParentTrackParent`.

**Verify**
- Standard checks.
- Inside a nested folder, pressing Global goes up one level, centered on the
  folder that was left (also for a folder near the start and end of its
  siblings). At the top level it does nothing.
- Holding Global goes to the root from any level.
- Quick press and release of Global responds promptly.
- Holding select no longer navigates. Double press still goes into a folder.
  A single press still selects.

### CL3 [x] device: IsPressed for long press registrations

Depends on: none.

- Long press registrations report pressed from when the long press fires until
  release.

**Verify**
- Standard checks.
- Temporary: a `press_release` + `kLongPress` mapping on a select button to a
  logged toggle property. Holding logs true after ~350ms and false on release. A
  short press logs nothing and still selects. Double press still works. Remove
  before commit.

### CL4 [x] common: Anchor

Depends on: none.

- `AnchorBase`, `Anchor<T>`, and `AnchorHold` (new `common/anchor.h` and
  `anchor.cc`), with the optional modifier bit.

**Verify**
- Standard checks.
- `jpr_common_test` passes: hold, first holder wins, release, clear and
  destroying the anchor emptying the hold, an old hold not releasing a new one,
  move, and the modifier bit.

### CL5 [x] common: TrackCache anchors

Depends on: CL4.

- `TrackAnchor` enum and per-action `Anchor<Track>` in `TrackCache`, cleared
  when a track is removed.

**Verify**
- Standard checks.

### CL6 [x] common: Track Ui* actions use the anchor

Depends on: CL5.

- `UiSelected()` and `DoUiProperty()` do the ranged behavior when an anchor is
  held by a different track, ignoring modifiers.
- Shift and anchor ranges share the range code.
- Update the modifier behavior comment in `track.h`.

**Verify**
- Standard checks, including Shift ranges (with and without Ctrl) for select,
  mute, solo, rec arm, which are unchanged.
- Covered by CL10.

### CL7 [x] common: Batch REAPER's UI refresh in multi-track Track actions

Depends on: CL6.

Each REAPER track setter call pays a UI refresh of ~2ms for mute/solo (often
~9ms) and 9–17ms for rec arm, so a 15-track range took 70–80ms, or ~205ms for
rec arm. Wrapping a whole range in one `PreventUIRefresh` scope makes each
call ~1µs and pays the refresh once: ~2.5ms, or 9–16ms for rec arm.

- A small RAII helper for `PreventUIRefresh(1)` / `PreventUIRefresh(-1)`, local
  to `track.cc` for now.
- Used by every Track UI action that changes more than one track:
  `SelectRange()`, `SetPropertyRange()`, the Alt "clear all" loop, and the
  Option "toggle selected tracks" loop.

**Verify**
- Standard checks.
- Shift ranges, Alt, Shift+Alt, Ctrl+Alt, and Option on mute, solo, rec arm,
  and Shift ranges on select, still behave the same, and REAPER's UI shows the
  result right away.
- Undo and redo of each of those restore and reapply the whole change.
- Performance: a 15-track Shift range of mute/solo is ~2.5ms (plus ~1.2ms for
  undo) rather than 70–80ms, and rec arm is under ~20ms rather than ~205ms. Alt
  on a 100+ track project stays in the low milliseconds (rec arm aside).

### CL8 [x] scene: View anchor

Depends on: CL4.

- `View::SetAnchor()`, `View::ReleaseAnchor()`, and `View::ClearAnchor()`,
  holding a single `AnchorHold` that is released when the view's context
  changes or it is deactivated.
- `View::SetRoute()` sets the route and the track at its other end together.

**Verify**
- Standard checks, including Send/Receive mode (scrolling routes, toggling
  sends and receives, and navigating across a route).
- Covered by CL10.

### CL9 [ ] scene: CallbackToggleProperty

Depends on: none.

- `CallbackToggleProperty`, which calls a callback with each value written to
  it.

**Verify**
- Standard checks.
- Covered by CL10.

### CL10 [ ] plugin: Hold to act on a range of tracks

Depends on: CL2, CL3, CL6, CL7, CL8, CL9.

- `anchor_<action>_<n>` properties and mappings for select, mute, solo, rec
  arm, and the `mod_select_anchor` modifier for the select anchor slot.
- Update the README changelog.

**Verify**
- Standard checks.
- Select: hold one track's select (it becomes the only selected track), press
  another: exactly the range is selected. Works in both directions, across the
  X-Touch and extender, and with Shift/Ctrl/Alt/Option held (ignored). Press a
  third track while still holding: the range is redone. Holding past 350ms
  selects the track at that point (the same as a single press), and releasing
  with no other press leaves it that way.
- Select timing: press the second button quickly and release both quickly: the
  range is selected, and not replaced by either track's own select. A double
  press of the second button while holding does not navigate.
- Select too early: press the second button before 350ms: normal select of that
  track. Press it again: the range.
- Mute, solo, rec arm: hold one (it toggles), press another: the range gets the
  held track's new value. Works for both values, and ignores grouping.
- Ranges skip the children of an expanded folder between the two ends.
- Last touched: after a ranged press, a Shift range starts from the anchor
  track.
- Anchor release: bank or channel navigation while holding releases the anchor
  (pressing another button is then a normal press, and releasing the held
  button does nothing). Same for switching to Send/Receive mode, and deleting
  the held track.
- Each action's anchor is independent: holding mute and pressing another
  track's select is a normal select.
- One anchor per strip: hold mute on a strip, then long press its select. The
  select anchor replaces the mute anchor, and releasing mute leaves the select
  anchor held.
- Holding Send and pressing select still picks a track for Send/Receive mode.
- Performance: steady state `Run()` average unchanged. A ranged press on a large
  project (100+ tracks) stays in the low milliseconds.
