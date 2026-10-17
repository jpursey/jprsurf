# Report touch to REAPER for automation

REAPER asks each control surface whether a track's volume or pan is touched
(`IReaperControlSurface::GetTouchState(track, isPan)`), to know when Touch and
Latch automation should record. JPRSurf used to always answer no, so REAPER
took each move as a brief touch: a fader recording in Touch mode kept dropping
back to the envelope between moves.

Now a track's volume or pan is touched for as long as a hand is on a control
mapped to its touch. Touch automation records for exactly as long as an X-Touch
fader is held, still or moving, and returns to the envelope the moment it is
let go. The pots, and the extender fader whose touch sensing is broken, stay
touched for 3 seconds after their last move, as Klinke does
(`CSurf_MCU::GetTouchState()`).

Along the way, a `press_release` mapping learned to release what it holds when
it stops reading, a property's write may no longer change views, and tracks are
held by plain pointer, as `TrackCache` keeps every one.

## Behavior

| Control                         | Reports touched                                          |
| ------------------------------- | -------------------------------------------------------- |
| Strip fader                     | Its track's volume, while the fader is touched           |
| Strip fader without touch       | Its track's volume, for 3 seconds after its last move    |
| Master fader                    | The master track's volume, while it is touched           |
| Strip pot                       | Its track's pan, for 3 seconds after its last turn       |
| Info strip in Send/Receive      | Its track's volume and pan, as a strip in Track mode     |
| Route faders and pots           | Nothing: REAPER never asks about routes                  |

- A strip whose track changes while its fader is held (a bank, a channel move,
  or the list scrolling) moves the touch to its new track. A strip that stops
  showing a track (switching to Send/Receive mode) releases it.
- Touch is reported for the track a strip shows. Grouped and ganged tracks that
  REAPER changes with it record and return together with it, which REAPER does
  on its own.
- Width (`isPan` 2) isn't reported: `CSURF_EXT_SUPPORTS_EXTENDED_TOUCH` still
  answers 0.
- Sends and receives aren't reported. Their envelopes behave as in Latch (see
  the backlog's *Report touch for sends and receives*).

## REAPER facts

From a trace in the user's REAPER (2026-10-17), with `GetTouchState` traced
and extended touch answered for the run. The calls are in "Seen in traces" in
`docs/testing_and_profiling.md`.
- Once the surface has changed a track's volume while playing, REAPER asks the
  master and every track, whatever its automation mode, for its volume (0) and
  pan (1), and width (2) for a surface with extended touch, before every run
  from then on, playing or stopped. It never asks from inside another call.
  When a change starts a new write, it also asks that track right after the
  run. So answering is per-run work: 258 questions a run on 128 tracks.
- It never asks about sends or receives. Their envelopes, set with `isend` 0,
  hold their last value in Touch, as in Latch.
- Not settled: whether touching a fader without moving it starts Latch
  recording, or only the first move does. The X-Touch's faders are sensitive
  enough that the user couldn't tell a touch from a tiny move, or whether the
  X-Touch sends a value after a touch anyway. It doesn't affect the design. A
  trace of a touch with no move would show whether JPRSurf sent REAPER a volume
  change at all.
- A write that starts recording automation makes REAPER's next refresh (the end
  of the run's deferred refresh) take 13–17ms, with 0.3–0.6ms in
  `CSurf_OnVolumeChangeEx()` or `CSurf_OnPanChangeEx()`. These show as "Slow
  run" warnings while recording, and are REAPER's own time: JPRSurf's in those
  runs was 42–79us.

## Structure

### common: track touch (track.h/.cc, track_cache.h/.cc)

- `TrackTouchType` (`kVolume`, `kPan`), and `Track::RegisterTouch(type)`, which
  returns a move-only `TrackTouchHandle` that holds the touch until it is
  destroyed or assigned over. A track counts its touches by type, so it stays
  touched until every handle is gone. `Track::IsTouched(type)` reads it, and
  `TrackListener::OnTrackTouchChanged(track, type)` reports a type becoming
  touched or untouched, so a touch doesn't notify every property of the track.
- `TrackCache::GetTouchedTracks()` lists the tracks with any touch: almost
  always none, and at most one per touch mapping. A track adds itself when it
  becomes touched, and removes itself when its last touch goes.
- `ControlSurface::GetTouchState()` compares REAPER's track ID with each
  touched track's, rather than looking the track up, and isn't logged. A null
  track, TrackCache's stub (which a strip with no track may touch), is never
  touched, and neither is any `isPan` but 0 and 1. `ControlSurfaceListener`
  doesn't change, and the plugin keeps no touch state.

### device: touch registrations (control.h/.cc, control_input_handle.h)

