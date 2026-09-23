# Automation Modes

The X-Touch automation buttons (Trim, Read, Touch, Write, Latch) set the REAPER
automation mode of the selected tracks, and each button lights up while any
selected track is in its mode. Group turns REAPER's global automation override
on and off, and while it is on the same buttons set and show the override
instead.

REAPER's global automation override (the automation control on the transport)
makes every track behave as if it were in one mode, without changing the
tracks' own modes, which come back when it is turned off. It is either off ("no
override"), one of the five modes, or Bypass, which ignores all automation.

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
- The master track counts when it is selected, if the actions act on it (see
  CL1). The lights should describe exactly the tracks a press would change.
- They follow the current project when switching project tabs.

### Global override (Group)

Group works like a mode button for the automation section:

| Override | Group | Mode lights                   | Mode button press                         |
| -------- | ----- | ----------------------------- | ----------------------------------------- |
| None     | Off   | Selected tracks' modes        | Set the selected tracks' mode             |
| Bypass   | Solid | All off                       | Set the override to that mode             |
| A mode   | Solid | That mode solid, the rest off | Same mode: set Bypass. Other: set that mode |

- Pressing Group with no override turns on the last override (the last one set
  from the surface or seen in REAPER), or Bypass if there hasn't been one since
  REAPER started. Pressing it while an override is on turns it off.
- While an override is on, the surface can't change the tracks' own modes. It
  shows and sets only the override. Turning the override off brings back the
  selected tracks' lights.
- Changing the override in REAPER (the transport control or its actions) updates
  the lights.
- The lights follow whatever `GetGlobalAutomationOverride()` reports, so they
  are right whether REAPER keeps the override per project or REAPER-wide (CL1
  checks which, for the user guide).

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
for i in CountSelectedTracks2(nullptr, want_master):
  modes |= 1 << GetMediaTrackInfo_Value(GetSelectedTrack2(nullptr, i, want_master), "I_AUTOMODE")
