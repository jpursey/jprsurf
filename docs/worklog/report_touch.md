# Report touch to REAPER for automation

REAPER asks each control surface whether a track's volume or pan is touched
(`IReaperControlSurface::GetTouchState(track, isPan)`), to know when Touch and
Latch automation should record. JPRSurf never answers, so REAPER takes each
move as a brief touch: a fader recording in Touch mode keeps dropping back to
the envelope between moves, as the user found at the end of *Fake X-Touch and
device tests*.

With this feature, a track's volume or pan is touched for as long as a hand is
on a control mapped to its touch: while a touch sensitive control (an X-Touch
fader) is touched, and for a control without touch (an X-Touch pot, or the
extender fader whose touch sensing is broken), until a short time after its
last input. Touch automation then records for exactly as long as the fader is
held, and Latch starts at the first touch, as they do with REAPER's own
faders. Klinke does the same (`CSurf_MCU::GetTouchState()` in
`csurf_mcu.cpp`), with 3 seconds for the pots.

## Behavior

| Control                         | Reports touched                                            |
| ------------------------------- | ---------------------------------------------------------- |
| Strip fader (Track mode)        | Its track's volume, while the fader is touched             |
| Strip fader without touch       | Its track's volume, for 3 seconds after its last move      |
| Master fader                    | The master track's volume, while it is touched             |
| Strip pot (Track mode, pan)     | Its track's pan, for 3 seconds after its last turn         |
| Faders and pots in Send/Receive | Nothing: REAPER asks only about a track's volume and pan   |

- A control reports touch only while its mapping is active. A strip whose
  track changes while it is touched (banking, or the list scrolling) moves its
  touch to the new track, and a mapping that stops reading (its view
  deactivates, such as switching to Send/Receive mode) releases it.
- Touch is reported for the track the mapping names. Grouped and ganged tracks
  that REAPER changes with it follow what To confirm finds.
- Width (`isPan` 2, which REAPER only asks a surface that supports extended
  touch) stays unsupported: `CSURF_EXT_SUPPORTS_EXTENDED_TOUCH` still answers
  0.

## Design

