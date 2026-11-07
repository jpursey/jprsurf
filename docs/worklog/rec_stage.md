# Record Stages

A track that is armed in REAPER can either record its input, or only monitor
it, with REAPER's record mode "Record: disable (input monitoring only)". On the
surface, the Rec button arms tracks only to record, so hearing an external
source without recording it means changing the record mode in REAPER. Clearing
stray record arms with Ctrl+Alt+Rec also disarms the tracks being monitored.

This feature gives the scene REAPER's two other record properties, the record
mode and the monitoring mode, and one combination of them, the record stage:
off, monitor (armed, with recording disabled), or record (armed, recording).
The X-Touch example's Rec button steps through the stages: a press arms a track
to monitor, or disarms it, and a double press arms it to record. Its light is
solid while monitoring, and blinks while recording, so it shows which tracks
will actually record. Alt and Ctrl+Alt on a double press move every recording
track back to monitoring, and leave it armed.

The record stage is built in, rather than defined in the config (see Option 2
below). If another combination of properties is ever wanted, the plan is to
replace it with a config component that defines combinations, and define the
record stage with it.

## Behavior

### Rec in the X-Touch example

| Gesture                  | Off     | Monitor | Record  |
| ------------------------ | ------- | ------- | ------- |
| Press                    | Monitor | Off     | Monitor |
| Double press             | Record  | Record  | Monitor |
| Light                    | Off     | Solid   | Blinks  |

- A press is `toggle off .. monitor`, and a double press is
  `toggle monitor .. record`, so a double press on a recording track goes back
  to monitoring.
- Disarming leaves the record mode alone, so a track armed again in REAPER
  honors its mode, and the light shows it: solid if the mode is "disable", and
  blinking for any other.
- Arming to monitor sets the record mode to "disable". Arming to record from
  "disable" restores the mode the surface replaced, or "input" if it has none
  (the mode was set to "disable" in REAPER, or REAPER has restarted since). The
  surface remembers it in memory only, never in the project.
- A press is delivered up to 150ms later than today (the double press window):
  after the release, or 150ms into a hold.

### Modifiers

These are `TrackActions`' rules, generalized from toggles to any enumerated
track value (see Design). A press's range, `toggle a .. b` or `set x`, is what
the press does to one track. "Past" means later in the value order, which for
the record stage is off, monitor, record.

| Modifier         | Press (`toggle off .. monitor`)                  | Double press (`toggle monitor .. record`)          |
| ---------------- | ------------------------------------------------ | -------------------------------------------------- |
| *(none)*         | This track, and its grouped tracks get its value | The same                                           |
| **Ctrl**         | This track alone                                 | The same                                           |
| **Shift**        | The range from the last touched track gets its value | The same                                       |
| **Shift + Ctrl** | The same, crossing folders                       | The same                                           |
| **Alt**          | Every track past off goes to off (disarms all)   | Every track past monitor goes to monitor           |
| **Alt + Ctrl**   | The same, then this track alone to monitor       | The same, then this track alone to record          |
| **Alt + Shift**  | The same, then this track and its grouped tracks | The same, then this track and its grouped tracks   |
| **Option**       | This track and each selected track, individually | The same                                           |

- Alt + Ctrl with a double press makes this the only recording track, and
  leaves every monitored track armed.
- Option applies the press to each track on its own: with a press, off goes to
  monitor, monitor to off, and record to monitor; with a double press, record
  goes to monitor, and off and monitor go to record.
- Grouped tracks are the ones REAPER's grouped rec arm reaches: the track's rec
  arm group, and the selected tracks if the track is selected (selection
  ganging).
- The last touched track is set as it is for mute, solo, and rec arm today.

### Ranges

- Holding Rec anchors the track, and pressing Rec on another track sets the
  range to the anchor's record stage, as rec arm does today.
- Press and hold anchors once the press is delivered (150ms into the hold),
  after the press has changed the anchor track.
- Double press and hold anchors on the second press, after it has changed the
  anchor track, so a range can be set to record.
- While an anchor is held, the other Rec buttons have no double press, so a
  ranged press acts at once, as Select's do.

## Design

### Names

- **`track:rec_mode`**: REAPER's record mode (`I_RECMODE`), an enumerated
  value, with its `track:ui_rec_mode` form. Its values are REAPER's modes, in
  REAPER's order, named after REAPER's "Set track record mode to" actions:
  `input`, `output_stereo`, `none`, `output_stereo_compensated`,
  `midi_output`, `output_mono`, `output_mono_compensated`, `midi_overdub`,
  `midi_replace`, and the newer ones (MIDI touch-replace and latch-replace,
  multichannel output, and the forced input formats), whose numbers are To
  confirm. Not to be confused with REAPER's project record mode (autosplit or
  tape, `CSURF_EXT_SETRECMODE`), which this doesn't touch.
