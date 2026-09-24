# Automation Modes

The X-Touch automation buttons (Trim, Read, Touch, Write, Latch) set the
automation mode of the selected tracks, and light up to show which modes the
selection is in. Group turns REAPER's global automation override on and off,
and while it is on the same buttons set and show the override instead. Along
the way, polled toggles became writable, state properties became current as
soon as a mapping uses them, and `TrackCache` learned to take REAPER events.

REAPER's global automation override (the automation control on the transport)
makes every track behave as if it were in one mode, without changing the
tracks' own modes. It is either off, one of the track modes, or Bypass, which
ignores all automation. It is saved per project, and isn't an undo step.

## Behavior

### Without an override

| Button | Press: set the selected tracks to | REAPER action |
| ------ | --------------------------------- | ------------- |
| Trim   | Trim/Read                         | 40400         |
| Read   | Read                              | 40401         |
| Touch  | Touch                             | 40402         |
| Write  | Write                             | 40403         |
| Latch  | Latch                             | 40404         |

| Light    | While                                        |
| -------- | -------------------------------------------- |
| Off      | No selected track is in the mode             |
| Solid    | Every selected track is in the mode          |
| Blinking | Some, but not all, selected tracks are in it |

- Each press runs one REAPER action on the selected tracks, which is one undo
  point. With nothing selected it does nothing. No modifiers, long press, or
  double press.
- The master track counts like any other selected track, as the actions change
  its mode too, so the lights describe exactly the tracks a press would change.
- REAPER has a sixth track mode, Latch Preview, which the SDK doesn't document
  and which has no button. Tracks in it light nothing, but still count toward
  a mixed selection, so a mix of Latch and Latch Preview tracks blinks Latch. A
  selection entirely in Latch Preview looks like no selection, which was
  accepted as rare.
- The lights follow the current project when switching project tabs.

### With an override (Group)

| Override      | Group | Mode lights                   | Mode button press                           |
| ------------- | ----- | ----------------------------- | ------------------------------------------- |
| None          | Off   | Selected tracks' modes        | Set the selected tracks' mode               |
| Bypass        | Solid | All off                       | Set the override to that mode               |
| A mode        | Solid | That mode solid, the rest off | Same mode: set Bypass. Other: set that mode |
| Latch Preview | Solid | Latch blinking, the rest off  | Set the override to that mode               |

- Pressing Group with no override restores the last override, whether it was
  set from the surface or REAPER's transport, or Bypass if there hasn't been
  one since REAPER started. Pressing it while an override is on turns it off.
- While an override is on, the surface doesn't change the tracks' own modes.
- A Latch Preview override can only be set from REAPER. Latch blinks for it
  (blinking has no other meaning during an override), and as Latch Preview
  isn't Latch, pressing Latch sets Latch.
- The lights follow the override when it is changed from REAPER, and when
  switching project tabs.

## REAPER facts

Found with temporary logging, as the SDK doesn't document them:
- `IReaperControlSurface::SetAutoMode()` is called when a track's mode is
  changed by its own automation button, an action, or the surface, with that
  single mode. It isn't called for undo, redo, selection changes, or the
  global override.
- `SetSurfaceSelected()` is called for every track after every mode change,
  undo, redo, and global override change, as well as for selection changes.
  Undo and redo also call `SetTrackListChange()`.
- `GetGlobalAutomationOverride()` returns -1 for none, 0–5 for the track modes
  (the same values as `I_AUTOMODE`, with 5 for Latch Preview), and 6 for
  Bypass. The SDK says 5 is Bypass, which is wrong.
- The actions 40400–40404 report no toggle state. The "Global automation
  override" actions do.

## Structure

### common: automation.h/.cc

- `AutoMode` (0–5, including `kLatchPreview`), `kAutoModeCount`, and
  `AutoModes`, a `gb::Flags<AutoMode>` set.
- `AutoOverride` (`kNone = -1`, the six modes, and `kBypass = 6`), with
  `GetAutoOverride()` and `SetAutoOverride()`, plain wrappers of the REAPER
  calls.

### common: selected track modes (track_cache.h/.cc)

`TrackCache` caches which modes the selected tracks (including the master) are
in, as an `AutoModes` mask:
- `HasSelectedAutoMode(mode)` and `HasMixedSelectedAutoModes()` (more than one
  mode) read it, re-reading every selected track's `I_AUTOMODE` first if it is
  stale.