Touch is something a `Control` works out from whatever inputs it has, which a
mapping asks for: a device doesn't know what its controls will be used for,
and any control can be touched (a jog wheel mapped to the master volume, or a
MIDI Fighter Twister's encoders). A mapping reads it as a press with release
into a track's touched property, which holds a touch on the track. REAPER's
question is answered from the track, in `common`, so the plugin only adds
mappings.

Nothing new runs per run but a time comparison in controls that already run,
and nothing walks mappings or tracks when REAPER asks.

### Names

| New name                                          | Alternatives                       | Existing names it might be confused with |
| ------------------------------------------------- | ---------------------------------- | ---------------------------------------- |
| `TrackTouchType` (`kVolume`, `kPan`)              |                                    | `TrackBoolProperty`, a `ViewProperty`-like list it isn't |
| `Track::RegisterTouch()`, `TrackTouchHandle`      | `Track::Touch()`                   | `ControlInputHandle`, which it is modeled on |
| `Track::IsTouched()`                              |                                    | REAPER's last touched track (`TrackCache::GetLastTouchedTrack()`), which is about selection |
| `InputConfig::touch`, `ReadConfig::touch`         | `read_touch`, `from_touch`         | `ControlInput::Type::kPress`, which touch reads as |
| `Control::kTouchHoldSecs`                         |                                    | Long press holds, and `ViewMapping::holding_` |
| `track:volume_touched`, `track:pan_touched`       |                                    | `track:volume`, `track:pan`              |

"Touch" is REAPER's own word for this, and the last touched track is the only
existing use of it.

### common: Track touch, and GetTouchState()

```
// What REAPER asks a surface about while recording Touch and Latch automation.
enum class TrackTouchType { kVolume, kPan };

// RAII handle for a touch on a track (see Track::RegisterTouch()). Destroying
// it, or assigning another, removes the touch.
class TrackTouchHandle { ... };

class Track {
  ...
  // Returns true if anything holds a touch of the type on the track.
  bool IsTouched(TrackTouchType type) const;

  // Touches the track until the handle is destroyed. Several handles may
  // touch the same track at once. Listeners are notified when the track
  // becomes touched or untouched.
  TrackTouchHandle RegisterTouch(TrackTouchType type);
};
```

- A touch count per type on `Track`. The handle holds a `shared_ptr` to the
  track, so a touch can't outlive it.
- `ControlSurface::GetTouchState()` finds the `Track` with
  `TrackCache::GetTrack()`, and answers `IsTouched()`, with `isPan` 0 as the
  volume and 1 as the pan. A track TrackCache doesn't have, or any other
  `isPan`, is not touched. `ControlSurfaceListener` doesn't change.
- Performance: a hash lookup and a read when REAPER asks, with no REAPER
  calls. A touch changing notifies the track's listeners, which happens about
  as often as a hand reaches for a control.

**Brittleness:** none left to callers: a touch is only held through a handle.

### device: touch registrations

```
struct InputConfig {
  ...
  // If true, the registration reads the control's touch as a press with
  // release, whatever inputs the control has, and input_type and
  // press_behavior are ignored. A control with a Press input that has release
  // alongside a Value or Delta input is touched while it is pressed. Any other
  // control with input is touched from an input until kTouchHoldSecs after
  // its last input. Touch is outside press groups and modifier exclusivity
  // (see required_modifiers), so a hand on the control is always reported.
  bool touch = false;
};

class Control {
  ...
  // How long a control without touch sensing stays touched after its last
  // input.
  static constexpr double kTouchHoldSecs = 3.0;
};
```

- `IsPressed(id)` and `GetPressCount(id)` work for a touch registration as for
  a press, so the scene reads it as it reads a press with release.
- The `Control` tracks touch only while it has a touch registration. The hold
  is released in its run, which it already has while it has registrations,
  with the run's time, so tests control it.
- A pushable encoder must stay two controls (as `control.h` already
  recommends), or its push would count as touch.
- Performance: a comparison of the last input's time per run, for a control
  with a touch registration in its hold.

### scene: touched properties, and releasing on deactivation

- `ReadConfig::touch` passes through to `InputConfig::touch`, and the read
  functions take it as a press with release, so `press_release` works as it
  does for a press: true on touch, false on release.
- `track:volume_touched` and `track:pan_touched` are bool track properties.
  Writing true holds a `TrackTouchHandle` on the property's track, and false
  drops it. When the property's track changes, a held touch moves to the new
  track (through a no-op protected virtual on `TrackProperty`, called by
  `SetTrack()`), and destroying the property drops it. Reading returns
  `Track::IsTouched()`, whoever holds the touch.
- Each `TrackProperties` (one per reference) has its own touched properties,
  so two strips on the same track each hold a touch. Two mappings writing the
  same reference's touched property share one handle, so the first release
  drops it: a config error, like two mappings to one toggle.
- **Releasing on deactivation:** a `press_release` mapping that is holding a
  press when it stops reading (its view deactivates, or it is disabled or
  destroyed) writes its release. Today it forgets the press, so a modifier
  released while its view is inactive stays on. A press that deactivates its
  own mapping would have it released at once, which is a config error.
- Performance: nothing per run. A touch or release is one write.

**Brittleness:** writing the release from `ViewMapping::RefreshActive()` runs
during view activation. A release that changes a view condition (a modifier)
must not activate or deactivate views from inside it. CL4 checks how
`Scene::ApplyViewConditions()` handles it, and defers the release to the next
`Sync()` if it can't.

### plugin: touch mappings

Mappings only, through the scene's config:
- Each strip fader in Track mode: `{.read = {.touch = true, .press_release =
  true}}` to `track:volume_touched`.
- Each strip pot where it maps pan: the same to `track:pan_touched`.
- The master fader: the same to `state:master_track.volume_touched`.

Send/Receive mode maps none, so it reports nothing.

### To confirm

Checked in the user's REAPER with a trace (CL1), as these are calls REAPER
makes on a surface between runs. `GetTouchState` is one of the trace's quiet
calls, so the check makes it traced for the run.

- [ ] How often REAPER calls `GetTouchState()`, and when: only while playing or
      recording, every run or more, and between runs or inside
      `CSurf_OnVolumeChangeEx()` and `CSurf_OnPanChangeEx()`.
- [ ] For which tracks: only those in Touch or Latch (and Latch Preview), only
      those the surface changed, or every track; and whether the master is
      asked.
- [ ] Which `isPan` values it asks with, with extended touch unsupported (0
      and 1 expected).
- [ ] For a fader moved on a grouped or ganged track, whether REAPER asks about
      the other tracks it changes. If it does, whether their envelopes should
      follow the touch of the track the surface moved is decided before CL4.
- [ ] Whether touching a fader without moving it starts Touch or Latch
      recording, or only a change does. It only affects what the user sees in
      the end checks, not the design.
- [ ] How REAPER knows a send or receive is touched, for its volume and pan
      envelopes in Touch and Latch. `GetTouchState()` has no route index, but
      extended touch ("isPan=2 for width, etc" in `reaper_plugin.h`) may ask
      with other values, so CL1 temporarily answers
      `CSURF_EXT_SUPPORTS_EXTENDED_TOUCH` with 1 to see every value REAPER
      uses. The other likely signal is the edit in `SetTrackSendUIVol()` and
      `SetTrackSendUIPan()`: `isend` 0 while tweaking, and 1 at the end of the
      edit, which JPRSurf never sends. Also what a send envelope in Touch mode
      does today when a send fader moves and stops: whether it returns to the
      envelope, after how long, or keeps writing until playback stops.
      Reporting route touch, whichever signal it turns out to be, is decided
      after CL1: added to this plan if it is small, or a backlog item if not.
      Ending an edit with `isend` 1 adds its own undo point, which
      `ContinuousUndo` must then not repeat.

## CLs

### CL1 [ ] common: Confirm when REAPER asks for touch

Depends on: nothing.

- Temporarily, `GetTouchState` is taken out of the trace's quiet calls
  (`reaper_trace.cc`), and `OnSupportsExtendedTouch()` answers true. With
  `JPRSURF_TRACE` set, the user plays the project with a few tracks in Touch,
  one in Latch, two in a volume group, and the rest in Read, and with send
  volume and pan envelopes on a track in Touch. They move a strip fader, a
  pot, and the master fader on the X-Touch, then a send's fader and pot in
  Send/Receive mode, letting go of each for a few seconds, and close REAPER.
- The trace shows each question in To confirm. The findings go in To confirm
  and in "Seen in traces" in `docs/testing_and_profiling.md`. Nothing in the
  fake changes: REAPER's question has no state in it, and surface tests ask it
  as REAPER does, with `GetSurface()->GetTouchState()`.
- Commits only the findings, with the quiet call and extended touch restored.

**Verify**
- The trace has `GetTouchState` calls, and the findings are recorded.
- `git diff` shows only docs.

### CL2 [ ] common: Track touch answers GetTouchState()

Depends on: CL1.

- `TrackTouchType`, `TrackTouchHandle`, and `Track::RegisterTouch()` and
  `IsTouched()`.
- `ControlSurface::GetTouchState()` answers from the track.
- Nothing touches a track yet, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `track_test.cc`: a handle touches its type and not the other, two handles
  keep the track touched until both are gone, moving and resetting a handle
  move and drop its touch, and listeners are notified only when the track
  becomes touched or untouched.
- `control_surface_test.cc`: a touched track and the master answer true for
  their type, and an untouched track, a track TrackCache doesn't have, or an
  `isPan` of 2 answer false.

### CL3 [ ] device: Touch registrations

Depends on: nothing.

- `InputConfig::touch` and `Control::kTouchHoldSecs`, as in the design.
- Nothing registers for touch yet, so no visible change.

**Verify**
- Standard checks.
- `control_test.cc`: with a Press input that has release, a touch
  registration is pressed from the press to the release, whatever the value
  input does; without one, from its first input until kTouchHoldSecs after
  its last, with a Value or a Delta input; a control with no input never is;
  touch registrations ignore modifiers, and don't change press groups or
  press behaviors of press registrations on the same control.
- `device_xtouch_test.cc`: on the fake X-Touch, a strip fader and the master
  fader are touched from `TouchFader()` until the release, and a pot and the
  extender's third fader for kTouchHoldSecs after a turn or a move.

### CL4 [ ] scene: Touched properties, and releasing on deactivation

Depends on: CL2, CL3, and To confirm's grouping finding.

- `ReadConfig::touch`, and the read functions taking it as a press with
  release.
- `track:volume_touched` and `track:pan_touched`, as in the design.
- `ViewMapping` writes a held `press_release` release when it stops reading.

**Verify**
- Standard checks.
- `track_properties_test.cc`: writing a touched property touches its track,
  false releases it, changing the track moves the touch, and destroying the
  properties releases it.
- `view_mapping_test.cc`: a touch mapping on a `FakeDevice` sets and clears
  its property; a `press_release` mapping holding a press releases it when its
  view deactivates, when it is disabled, and when it is destroyed, and a
  modifier released while its view is inactive doesn't stay on.
- `scene_test.cc`: a reference field (`state:master_track.volume_touched`)
  touches its track, and a release that changes a view condition activates
  and deactivates views correctly.

### CL5 [ ] plugin: Report touch to REAPER

Depends on: CL4.

- The touch mappings in `PluginSurface`, as in the design.

**Verify**
- Standard checks.
- Surface tests in `src/jpr/plugin/default_config` (`track_strip_test.cc`,
  `send_mode_test.cc`, `global_test.cc`), with `GetTouchState()` asked as
  REAPER does: a touched strip fader reports its track's volume until
  released, and not the pan; the extender's third fader and a turned pot
  report for kTouchHoldSecs; banking while a fader is touched moves the touch
  to its new track; the master fader reports the master; switching to
  Send/Receive mode while a fader is touched releases it, and there touching
  a fader or turning a pot reports nothing.
- `call_count_test.cc`: touching a fader and `GetTouchState()` make no REAPER
  calls.
- The load check.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow).

## Checks in REAPER

In the user's REAPER, with the extension deployed, while playing:
- **Touch:** holding a strip fader still, then moving it, then holding it
  again, records one unbroken stretch of envelope, which returns to the
  existing envelope only when the fader is released. The same for the master
  fader.
- **Latch:** touching a fader starts recording (or the first move does, as To
  confirm found), and it continues after the release until playback stops.
- **No touch sensing:** the pots, and the extender's third fader, record in
  Touch mode, and return to the envelope about 3 seconds after the last
  move.
- **Grouped tracks:** moving a fader on a grouped track records the group's
  envelopes as To confirm found REAPER does.
- **Send/Receive mode:** moving a send fader records nothing new, as before.
- The load check passes, and once REAPER has created the surface,
  `jprsurf.log` has no new errors or "Slow run" warnings.

No per-run work changes beyond a comparison in controls that already run, so
there is no benchmark or idle profile.