- **`track:monitor_mode`**: REAPER's record monitoring (`I_RECMON`), with
  `track:ui_monitor_mode`: `off`, `on`, and `auto` (REAPER's "auto/tape",
  not when playing).
- **`track:rec_stage`**: the combination, with `track:ui_rec_stage`: `off`
  (not armed), `monitor` (armed, record mode `none`), and `record` (armed, any
  other mode). Beside `track:rec_arm`, which stays the plain toggle.
- **`rec_stage`**, a new `action` of `track_anchor`.

### device: Held presses beside a double press

Today a normal press with a long or double press sibling is deferred, and is
never held (`Control::IsPressed()` is false), and neither is a double press.
So `strip.rec [held]` stops working once Rec has a `[double]` mapping.

- A press delivered while the button is still down stays pressed until it is
  released: a normal press delivered 150ms into a hold (`kPendingRelease`
  timing out), and the second press of a double press.
- `[held]` then works beside `[double]`, on from when the press is delivered,
  and `[double, held]` is on from the second press. The config already accepts
  both, so the language doesn't change. The spec's `held` gets a sentence on
  `double`.
- A normal press delivered after its release (after the double press window)
  is never held, as today.

### common: Record mode and monitoring mode on Track

```
// REAPER's record modes (I_RECMODE) and monitoring modes (I_RECMON), with
// REAPER's numbers.
enum class TrackRecMode { kInput = 0, kOutputStereo = 1, kNone = 2, ... };
enum class TrackMonitorMode { kOff = 0, kOn = 1, kAuto = 2 };

class Track {
  TrackRecMode GetRecMode() const;
  TrackMonitorMode GetMonitorMode() const;

  // The record mode the surface last replaced with kNone, or nullopt if it
  // hasn't since REAPER started. Kept in memory only.
  std::optional<TrackRecMode> GetReplacedRecMode() const;

  // Re-reads the record mode from REAPER, when REAPER calls the track's
  // SetSurfaceRecArm(), which it does whenever the mode changes.
  void RefreshRecMode();
};

class TrackBatch {
  void SetRecMode(Track* track, TrackRecMode mode,
                  TrackGrouping grouping = TrackGrouping::kNone);
  void SetMonitorMode(Track* track, TrackMonitorMode mode,
                      TrackGrouping grouping = TrackGrouping::kNone);
};

// The tracks a grouped rec arm change to the track reaches: its rec arm
// group, and the selected tracks if it is selected. Includes the track.
std::vector<Track*> GetRecArmGroup(Track* track);
```

- The monitoring mode is in `GetTrackState()`'s flags (&128 on, &256 auto),
  which `Refresh()` already reads, so it costs nothing more to cache.
- The record mode isn't in `GetTrackState()`. It is read with
  `GetMediaTrackInfo_Value()` and set with `SetMediaTrackInfo_Value()`, which
  is new to the API list, and gets its cost measured.
- `SetRecMode()` remembers the mode it replaces when it sets `kNone` over
  another mode. The memory is on `Track`, so it outlives scenes, and a config
  reload keeps it, which is harmless: it is only a better guess than `input`.
- REAPER has no grouping for the record mode, so `SetRecMode()` with
  `kGrouped` sets every track in `GetRecArmGroup()`. Whether the monitoring
  mode's setter can use REAPER's own grouping (`CSurf_OnInputMonitorChangeEx`)
  is To confirm.
- `TrackBatch` adds one undo point for a batch that changes the record or
  monitoring mode, as for its other changes ("JPR:Change Record Mode" alone).

**Performance:** the record mode is never polled. REAPER calls
`SetSurfaceRecArm()` for a track whenever its record mode changes (see To
confirm), so the track re-reads it then, which also covers the rounds of every
track's state after a track list change or an undo. How a notification reaches
the track's cache, and whether a round reads each track or marks it to read on
its next use, is decided in CL3, with its calls bounded on the large project.
`GetRecArmGroup()` reads group membership on a press only, and is bounded on
the large project in the call count tests.

### scene: Enumerated track values and their actions

Today a press on an enumerated property is resolved by the mapping, which reads
the property and writes the result (`ViewMapping`, `press_toggles`). The track
actions need the press itself, to apply it to each selected track with Option,
and to know what Alt returns tracks to.