```

with an early exit once all five bits are set. That is cheap for a few selected
tracks, but grows with the selection (and `GetSelectedTrack2` may itself walk
the track list), so it shouldn't run every frame.

Instead it runs only when its answer can change:
- **`IReaperControlSurface::SetAutoMode()`**: REAPER calls this when an
  automation mode changes (the SDK says "automation mode for current track").
- **`SetSurfaceSelected()`**: the selection changed. It already marks the mode
  buttons dirty for the same reason.
- **`TrackCache::Refresh()`**: the track list changed, including switching
  project tabs.

The result is cached as a bitmask in `TrackCache` (common), which already holds
"generic REAPER track state", alongside `GetOnlySelectedTrack()`:
- `enum class AutoMode { kTrimRead, kRead, kTouch, kWrite, kLatch }` in a new
  `common/automation.h`, with values matching REAPER's `I_AUTOMODE` (0–4).
- `TrackCache::HasSelectedAutoMode(AutoMode)` returns whether any selected track
  is in the mode, recomputing the mask first if it is stale.
- `TrackCache::HasMixedSelectedAutoModes()` returns whether the selected tracks
  are in more than one mode (more than one bit set), recomputing the same way.
- `TrackCache::InvalidateSelectedAutoModes()` marks it stale. `Refresh()` calls
  it itself.

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
global override, CL7), the mask is never recomputed at all.

Six polled toggles in `kPolledToggles` (scene) read the cached mask: one per
mode, and one for "mixed". So once the mask is cached, a run costs six bit
tests.

Each track is in exactly one mode, so a mode covers the whole selection exactly
when it is the only mode present. So "mixed" alone decides solid or blinking
for all five lights, and no per-mode "all" state is needed:
- Each light is a write mapping from its mode's toggle (off or on).
- The mapping has a `WriteConfig::mode_overrides` entry on the mixed toggle,
  switching to output mode 1 (MCU blinking) while it is true. This is existing
  mapping machinery, and the mapping rewrites the light when either property
  changes.

**Brittleness:** the cache is only as fresh as the invalidation calls, and
`ControlSurface` has to make them from the right REAPER callbacks. The risk is a
change REAPER doesn't report through `SetAutoMode()` (for example a ReaScript
setting `I_AUTOMODE`, or undoing a mode change), leaving a light stale until the
next selection change. CL1 checks this with temporary logging. If there are
gaps, the fallback is to also invalidate when `GetProjectStateChangeCount()`
moves, which is one cheap call per run and catches anything that makes an undo
point (a mode change does).

### Global override

REAPER reads and sets the override with `GetGlobalAutomationOverride()` and
`SetGlobalAutomationOverride()`: -1 for none, 0–4 for the modes (the same values
as `I_AUTOMODE`), and 5 for Bypass. There is no known notification for it, so
it is polled, by the properties' own `UpdateState()` from `Scene::OnRun()`, the
same as the polled toggles and command toggle states. `ControlSurface` has no
part in it. That should be cheap, as it only reads a setting, and CL7 measures
it. (REAPER's "Global automation override" actions do report a toggle state,
such as 40876 "No override", which confirms REAPER tracks it as ordinary state.
The properties don't use those actions, as "same mode again goes to Bypass"
needs its own write behavior.)

**common (automation.h/.cc):** alongside `AutoMode`:
- `AutoOverride` (`kNone = -1`, the five modes, and `kBypass = 5`), with values
  matching REAPER.
- `GetAutoOverride()` and `SetAutoOverride()` wrap the REAPER calls, and both
  remember the last override that wasn't `kNone`, which
  `GetLastAutoOverride()` returns (`kBypass` until there is one). Remembering
  it inside the getter and setter means the surface and REAPER's own control
  are tracked the same way, with no caller having to record it.

**scene: `AutoOverrideProperty`:** a polled toggle `SceneStateProperty` that
reads the override each run and notifies when its value changes. It has a
target, which gives the six properties that the surface needs, created on
demand by `Scene::GetProperty()` like the timeline properties:

| Property                 | On while            | Write on           | Write off          |
| ------------------------ | ------------------- | ------------------ | ------------------ |
| `kAutoOverrideActive`    | Any override is on  | The last override  | No override        |
| `kAutoOverride<Mode>` x5 | The override is it  | Set it             | Bypass             |

A read mapping on a toggle writes the opposite of its current value, so these
give exactly the button behavior in the table above, and write mappings from
the same properties give the lights.

**plugin: mappings.** Group has a read and write mapping to
`kAutoOverrideActive`. Each mode button has two pairs of mappings, switched by
a `ViewMapping::Condition` on `kAutoOverrideActive`:
- Off: the per-track read mapping (`kCmdAutoMode*`) and light (CL4).
- On: a read and write mapping to its `kAutoOverride<Mode>`.

Conditions re-register read mappings when they switch, which loses a pending
long or double press. The automation buttons have neither, so that is safe
here.

## CLs

### CL1 [ ] plugin: automation buttons set the selected tracks' mode

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

### CL2 [ ] common: selected track automation modes in TrackCache

Depends on: CL1 (findings).

- `AutoMode` enum in a new `common/automation.h`.
- `TrackCache::HasSelectedAutoMode()`, `HasMixedSelectedAutoModes()`, and
  `InvalidateSelectedAutoModes()`, with the lazily recomputed mask. `Refresh()`
  invalidates it.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL4. Temporary: log the mask when it is recomputed, and check it
  matches the TCP for a few selections, then remove.

### CL3 [ ] scene: polled toggles for the selected automation modes

Depends on: CL2.

- `kStateSelectedAutoTrimRead`, `kStateSelectedAutoRead`,
  `kStateSelectedAutoTouch`, `kStateSelectedAutoWrite`, and
  `kStateSelectedAutoLatch` (`kStateName<4>` to `<8>`), with rows in
  `kPolledToggles` reading `TrackCache::HasSelectedAutoMode()`.
- `kStateSelectedAutoMixed` (`kStateName<9>`), reading
  `TrackCache::HasMixedSelectedAutoModes()`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL4.

### CL4 [ ] plugin: automation button lights

Depends on: CL3.

- `SetAutoMode()` and `SetSurfaceSelected()` call
  `TrackCache::InvalidateSelectedAutoModes()` (plus the fallback, if CL1 found
  gaps).
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
- No tracks selected: all off. Master track selected (per CL1's finding).
- Switching project tabs updates the lights.
- **Performance:** in a large project (100+ tracks), the `Run()` average doesn't
  move from before the change. Select all tracks, then change all their modes
  from REAPER: no frame spike beyond REAPER's own work (the `Run()` max).

### CL5 [ ] common: global automation override

Depends on: CL2.

- `AutoOverride`, `GetAutoOverride()`, `SetAutoOverride()`, and
  `GetLastAutoOverride()` in `automation.h/.cc`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL7.

### CL6 [ ] scene: global automation override properties

Depends on: CL5.

- `AutoOverrideProperty`, and `kAutoOverrideActive` and the five
  `kAutoOverride<Mode>` properties, created on demand by `Scene::GetProperty()`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Covered by CL7.

### CL7 [ ] plugin: Group button and override mode

Depends on: CL4, CL6.

- Group: read and write mappings to `kAutoOverrideActive`.
- Mode buttons: the CL1 read mappings and CL4 lights get a condition on
  `kAutoOverrideActive` being off, and new read and write mappings to
  `kAutoOverride<Mode>` with a condition on it being on.
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
- While an override is on, mode button presses don't change any track's mode.
- Switching project tabs: the lights match the transport in each project.
- **Performance:** the `Run()` average doesn't move from before the change.