`InputConfig::touch` registers for whether a hand is on a control, read as a
press with release (`IsPressed()` and `GetPressCount()`):
- A control whose press input has release (`HasPressRelease()`) is touched
  while it is pressed, as a touch sensitive fader is.
- Any other control with input is touched from an input until
  `Control::kTouchHoldSecs` (3s) after its last. The hold starts on the
  control's run after the input, so it never depends on the time of a run from
  before the control was idle.
- Touch registrations are kept apart from the others (`touch_flags_`), so they
  ignore modifiers, and stay out of press groups and press behaviors. A control
  only follows its touch while it has one, and a registration made while the
  control is touched starts pressed, with its flag set.
- Which inputs a control listens to comes from one place, `IsInputRead()`,
  which takes touch into account, and a listener is only set or cleared when
  that changes.

A device doesn't know what its controls are used for, so touch is worked out
from whatever inputs a control has, and any control can be mapped to it (a jog
wheel, or a MIDI Fighter Twister's encoders). A pushable encoder must stay two
controls, as `control.h` recommends, or its push would count as touch.

### scene: touched properties, and releases (view_mapping.h/.cc, track_properties.h/.cc, view.h/.cc)

- `ReadConfig::touch` passes through to `InputConfig::touch`, and reads touch
  as a press with release on any control with input, ignoring `input_type`.
- `track:volume_touched` and `track:pan_touched` hold a `TrackTouchHandle` on
  their track while true. The touch moves with the property to another track,
  through a new no-op `TrackProperty::OnTrackSet()` hook, and goes with the
  property. Reading is whether anything touches the track. Each
  `TrackProperties` (one per reference) has its own, so two strips on one track
  each hold a touch, and only the touched property of the changed type is
  notified.
- **Releasing when reading stops:** a `press_release` mapping that stops
  reading while it holds a press (its view deactivates, or it is disabled or
  destroyed) applies the release then, by reading its unregistered input, which
  `Control::IsPressed()` reads as released. Before, the press was forgotten, so
  a modifier released while its view was inactive stayed on. A press that
  deactivates its own mapping is a configuration error.
- **A write may never change views:** that release is written while views are
  being refreshed, so `ViewProperty` documents that a write may only change
  what a condition reads, which the scene applies on its next run, and
  `View::Enable()`, `Disable()`, and `RefreshActive()` log an error and do
  nothing while a refresh is in progress (`Scene::refreshing_views_`).

### plugin: mappings (plugin_surface.cc)

`AddTouchMapping(view, property, control)` maps a control's touch to a touched
property (`{.press_release = true, .touch = true}`). `AddTrackStripMappings()`
maps each strip's fader to `track:volume_touched` and pot to
`track:pan_touched`, which covers every strip in Track mode and the Info strip
in Send/Receive mode, and the master fader's view maps it to the master's
`track:volume_touched`. The route strips map none.

### Tracks held by pointer

`TrackCache` keeps every `Track` for as long as the cache exists, and only tests
destroy the cache, so tracks are owned by `unique_ptr`, `TrackProperties` and
`RouteProperties` hold a `Track*`, and `Track` dropped
`enable_shared_from_this`, `GetShared()`, and `GetWeak()`.

## Building blocks

- **Touch on any control:** `InputConfig::touch`, or `ReadConfig::touch` in a
  mapping, reads a hand on a control whatever its inputs. With
  `press_release` into a bool property, a touch can drive anything, such as
  showing a value while a fader is held.
- **Holding something on a track:** a move-only handle from
  `Track::RegisterTouch()` is the pattern for state many holders share on a
  track, with counts, and a listener callback only on the change.
- **Properties that follow their track:** `TrackProperty::OnTrackSet()` lets a
  property move what it holds when its reference moves to another track.
- **Test helpers:** `SurfaceTest::IsTouched()` and `AskEveryTouch()` ask what
  REAPER asks (`AskEveryTouch()` goes to the surface itself, so a benchmark
  times only the answers), and `RunPlaying()` runs while playing.

## Performance

- **Steady state:** the 35 touch mappings sync every run and do nothing until
  a touch. Benchmarked back to back, runs rose about 0.1–0.2us (2–3%). The idle
  profile in the user's REAPER rose 27.5 → 29.4us a run, within the noise
  between REAPER sessions, with identical call counts.
- **While automating:** the benchmark `IdleWhileAutomating` asks every track
  before each run while playing, with a fader held, its answers in a
  `ControlSurface::GetTouchState` point. Answering from a hash lookup cost 4.8us
  a run on 128 tracks. Comparing against the touched tracks instead costs about
  0.77us. Inlining `TrackCache::Get()` by hand measured no different, so it
  wasn't kept (see the backlog's *Measure inlining and link-time optimization
  for the Release build*).
- **Answering makes no REAPER calls**, which `call_count_test.cc` checks for
  every track.
- The bars in `profiles/` were replaced at the end of the feature.
