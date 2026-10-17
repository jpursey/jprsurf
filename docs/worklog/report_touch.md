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
| Route faders and pots           | Nothing: REAPER asks only about a track's volume and pan   |
| Info strip in Send/Receive      | Its track's volume and pan, as a strip in Track mode       |

- A control reports touch only while its mapping is active. A strip whose
  track changes while it is touched (banking, or the list scrolling) moves its
  touch to the new track, and a mapping that stops reading (its view
  deactivates, such as switching to Send/Receive mode) releases it.
- Touch is reported for the track the mapping names, and not for grouped and
  ganged tracks that REAPER changes with it (see To confirm).
- Sends and receives aren't reported: REAPER never asks about them (see the
  backlog's *Report touch for sends and receives*).
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

REAPER asks about every track's volume and pan before every run (see To
confirm), so answering is per-run work: each answer is a hash lookup and a
read, with nothing walking mappings or tracks. Otherwise nothing new runs per
run but a time comparison in controls that already run.

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
  // becomes touched or untouched (see TrackListener::OnTrackTouchChanged()).
  TrackTouchHandle RegisterTouch(TrackTouchType type);
};

class TrackListener {
  ...
  // Called whenever the track becomes touched or untouched for the type.
  virtual void OnTrackTouchChanged(Track* track, TrackTouchType type) {}
};
```

- A touch count per type on `Track`. The handle holds a `Track*`, as TrackCache
  never destroys a track.
- `ControlSurface::GetTouchState()` finds the `Track` with
  `TrackCache::GetTrack()`, and answers `IsTouched()`, with `isPan` 0 as the
  volume and 1 as the pan. A track TrackCache doesn't have, or any other
  `isPan`, is not touched. `ControlSurfaceListener` doesn't change.
- Performance: a hash lookup and a read when REAPER asks, with no REAPER
  calls. REAPER asks twice per track before every run, so on a 128-track
  project this is 258 answers a run, and must stay that cheap (no logging on
  the path, as the existing `VLOG_REAPER()` would be at that rate). A touch
  changing notifies the track's listeners with their own callback, rather
  than `OnTrackChanged()`, which would notify every one of the track's
  properties. It happens about as often as a hand reaches for a control.

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
  `Track::IsTouched()`, whoever holds the touch, and
  `TrackProperties::OnTrackTouchChanged()` notifies only the touched
  properties.
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
while views are being refreshed. A release that changes a view condition (a
modifier) only sets the condition's flag, and the scene applies it on its next
run. A property's write may never change views directly (documented on
`ViewProperty`), and the scene enforces it: `View::Enable()`, `Disable()`, and
`RefreshActive()` log an error and do nothing while a refresh is in progress.
Deferring such a change instead is a backlog item. The release reads the
unregistered input, which `Control::IsPressed()` reads as released, so each
read function applies it as it applies a release.

### plugin: touch mappings

Mappings only, through the scene's config:
- Each strip that shows a track (`AddTrackStripMappings()`: every strip in
  Track mode, and the Info strip in Send/Receive mode): its fader `{.read =
  {.press_release = true, .touch = true}}` to `track:volume_touched`, and its
  pot the same to `track:pan_touched`.
- The master fader: the same to `track:volume_touched` of its view's subject,
  the master track.

The route strips in Send/Receive mode map none, so they report nothing.

### To confirm

Checked in the user's REAPER with a trace (CL1, 2026-10-17), on a 7-track
project with tracks in Touch, Latch, a volume group, and Read, and a send with
volume and pan envelopes. The calls are in "Seen in traces" in
`docs/testing_and_profiling.md`.

- [x] How often REAPER calls `GetTouchState()`, and when: before every run,
      never from inside another call, once the surface has changed a track's
      volume while playing, and from then on whether playing or stopped. When
      a run's change starts a new write on a track in Touch, REAPER also asks
      that track right after the run. So answering is per-run work: on a
      128-track project, 258 questions a run. The design's answer, a hash
      lookup and a read, stays cheap, and CL5 adds the questions to the
      benchmark.
- [x] For which tracks: the master and every track, in order, whatever its
      automation mode (Trim and Read too).
- [x] Which `isPan` values it asks with: 0 and 1, and 2 only for a surface
      that supports extended touch. No other value, so routes have none.
- [x] For a fader moved on a grouped or ganged track: REAPER asks about every
      track anyway, so the surface could answer for grouped tracks too. With
      every answer false, the trace can't show what a grouped track's envelope
      does when only the moved track is touched. The design reports only the
      track a mapping names, and the end checks look at the grouped track; if
      its envelope drops back while the moved one records, reporting grouped
      tracks is a follow-up.
- [ ] Whether touching a fader without moving it starts Touch or Latch
      recording, or only a change does. Every answer was false, so this waits
      for the end checks. It only affects what the user sees, not the design.
- [x] How REAPER knows a send or receive is touched: not through
      `GetTouchState()`, which is never asked about a route, even with
      extended touch. Moved with `isend` 0, the send's volume and pan
      envelopes on a track in Touch kept their last value after the change
      stopped, as in Latch. The likely signal is the end of the edit (`isend`
      1), which JPRSurf never sends. It needs its own design (when an edit
      ends, and its undo point beside `ContinuousUndo`), so it is the backlog
      item *Report touch for sends and receives*.

## CLs

### CL1 [x] common: Confirm when REAPER asks for touch

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

### CL2 [x] common: Track touch answers GetTouchState()

Depends on: CL1.

- `TrackTouchType`, `TrackTouchHandle`, `Track::RegisterTouch()` and
  `IsTouched()`, and `TrackListener::OnTrackTouchChanged()`.
- `ControlSurface::GetTouchState()` answers from the track.
- Nothing touches a track yet, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `track_test.cc`: a handle touches its type and not the other, two handles
  keep the track touched until both are gone, moving and resetting a handle
  move and drop its touch, a deleted track keeps its touch, and listeners
  hear `OnTrackTouchChanged()` only when the track becomes touched or
  untouched, and never `OnTrackChanged()`.
- `control_surface_test.cc`: a touched track and the master answer true for
  their type, and an untouched track, a track TrackCache doesn't have, or an
  `isPan` of 2 answer false.

### CL3 [x] device: Touch registrations

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

### CL4 [x] scene: Touched properties, and releasing on deactivation

Depends on: CL2 and CL3.

- `ReadConfig::touch`, and the read functions taking it as a press with
  release.
- `track:volume_touched` and `track:pan_touched`, as in the design.
- `ViewMapping` writes a held `press_release` release when it stops reading.
- `View` refuses, with an error, being enabled, disabled, or refreshed while
  the scene's views are being refreshed, and `ViewProperty` documents that a
  write may never change views.

**Verify**
- Standard checks.
- `track_properties_test.cc`: writing a touched property touches its track,
  false releases it, two properties on the same track hold their own touches
  and read each other's, changing the track moves the touch, and destroying
  the properties releases it.
- `view_mapping_test.cc`: a touch mapping on a `FakeDevice` reads a button's
  press with release, and a fader's and a pot's input for the hold, ignoring
  the input type, and a control with no input reads none; a `press_release`
  mapping holding a press, or a touch, releases it when its view
  deactivates, and its release, read while the view is inactive, changes
  nothing; and a release that changes a view's condition changes which views
  are active on the next run; a release whose write enables a view is refused
  with an error, and the same write outside a refresh enables it. Disabling
  and destroying a mapping release it through the same path
  (`RefreshActive()`), which tests can't reach directly.

### CL5 [x] plugin: Report touch to REAPER

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
- A new benchmark, `IdleWhileAutomating`, asks every track's volume and pan
  before each run while playing, as REAPER does, with their answers in a
  `ControlSurface::GetTouchState` point. JPRSurf's time per run doesn't
  regress against the bar, and the new benchmark's is compared against
  `IdleWhilePlaying`'s.
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
- **Latch:** touching a fader starts recording, or the first move does (the
  open question in To confirm), and it continues after the release until
  playback stops.
- **No touch sensing:** the pots, and the extender's third fader, record in
  Touch mode, and return to the envelope about 3 seconds after the last
  move.
- **Grouped tracks:** moving a fader on a grouped track in Touch records the
  other track's envelope for as long as the moved one's. If it drops back to
  its envelope between changes instead, reporting grouped tracks goes in the
  backlog.
- **Send/Receive mode:** moving a send fader records as before (as in Latch).
- The load check passes, and once REAPER has created the surface,
  `jprsurf.log` has no new errors or "Slow run" warnings.
- An idle profile, after a fader has moved while playing so REAPER asks for
  touch before every run, doesn't regress against the bar.