- `OnSelectionChanged()` and `OnAutoModeChanged()` mark it stale. They are named
  for the REAPER events `ControlSurface` forwards from `SetSurfaceSelected()`
  and `SetAutoMode()`, so `TrackCache` decides what each event makes stale, and
  its comments record which REAPER changes arrive as which event. `Refresh()`
  marks it stale too.
- It is lazy, so a burst of notifications (selecting 100 tracks sends 100
  `SetSurfaceSelected()` calls) costs one re-read, on the next use, and nothing
  while no light is mapped.

### scene: polled toggles (reaper_property.h/.cc)

All of this is rows in the `kPolledToggles` table:

| Property                      | Index | On while                                 | Write on          | Write off   |
| ----------------------------- | ----- | ---------------------------------------- | ----------------- | ----------- |
| `kStateSelectedAuto<Mode>` x6 | 4–9   | Any selected track is in the mode        |                   |             |
| `kStateSelectedAutoMixed`     | 10    | The selected tracks are in several modes |                   |             |
| `kStateAutoOverrideActive`    | 11    | Any override is on                       | The last override | No override |
| `kStateAutoOverride<Mode>` x6 | 12–17 | The override is the mode                 | Set it            | Bypass      |
| `kStateAutoOverrideBypass`    | 18    | The override is Bypass                   | Set it            | No override |

- `scene` exposes every mode and override REAPER has, including Latch Preview
  and Bypass, whether or not the X-Touch has a button for it.
- Each mode covers the whole selection exactly when it is the only mode
  present, so "mixed" alone decides solid or blinking for all the lights.
- The last override is a file-local variable beside the Active row. It is part
  of what turning Active on means, not reusable REAPER logic, so it isn't in
  `common`. Active's read function records it, and is polled every run while
  Group is mapped, so overrides set from REAPER count too.

### plugin: mappings (control_surface.cc)

All in `ControlSurface::InitViews()`, from a small `kAutoModeButtons` table:
- Group has a read and write mapping to `kStateAutoOverrideActive`.
- Each mode button has two pairs of mappings, switched by a
  `ViewMapping::Condition` on `kStateAutoOverrideActive`:
  - No override: a read mapping to its `kCmdAutoMode*` action, and a light from
    its `kStateSelectedAuto*` toggle with a `mode_overrides` entry on
    `kStateSelectedAutoMixed` to blink (output mode 1).
  - Override: a read mapping to its `kStateAutoOverride*` toggle, and a light
    from the same toggle with a `mode_overrides` entry on
    `kStateAutoOverrideLatchPreview` to blink.
- Latch's override light is instead `auto_override_any_latch`, on for Latch or
  Latch Preview. It exists only because the X-Touch has no Latch Preview
  button, so the plugin adds it to the scene itself as a read-only
  `PolledToggleProperty`, rather than it being a scene row.
- Conditions re-register read mappings when they switch, which loses a pending
  long or double press. The automation buttons have neither, so conditions
  (rather than views, as surface modes use) are the simplest fit for a switch
  that follows REAPER state.

## Building blocks

- **Writable polled toggles (scene/reaper_property.h/.cc)**: a `kPolledToggles`
  row may have a write function (`void (*)(bool)`) as well as its read
  function. `PolledToggleProperty::WriteBool()` calls it and then
  `UpdateState()`. Read-only rows have none and ignore writes. Plugins can also
  add their own `PolledToggleProperty` to a scene, for state that is device
  specific.
- **Template row helpers (reaper_property.cc)**: `HasSelectedAutoMode<Mode>`,
  `IsAutoOverride<Override>`, and `WriteAutoOverride<On, Off>` turn a row into
  one line of plain function pointers.
- **State properties are current when first used (scene_state_property.cc)**:
  `SceneStateProperty::OnRegistered()` calls `UpdateState()`. A mapping writes
  its control as soon as it becomes active, and a property only updates while
  a mapping uses it, so before this a light switched on by a condition or view
  could show state from when it was last used, for one frame. `UpdateState()`
  must therefore stay cheap.
- **Forwarding REAPER events to `TrackCache`**: new state derived from the
  selection or automation modes goes behind `OnSelectionChanged()` /
  `OnAutoModeChanged()`, so `ControlSurface` needs no change for it.

## Performance

- In normal use, the `Run()` average and max don't move.
- Without an override, the lights cost one `GetGlobalAutomationOverride()`
  call (for Group) and a few cached bit tests per run. During an override, up
  to eight override reads per run, each a cheap REAPER getter.
- Changing the automation mode of 100 tracks at once gives a one-off 2–3ms
  frame, which was accepted (a frame is ~33ms).