```
// What a press does to an enumerated value: toggles between min and max
// (`toggle`, and `set`, where they are equal), or steps through them.
struct EnumeratedPress {
  int min = 0;
  int max = 0;
  bool toggles = false;
  int count = 1;
  int Apply(int value) const;
};

class ViewProperty {
  // Applies a press. The default applies it to GetInt() and calls SetInt().
  virtual void PressInt(const EnumeratedPress& press);
};

// An enumerated value of a track, which the track actions read and set.
class TrackValue {
 public:
  virtual int GetMaxValue() const = 0;
  virtual int Get(const Track& track) const = 0;
  virtual void Set(TrackBatch& batch, Track* track, int value,
                   TrackGrouping grouping) const = 0;
};

class TrackActions {
  // Runs the press on the value of the track, with the modifiers in the class
  // comment, which generalize UiToggle()'s.
  void UiPress(Track* track, const TrackValue& value,
               const EnumeratedPress& press);
};
```

- `ViewMapping` calls `PressInt()` rather than resolving the press, and the
  `ui_` enumerated track properties override it to call `UiPress()`.
- `UiPress()` follows `UiToggle()`'s structure: anchor, Alt, Shift, Option,
  then the plain press. Alt returns every track past `press.min` to it. The
  class comment documents both.
- `track:rec_mode` and `track:monitor_mode` are `TrackValue`s over `Track`'s
  getters and `TrackBatch`'s setters.
- `track:rec_stage` is a `TrackValue` over rec arm and the record mode, in its
  own file, so it can be replaced by a config defined combination later:
  - Off disarms, and leaves the mode alone.
  - Monitor arms and sets `none`.
  - Record arms, and if the mode is `none`, sets the replaced mode or `input`.
  - With `kGrouped`, it sets every track in `GetRecArmGroup()`, each alone, so
    REAPER's grouping and the record mode reach the same tracks.
- Anchors: `TrackActions` gets an anchor for the record stage, and
  `track_anchor`'s `action` takes `rec_stage`. A ranged press copies the anchor
  track's value. Anchors for `rec_mode` and `monitor_mode` are left out until
  something uses them.

**Brittleness:** `UiPress()` and `UiToggle()` are parallel implementations of
the same rules. Once the record stage is in, the toggles could become
`TrackValue`s too, so there is one; that is a follow-up, not part of this
feature.

### plugin: The X-Touch example

```
components {  # At the top level.
  modifier mod:rec_anchor
}
components {  # On each strip's view, replacing user:anchor_rec_arm.
  track_anchor user:anchor_rec_stage(action = rec_stage,
                                     modifier = mod:rec_anchor)
}
mappings {
  strip.rec -> track:ui_rec_stage { toggle off .. monitor }
  strip.rec [double] -> track:ui_rec_stage { toggle monitor .. record }
  strip.rec [mod:rec_anchor] -> track:ui_rec_stage { toggle off .. monitor }
  strip.rec [held] -> user:anchor_rec_stage
  strip.rec [double, held] -> user:anchor_rec_stage
  strip.rec <- track:rec_arm {
    mode blink
    when track:rec_stage = monitor { mode solid }
  }
}
```

- `track_strip` keeps `strip.rec <-> track:ui_rec_arm` for the Info strip in
  Send/Receive mode, which has no anchors. Whether the Info strip gets record
  stages too is to decide in the CL.
- `xtouch.md` documents Rec, its light, and the double press modifiers.
- The user's `pursey.jprsurf` gets the same change, by hand.

### To confirm

In the test install, checked with temporary probes (CL1a):
- **Record modes.** REAPER's "Set track record mode to" actions set
  `I_RECMODE` to: 0 input, 1 output (stereo), 2 none (monitoring only), 3
  output (stereo, latency compensated), 4 MIDI output, 5 output (mono), 6
  output (mono, latency compensated), 7 MIDI overdub, 8 MIDI replace, 9 MIDI
  touch-replace, 10 output (multichannel), 11 output (multichannel, latency
  compensated), and 16 MIDI latch-replace. No action sets 12 to 15, the forced
  input formats ("Record: input (force format)") in REAPER's API docs.
  `SetMediaTrackInfo_Value()` accepts any number, -1 and 17 to 20 included,
  and reads it back unchanged, so the surface must only write known modes.
- **Monitoring.** `I_RECMON` is 0 off, 1 on, and 2 auto (tape), and
  `GetTrackState()`'s flags have &128 for 1 and &256 for 2, and neither for any
  other value, which it also accepts. The master's reads as 1, with no flag.
