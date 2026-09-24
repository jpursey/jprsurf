# Automation Modes

The X-Touch automation buttons (Trim, Read, Touch, Write, Latch) set the REAPER
automation mode of the selected tracks, and each button lights up while any
selected track is in its mode. Group turns REAPER's global automation override
on and off, and while it is on the same buttons set and show the override
instead.

REAPER's global automation override (the automation control on the transport)
makes every track behave as if it were in one mode, without changing the
tracks' own modes, which come back when it is turned off. It is either off ("no
override"), one of the track modes, or Bypass, which ignores all automation.

## Behavior

### Buttons (no override)

| Button | Press: set the selected tracks to | REAPER action |
| ------ | --------------------------------- | ------------- |
| Trim   | Trim/Read                         | 40400         |
| Read   | Read                              | 40401         |
| Touch  | Touch                             | 40402         |
| Write  | Write                             | 40403         |
| Latch  | Latch                             | 40404         |

- Each press runs exactly one REAPER action ("Automation: Set track automation
  mode to ..."), which acts on the selected tracks and adds one undo point.
  With no tracks selected it does nothing.
- No modifiers, long press, or double press.

### Lights (no override)

| Light    | While                                          |
| -------- | ---------------------------------------------- |
| Off      | No selected track is in the mode               |
| Solid    | Every selected track is in the mode            |
| Blinking | Some, but not all, selected tracks are in it   |

- When the selected tracks are in different modes, each of their modes blinks.
  Pressing one of the buttons sets them all, leaving that one solid.
- All off when no tracks are selected.
- Trim/Read is REAPER's default mode, so Trim is lit for most selections.
- The master track counts when it is selected, as the actions change its mode
  too. The lights describe exactly the tracks a press would change.
- They follow the current project when switching project tabs.

### Global override (Group)

Group works like a mode button for the automation section:

| Override      | Group | Mode lights                   | Mode button press                         |
| ------------- | ----- | ----------------------------- | ----------------------------------------- |
| None          | Off   | Selected tracks' modes        | Set the selected tracks' mode             |
| Bypass        | Solid | All off                       | Set the override to that mode             |
| A mode        | Solid | That mode solid, the rest off | Same mode: set Bypass. Other: set that mode |
| Latch Preview | Solid | Latch blinking, the rest off  | Set the override to that mode             |

- Pressing Group with no override turns on the last override (the last one set
  from the surface or seen in REAPER), or Bypass if there hasn't been one since
  REAPER started. Pressing it while an override is on turns it off.
- While an override is on, the surface can't change the tracks' own modes. It
  shows and sets only the override. Turning the override off brings back the
  selected tracks' lights.
- Changing the override in REAPER (the transport control or its actions) updates
  the lights.
- The override is per project, so the lights follow it when switching project
  tabs.
- Latch Preview has no button of its own, so a Latch Preview override (set from
  REAPER) blinks Latch. Blinking has no other meaning during an override, as
  exactly one override is on at a time. Latch Preview is not Latch, so pressing
  Latch sets the override to Latch (solid), and pressing it again goes to
  Bypass, the same as any mode that isn't lit solid. The surface can't set a
  Latch Preview override itself.
- Without an override, tracks in Latch Preview light nothing (blinking already
  means "some selected tracks", so it can't also mean Latch Preview there). They
  still count toward a mixed selection, so a mix of Latch and Latch Preview
  tracks blinks Latch. A selection entirely in Latch Preview looks the same as
  no selection, which is accepted as rare.

## Design

### Buttons

`kCmdAutoModeTrim`, `kCmdAutoModeRead`, `kCmdAutoModeTouch`,
`kCmdAutoModeWrite`, and `kCmdAutoModeLatch` already exist in
`reaper_property.h` (unused). The buttons are just read mappings to them in
`ControlSurface::InitViews()`, next to the utility buttons.

REAPER reports no toggle state for these actions (the action list's State
column is empty for them), so `Scene::GetProperty()` makes them
`CommandActionProperty`s, and a press always runs the action.

### Lights: which modes are in the selection

REAPER has no single query for this. It is:

```
for i in CountSelectedTracks2(nullptr, true):
  modes |= 1 << GetMediaTrackInfo_Value(GetSelectedTrack2(nullptr, i, true), "I_AUTOMODE")
```

That is cheap for a few selected tracks, but grows with the selection (and
`GetSelectedTrack2` may itself walk the track list), so it shouldn't run every
frame. (An early exit once every mode is found was considered, but a selection
with every mode in it is too rare for the check to pay for itself.)

Instead it runs only when its answer can change:
- **`IReaperControlSurface::SetAutoMode()`**: REAPER calls this when an
  automation mode changes (the SDK says "automation mode for current track").
- **`SetSurfaceSelected()`**: the selection changed. It already marks the mode
  buttons dirty for the same reason.
- **`TrackCache::Refresh()`**: the track list changed, including switching
  project tabs.

The result is cached as a bitmask in `TrackCache` (common), which already holds
"generic REAPER track state", alongside `GetOnlySelectedTrack()`:
- `enum class AutoMode { kTrimRead, kRead, kTouch, kWrite, kLatch,
  kLatchPreview }` in a new `common/automation.h`, with values matching
  REAPER's `I_AUTOMODE` (0–5).
- `TrackCache::HasSelectedAutoMode(AutoMode)` returns whether any selected track
  is in the mode, recomputing the mask first if it is stale.
- `TrackCache::HasMixedSelectedAutoModes()` returns whether the selected tracks
  are in more than one mode (more than one bit set), recomputing the same way.
- `TrackCache::OnSelectionChanged()` and `OnAutoModeChanged()` mark it stale.
  They are named for the REAPER events `ControlSurface` forwards
  (`SetSurfaceSelected()` and `SetAutoMode()`), so `TrackCache` decides what
  each event makes stale, and records in its comments which REAPER changes
  arrive as which event. `Refresh()` marks it stale itself.

It is lazy, so a burst of notifications (selecting 100 tracks sends 100
`SetSurfaceSelected()` calls) costs one recompute, on the next read.

The invalidation calls are in the REAPER callbacks, not `ControlSurface::Run()`.
Nothing reads the mask directly: `Scene::OnRun()` calls `UpdateState()` on
every registered state property each run, so the first of the polled toggles
below recomputes it, the rest reuse it, and any whose value changed notify
their mappings, which rewrite the lights in the same run (`SyncMappings()`).
`Run()` refreshes `TrackCache` before running the scene, so the mask is never
read from a stale track list. A state property is only registered while an
active mapping uses it, so when nothing shows the lights (such as during a
global override, CL8), the mask is never recomputed at all.

Seven polled toggles in `kPolledToggles` (scene) read the cached mask: one per
mode, including Latch Preview, and one for "mixed". `scene` exposes every mode
REAPER has, whether or not a device has a button for it. A toggle is only
polled while a mapping uses it, so once the mask is cached, a run costs a bit
test for each one mapped.

Each track is in exactly one mode, so a mode covers the whole selection exactly
when it is the only mode present. So "mixed" alone decides solid or blinking
for all five lights, and no per-mode "all" state is needed:
- Each light is a write mapping from its mode's toggle (off or on).
- The mapping has a `WriteConfig::mode_overrides` entry on the mixed toggle,
  switching to output mode 1 (MCU blinking) while it is true. This is existing
  mapping machinery, and the mapping rewrites the light when either property
  changes.

**Brittleness:** the cache is only as fresh as the REAPER events forwarded to
`TrackCache`, and `ControlSurface` has to forward them. CL1's
findings show every change tested is followed by one of `SetAutoMode()`,
`SetSurfaceSelected()`, or `SetTrackListChange()`. The known gap is a ReaScript
setting `I_AUTOMODE` directly, which leaves a light stale until the next
selection change. If that ever matters, the fix is to also invalidate when
`GetProjectStateChangeCount()` moves.

**Latch Preview:** REAPER has a sixth track mode, Latch Preview (`I_AUTOMODE`
5), which the SDK comments don't mention and which has no button. `AutoMode`
includes it as `kLatchPreview`, and it is a bit in the mask like the others, so
a selection of Read and Latch Preview tracks blinks Read, as not every selected
track is in Read. Without an override, no light shows Latch Preview itself (see
Behavior).

### Global override

REAPER reads and sets the override with `GetGlobalAutomationOverride()` and
`SetGlobalAutomationOverride()`: -1 for none, and otherwise a track mode (the
same values as `I_AUTOMODE`, including 5 for Latch Preview), or 6 for Bypass
(not the 5 the SDK says, see CL1's findings). It is per project, and isn't in
the undo history. There is no known notification for it, so
it is polled, by the properties' own `UpdateState()` from `Scene::OnRun()`, the
same as the polled toggles and command toggle states. `ControlSurface` has no
part in it. That should be cheap, as it only reads a setting, and CL8 measures
it. (REAPER's "Global automation override" actions do report a toggle state,
such as 40876 "No override", which confirms REAPER tracks it as ordinary state.
The properties don't use those actions, as "same mode again goes to Bypass"
needs its own write behavior.)

**common (automation.h/.cc):** alongside `AutoMode`:
- `AutoOverride` (`kNone = -1`, the six track modes, and `kBypass = 6`), with
  values matching REAPER.
- `GetAutoOverride()` and `SetAutoOverride()` wrap the REAPER calls.
- Which override to restore isn't reusable REAPER logic but part of what
  turning `kStateAutoOverrideActive` on means, so `scene` tracks it beside that
  toggle: its read function remembers the last override other than none
  (`kBypass` until there is one), and polling means REAPER-set overrides count
  too.

**scene: writable polled toggles.** The override properties are polled
toggles, the same as the selected track states: rows in `kPolledToggles`
named `kStateName<Index>`, which read the override each run. The one addition
is that a row may also have a write function, which `PolledToggleProperty`
calls when the toggle is written (read-only rows have none, and ignore writes).
This keeps one table-driven mechanism instead of a separate property type with
its own names and table.

| Property                         | On while the override is | Write on          | Write off   |
| -------------------------------- | ------------------------ | ----------------- | ----------- |
| `kStateAutoOverrideActive`       | Anything but none        | The last override | No override |
| `kStateAutoOverride<Mode>` x5    | That mode                | Set it            | Bypass      |
| `kStateAutoOverrideLatchPreview` | Latch Preview            | Set it            | Bypass      |
| `kStateAutoOverrideBypass`       | Bypass                   | Set it            | No override |

`kStateAutoOverrideBypass` isn't used by the X-Touch, but like Latch Preview,
`scene` exposes every override REAPER has.

A read mapping on a toggle writes the opposite of its current value, so these
give exactly the button behavior in the table above, and write mappings from
them give the lights.

**plugin: mappings.** Group has a read and write mapping to
`kStateAutoOverrideActive`. Each mode button has two sets of mappings, switched
by a `ViewMapping::Condition` on `kStateAutoOverrideActive`:
- Off: the per-track read mapping (`kCmdAutoMode*`) and light (CL4).
- On: a read and write mapping to its `kStateAutoOverride<Mode>`. Latch is the
  exception, as its light and press differ in Latch Preview:
  - Its read mapping is to `kStateAutoOverrideLatch`, so from Latch Preview
    (off) a press sets Latch.
  - Its light is a write mapping from an "any Latch" toggle, on while the
    override is Latch or Latch Preview, with a `mode_overrides` entry on
    `kStateAutoOverrideLatchPreview` to blink, the same mechanism as the
    per-track lights.
  - "Any Latch" only exists because the X-Touch has no Latch Preview button,
    so it is a plugin property, not a scene one: a read-only
    `PolledToggleProperty` the plugin adds to the scene itself.

Conditions re-register read mappings when they switch, which loses a pending
long or double press. The automation buttons have neither, so that is safe
here.

## CLs

### CL1 [x] plugin: automation buttons set the selected tracks' mode

Depends on: none.

- Read mappings from the five automation buttons to `kCmdAutoMode*`.
- User guide: automation buttons under Always available, and the cheat sheet.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).
- With one and several tracks selected, each button sets all of them to its
  mode (check the track automation mode buttons in the TCP or mixer). Tracks
  that aren't selected don't change.
- Each press is one undo point, and undo restores the previous modes.
- With nothing selected, a press does nothing.
- With only the master track selected, does a press change the master's mode?
  (Decides whether the lights include the master.)
- Temporary, removed before commit:
  - Log `SetAutoMode()` at INFO. Record when REAPER calls it: surface press,
    action from the keyboard or action list, the TCP automation button on a
    single track (selected and not), undo/redo of a mode change, a ReaScript
    `SetMediaTrackInfo_Value(tr, "I_AUTOMODE", n)`, selection changes, and
    changing the global override from the transport.
  - Log `GetGlobalAutomationOverride()` as the override is changed from the
    transport, to confirm the values (-1, 0–4, 5 for Bypass). Also check
    whether it changes when switching project tabs, and whether changing it
    adds an undo point.
  - Record the findings here, and adjust the later CLs before starting them.

**Findings**
- The buttons work as planned, including with several tracks selected, as one
  undo point each.
- REAPER calls `SetAutoMode()` for a surface press, the actions run from REAPER,
  and a track's own automation mode button (selected or not). The argument is a
  single mode, so it only says that something changed. Each call is followed by
  `SetSurfaceSelected()` for every track.
- It does not call `SetAutoMode()` for undo/redo or selection changes. Undo and
  redo call `SetTrackListChange()` and `SetSurfaceSelected()` for every track
  instead, and selection changes call `SetSurfaceSelected()`, so the planned
  invalidation covers every path tested. The `GetProjectStateChangeCount()`
  fallback isn't needed.
- A ReaScript setting `I_AUTOMODE` directly couldn't be tested. If REAPER
  doesn't report it, the lights catch up at the next selection change.
- The global override:
  - never calls `SetAutoMode()`, but each change is followed by
    `SetSurfaceSelected()` for every track;
  - adds no undo point;
  - is per project: switching project tabs changes it (and the per-track modes),
    again with no `SetAutoMode()` call;
  - reports 6 for Bypass, not the 5 the SDK says. 5 is Latch Preview, a sixth
    track mode the SDK comments don't mention (actions 42023 and 42024). The
    values logged while stepping down the transport's menu were 6 (Bypass),
    0, 1, 0, 2, 4 (Latch), 5 (Latch Preview), 3 (Write), 0, -1.
- The actions change the master track's mode when it is selected, like any
  other track.

### CL2 [x] common: selected track automation modes in TrackCache

Depends on: CL1 (findings).

- `AutoMode` enum in a new `common/automation.h`.
- `TrackCache::HasSelectedAutoMode()`, `HasMixedSelectedAutoModes()`, and
  `InvalidateSelectedAutoModes()`, with the lazily recomputed mask. `Refresh()`
  invalidates it.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL4, whose lights show exactly what these return. Nothing calls
  them until then, so there is nothing to check in REAPER.

### CL3 [x] scene: polled toggles for the selected automation modes

Depends on: CL2.

- `kStateSelectedAutoTrimRead`, `kStateSelectedAutoRead`,
  `kStateSelectedAutoTouch`, `kStateSelectedAutoWrite`, and
  `kStateSelectedAutoLatch`, and `kStateSelectedAutoLatchPreview`
  (`kStateName<4>` to `<9>`), with rows in `kPolledToggles` reading
  `TrackCache::HasSelectedAutoMode()`.
- `kStateSelectedAutoMixed` (`kStateName<10>`), reading
  `TrackCache::HasMixedSelectedAutoModes()`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL4.

### CL4 [x] plugin: automation button lights

Depends on: CL3.

- `SetAutoMode()` and `SetSurfaceSelected()` call
  `TrackCache::InvalidateSelectedAutoModes()`.
- Write mappings from the five mode toggles to the automation buttons, each
  with a mode override to blinking on `kStateSelectedAutoMixed`.
- User guide: the lights.

**Verify**
- Standard checks.
- Select one track: exactly its mode's button is solid. Pressing another button
  moves the light.
- Select several tracks all in one mode: that button is solid.
- Select tracks in different modes: each of their modes blinks, and the rest are
  off. Setting them all with one press leaves only that button, solid.
- Change the selection from mixed to one mode (and back) without changing any
  modes: the lights switch between blinking and solid.
- Changing a mode from REAPER (TCP button, action list, undo) updates the
  lights, including a selected track that isn't the first one selected.
- No tracks selected: all off. Only the master track selected: its mode is lit.
- Switching project tabs updates the lights.
- **Performance:** in a large project (100+ tracks), the `Run()` average doesn't
  move from before the change. Select all tracks, then change all their modes
  from REAPER: no frame spike beyond REAPER's own work (the `Run()` max).

**Findings**
- All checks pass. The `Run()` average and max don't move in normal use.
- Changing the automation mode of 100 tracks at once gives a one-off 2–3ms
  frame, which was accepted (a frame is ~33ms).

### CL5 [x] common: global automation override

Depends on: CL2.

- `AutoOverride`, `GetAutoOverride()`, `SetAutoOverride()`, and
  `GetLastAutoOverride()` in `automation.h/.cc`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL8.

### CL6 [x] scene: global automation override properties

Depends on: CL5.

- An optional write function for `PolledToggleProperty` rows.
- `kStateAutoOverrideActive`, the five `kStateAutoOverride<Mode>` properties,
  `kStateAutoOverrideLatchPreview`, and `kStateAutoOverrideBypass`
  (`kStateName<11>` to `<18>`), as writable rows in `kPolledToggles`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL8.

### CL7 [x] scene: state properties are current when first used

Depends on: none.

A `SceneStateProperty` only updates its state while a mapping uses it, and a
mapping writes its control as soon as it becomes active. So a mapping switched
on by a condition (CL8's override lights) or a view (modes) writes whatever
state the property cached when it was last used, until the next run corrects
it: a wrong light for one frame. `SceneStateProperty::OnRegistered()` now calls
`UpdateState()`, so a property is current the moment anything starts using it.
- Every `UpdateState()` is a cheap REAPER read (command toggle state, the
  polled toggles, the ruler modes, and the timeline position).
- Found by `/code-review` on CL8. The flash wasn't visible in testing, but the
  fix is general and removes it for every conditioned or view-switched light.

**Verify**
- Standard checks, including the smoke test (the timecode display, ruler mode
  lights, transport lights, and mode buttons all use state properties).
- No new `Run()` spikes when switching modes.

### CL8 [x] plugin: Group button and override mode

Depends on: CL4, CL6, CL7.

- Group: read and write mappings to `kStateAutoOverrideActive`.
- Mode buttons: the CL1 read mappings and CL4 lights get a condition on
  `kStateAutoOverrideActive` being off, and new read and write mappings to
  `kStateAutoOverride<Mode>` with a condition on it being on (Latch's light
  from a plugin "any Latch" `PolledToggleProperty`, blinking on
  `kStateAutoOverrideLatchPreview`).
- User guide: Group and the override.

**Verify**
- Standard checks.
- No override: Group is off, and the mode buttons behave as in CL1 and CL4.
- Group from no override, after a fresh REAPER start: Bypass is on (check the
  transport), Group is solid, and the mode lights are all off.
- In Bypass, pressing a mode button sets the override to it: that button is
  solid, the rest off. Pressing another mode moves it. Pressing the same one
  again goes back to Bypass.
- Group while an override is on turns it off, and the selected tracks' lights
  come back. The tracks' own modes are unchanged.
- Group again turns on the last override (e.g. Read, not Bypass).
- Setting the override from REAPER's transport, with the surface in either
  state, updates Group and the mode lights. Group afterwards remembers that one.
- Latch Preview override (set from the transport): Latch blinks, the rest are
  off. Pressing Latch sets Latch (solid), and pressing it again goes to Bypass.
- Without an override, select only tracks in Latch Preview: all lights off.
  Add a track in Latch: Latch blinks.
- While an override is on, mode button presses don't change any track's mode.
- Switching project tabs: the lights match the transport in each project.
- **Performance:** the `Run()` average doesn't move from before the change.

### CL9 [x] scene, common: the last override lives with the Group toggle

Depends on: CL8.

Which override to restore isn't reusable REAPER logic but part of what turning
`kStateAutoOverrideActive` on means, so it moves out of `common` and next to
that row.
- `kStateAutoOverrideActive`'s read function records the last override other
  than none that it sees, in a file-local variable in `reaper_property.cc`, and
  its write function restores it. It is polled every run while Group is
  mapped, so it sees overrides set from REAPER as well as from the surface.
- `common`'s `GetAutoOverride()` and `SetAutoOverride()` become plain wrappers,
  and `GetLastAutoOverride()` is removed.
- One CL across `common` and `scene`, as removing the `common` function first
  would break `scene`.

**Verify**
- Standard checks.
- CL8's Group checks: after a fresh start Group turns on Bypass, then it
  restores the last override set from the surface or from REAPER's transport.

### CL10 [x] common, plugin: TrackCache is told about REAPER events

Depends on: CL8.

`ControlSurface` calls `TrackCache::InvalidateSelectedAutoModes()`, which names
the cache rather than what happened, so the knowledge of which REAPER callbacks
matter is split between `plugin` and `common`. Each new selection-derived value
cached in `TrackCache` would need another call from each callback.
- Replace it with `TrackCache::OnSelectionChanged()` and `OnAutoModeChanged()`,
  called from `SetSurfaceSelected()` and `SetAutoMode()`. `TrackCache` decides
  what each one invalidates, and its comments record which REAPER events call
  them (CL1's findings).
- Update the comment on the `kStateSelectedAuto*` toggles to match.
- No change in behavior.

**Verify**
- Standard checks.
- CL4's light checks: selection changes, mode changes from the surface and from
  REAPER, and undo/redo still update the lights.