- **The project file.** The REC line is arm, input, monitoring, then mode:
  `REC 0 0 2 7 0 0 0 0` is monitoring auto, MIDI overdub.
- **Undo.** Neither setter adds an undo point, or makes the project dirty. An
  undo point from `Undo_OnStateChangeEx(UNDO_STATE_TRACKCFG)` holds the mode,
  the monitoring, and the arm, and undo and redo restore all three. A mode
  change with no point of its own is held by the next point, as other changes
  are. The actions add their own ("Set selected track recording mode", and
  "... recording monitor"), only when they change something.
- **Surface calls.** Setting `I_RECMODE` calls the track's state on the
  surface twice (`SetSurfaceRecArm()`, `SetSurfaceMute()`, `SetSurfaceSolo()`,
  `SetSurfaceVolume()`, `SetSurfacePan()`, and `CSURF_EXT_SETPAN_EX`), and
  setting `I_RECMON` once, even when nothing is armed. In a deferred refresh
  they are held until it ends, then sent once. The actions send it once, after
  a change. None of these calls says what the mode or monitoring is.
- **Monitoring with ganging.** `CSurf_OnInputMonitorChangeEx()` with
  `allowgang` sets every selected track to the same monitoring, if the track
  is selected, and never its groups (REAPER's groups have no monitoring). It
  returns the new value, sends the state of each track it changes, and adds no
  undo point, nor makes the project dirty. -1 isn't a toggle (1 went to 2, and
  2 stayed 2), so the surface never passes it. `I_RECMODE` is never grouped.
- **Rec arm groups.** Each group property has its own leaders and followers
  (`GetSetTrackGroupMembership()` with `RECARM_LEAD` and `RECARM_FOLLOW`, and
  `...High()` for groups 33 to 64, which work the same). A grouped rec arm
  that changes the track reaches every follower of a group the track leads,
  and on through each of those that leads a group of its own. A follower that
  doesn't lead reaches no one. With ganging, it reaches the selected tracks,
  if the track is selected, and each of their groups the same way.
- **Rec arm values.** Every track reached is set to the track's new value,
  and nothing changes if the track doesn't. The one exception: a follower that
  leads the group, when the track also follows it, is toggled instead. Groups
  made from the fake's `group` are all of this kind, which is why the fake
  toggles grouped tracks today.

In the user's REAPER:
- **Changes in REAPER's own UI** (a trace, 2026-11-07, of one track's record
  mode set to none then input, and its monitoring set off, on, then auto,
  disarmed and again armed). Every change called the track's state on the
  surface between runs: a record mode change twice, as its setter does, and a
  monitoring change once. Each round is the track's `SetSurfaceRecArm()`,
  `Extended(CSURF_EXT_SETINPUTMONITOR)` with the new monitoring, then its mute,
  solo, volume, and pan. With the setters and the actions, every way the mode
  or the monitoring changes calls `SetSurfaceRecArm()` for the track, so the
  surface needn't poll either (see Design). The test install's
  `RecordingSurface` leaves `CSURF_EXT_SETINPUTMONITOR` out, as about state the
  fake didn't hold.
- **Redraw after the API** (2026-11-07, a ReaScript that set `I_RECMODE` and
  `I_RECMON` inside `PreventUIRefresh(1)` and `(-1)`): REAPER redrew the
  track's monitoring each time, and on an armed track, its record mode on the
  arm button (which shows it only while armed), with nothing more. Klinke's
  MCU toggled rec arm twice after setting the record mode, as a "workaround
  for missing gui update", which isn't needed.

## CLs

### CL1a [x] common: The record and monitoring modes in the fake

Depends on: nothing.

- The findings in To confirm, from temporary probes (removed in this CL).
- `FakeTrack` holds the record mode and the monitoring, which the project file
  writes in its REC line, `GetMediaTrackInfo_Value()` reads, and
  `GetTrackState()`'s flags show. Every track's state in `SurfaceNotifier`
  sends `CSURF_EXT_SETINPUTMONITOR` with the track's monitoring, and
  `RecordingSurface` records it.
- Contract tests of the reads, the REC line, and the input monitor call.
- No new API functions.

**Verify**
- Standard checks.
- Contract tests (`check_in_reaper`).

### CL1b [ ] common: The setters of the record and monitoring modes

Depends on: CL1a.

- `SetMediaTrackInfo_Value`, `CSurf_OnInputMonitorChangeEx`, and
  `GetSetTrackGroupMembership` and `...High` on the API list, each in the fake,
  and in the cost table.
- Contract tests of the setters: the values they accept, their undo, and their
  surface calls, in and out of a deferred refresh; and the monitoring's
  ganging.

**Verify**
- Standard checks.
- Contract tests (`check_in_reaper`).
- Cost measurement of the new functions (main session, quiet machine).

### CL1c [ ] common: Rec arm groups in the fake

Depends on: CL1b.

- `FakeTrack`'s `group` becomes leaders and followers for each group
  property, in groups 1 to 64, read by `GetSetTrackGroupMembership()` and
  written in the project file's `GROUP_FLAGS`. Its uses in the tests move to
  it.
- The fake's rec arm reaches tracks as To confirm says, transitively through
  followers that lead, with the toggle of a follower that leads a group the
  track follows.
- Contract tests of the reach.

**Verify**
- Standard checks.
- Contract tests (`check_in_reaper`).

### CL2 [ ] device: Held presses beside a double press

Depends on: nothing.

- `Control`: a press delivered while the button is down stays pressed until
  release, for normal presses that time out of `kPendingRelease`, and second
  presses of a double press.
- The spec's `held`: a sentence on `double`.

**Verify**
- Standard checks.
- `control_test.cc`: `[held]` beside `[double]` is on from 150ms into a hold
  until release; `[double, held]` is on from the second press until release; a
  quick tap's deferred press is never held; long press behavior is unchanged.

### CL3 [ ] common: Record mode and monitoring mode on Track

Depends on: CL1c.

- `TrackRecMode`, `TrackMonitorMode`, and `Track`'s getters,
  `GetReplacedRecMode()`, and `RefreshRecMode()`.
- `TrackBatch::SetRecMode()` and `SetMonitorMode()`, and `GetRecArmGroup()`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- `track_test.cc` against the fake: caching, the replaced mode, grouping
  through `GetRecArmGroup()`, and one undo point per batch.

### CL4 [ ] scene: Enumerated track values, rec_mode, and monitor_mode

Depends on: CL3.

- `EnumeratedPress`, `ViewProperty::PressInt()`, and `ViewMapping` calling it.
- `TrackValue`, `TrackActions::UiPress()`, and its class comment.
- `track:rec_mode`, `track:monitor_mode`, and their `ui_` forms.
- The generated scene pages (`update_user_docs`).

**Verify**
- Standard checks.
- `view_mapping_test.cc`: presses reach `PressInt()` with their range.
- `track_actions_test.cc`: every modifier rule on an enumerated value,
  including Option applying the press to each track, and Alt's order.
- `track_properties_test.cc`: the properties, and a change made in REAPER
  read on its notification, with no polling.

### CL5 [ ] scene: The record stage

Depends on: CL4.

- `track:rec_stage` and `track:ui_rec_stage`, the record stage anchor, and
  `track_anchor`'s `rec_stage` action.
- The generated scene pages.

**Verify**
- Standard checks.
- Unit tests of the record stage's reads and writes: disarming leaves the mode,
  monitor sets `none`, record restores the replaced mode or `input`, grouping
  reaches `GetRecArmGroup()`, and a range copies the anchor's stage.

### CL6 [ ] plugin: Record stages in the X-Touch example

Depends on: CL2, CL5.

- `xtouch.jprsurf` and `xtouch.md` as in Design.
- Surface tests in `src/jpr/plugin/xtouch_config`: the press, double press,
  light, Alt + Ctrl double press, and ranges from a hold and a double press
  hold.
- Call count bounds for the steady state with armed tracks, and each new
  gesture, in `call_count_test.cc`.

**Verify**
- Standard checks.
- Load check (`check_load_in_reaper`).
- Benchmark of a track list change, which now reads record modes.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow). This
  includes Option 2: a config component that defines combinations of track
  properties, to replace the built in record stage if another combination is
  wanted.

## Checks in REAPER

Copy the Rec mappings from `xtouch.jprsurf` into `pursey.jprsurf`, then:
- Press Rec on a disarmed track: it arms, REAPER shows "Record: disable", and
  the light is solid. Press again: it disarms, and the mode stays "disable".
- Double press: the light blinks, and the mode is back to what it was (try a
  MIDI overdub track), or "input".
- With several tracks recording and some monitoring, Alt + Ctrl double press on
  one: only it records, and the others still monitor, armed.
- Hold Rec, and press Rec on another track; then double press and hold, and do
  the same. Each range gets the held track's stage.
- Grouped and selected tracks follow a press, mode included.
- A Rec press feels no slower than it should (up to 150ms after release).
- The extension loads with no new errors in `jprsurf.log`, "Slow run" warnings
  included, and the idle profile, with a few armed tracks shown, doesn't
  regress against `profiles/`.
