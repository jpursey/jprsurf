# Fake REAPER

A test-only library, `jpr/common/testing`, that holds REAPER's state in memory
and loads it as the REAPER API, so code that depends on REAPER is unit tested,
and verified without running REAPER. With it came the tests of `common`, and
the testing rules in CLAUDE.md: a change is verified by its tests, and REAPER
is run only to check behavior nobody has checked, for performance, and at the
end of a feature (see Checking in REAPER in CLAUDE.md). The design it came from
is in [testing_and_profiling.md](../testing_and_profiling.md) (Time, Fake
REAPER).

## Behavior

Nothing changes on the surface. Two changes to the plugin made it testable:
every run has one time, read from REAPER's clock once, and the process state in
`common` can be reset by the fake. With the second, the plugin, rather than
`common`, says where the profile is written.

## Names

| Name                              | What                                                                       | Might be confused with                                                                             |
| --------------------------------- | -------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------- |
| `FakeReaper`                      | REAPER's state in memory, loaded as the API. One at a time.                | `ReaperTrace` and `ReaperProfiler`, which hook the API rather than implement it                    |
| `FakeProject`                     | One open project tab: its tracks, routes, transport, and undo              | REAPER's `ReaProject*`, which points to one (`ToReaProject()`)                                     |
| `FakeTrack`                       | A track's own values, which a test sets. A `MediaTrack*` points to one.    | `Track`, `common`'s cached view of a track; the file-local `FakeTrack` in `reaper_trace_test.cc`   |
| `FakeRoute`                       | A send (and the receive at its other end), or a hardware output            | `TrackRoute`, `common`'s cached view of one                                                        |
| `FakeCommand`                     | An action a test adds: its ID, text, name, toggle state, and handler       |                                                                                                    |
| `FakeMidiInput`, `FakeMidiOutput` | The fake's `midi_Input` and `midi_Output`                                  | `MidiIn`, `MidiOut`, `common`'s ports; the profiler's `ProfiledMidiInput` and `ProfiledMidiOutput` |
| `TestControlSurface`              | The surface a test drives, making the calls REAPER would                   | `ControlSurface`, which it wraps                                                                   |
| `TestReset`                       | Registers a reset of process state, which only `FakeReaper` runs           | `MidiOut::ResetAllState()`, which resets a port's sent state                                       |
| `RunTime::Now()`                  | Reads REAPER's clock as a `RunTime`                                        | `absl::Now()`, which is real time, for measurement only                                            |

## Structure

### common: One clock (runner.h, control_surface.cc)

- `ControlSurface::Run()` reads `RunTime::Now()` (REAPER's `time_precise()`)
  once. It uses it for the visibility poll, and passes it to
  `ContinuousUndo::Update()` and the listener's `OnRun(const RunTime&)`, which
  passes it to each `Runner::Run()` (and `MidiPorts::RunInput()` and
  `RunOutput()`). Runners never read the clock, so a test controls time by
  controlling REAPER's clock.
- The runs outside `Run()`, when `PluginSurface` and `MidiPorts` are destroyed,
  read `RunTime::Now()`. `absl::Now()` is only used for measurement: the
  profiler, the trace, and the log.
- `ContinuousUndo::Update()` runs at the start of the run, before the listener.
  It is given the run's start time, so it never flushed a change the listener
  made in the same run, and still flushes on the same run as before: the first
  `kDelay` past the last change. Its comment, and the call in `Run()`, say why
  that is safe.

**Brittleness:** a runner could still be run with a time read separately. There
is one `Run()`, and every runner is run from it, so this is easy to see in
review.

### common: Process state (test_reset.h)

```
// For tests only: registers a function that resets process state, which
// FakeReaper calls when it is created and destroyed, and nothing else can.
//   const TestReset kResetRulerModes([] { g_last_ruler_modes = {}; });
//   const TestReset TrackCache::s_test_reset_([] { ... });
class TestReset final {
 public:
  explicit TestReset(void (*reset)());
};
```

| State                                           | Reset                                                                          |
| ----------------------------------------------- | ------------------------------------------------------------------------------ |
| `TrackCache`, `ContinuousUndo`                  | Their `s_test_reset_` deletes the instance                                     |
| `ControlSurface`'s registered type              | Its `s_test_reset_` forgets it, and CHECKs no instance exists                  |
| `timeline.cc`'s `g_last_ruler_modes`            | `kResetRulerModes`                                                             |
| `g_modifiers`                                   | `kResetModifiers`                                                              |
| `MidiPorts`' flush wait (100ms, on destruction) | `MidiPorts::SetFlushWait(duration)`, a plain setting, zero under the fake      |

- **Each reset registers itself**, beside the state it resets, and the fake
  calls every registered one, so every library is treated the same, with no
  list to keep. A class's reset is the initializer of a private static member,
  which can reach its private state, so no reset is public. A registration is
  linked into a binary exactly when its state is.
- `TrackCache::Get()` and `ContinuousUndo::Get()` create their instance on
  first use from a file-local `g_` pointer, rather than a function static, so a
  reset can replace it, for the same null check.
- `~Track()` no longer calls back into `TrackCache`. The cache never drops a
  track while it exists, so it only mattered as the cache was destroyed.
- **Not reset yet:** `scene`'s globals (`g_last_auto_override`), and
  `Plugin`'s instance and trace. *Scene tests* and *Surface tests* register
  theirs.

**Brittleness:** a new global needs a `TestReset` beside it. The `g_` and `s_`
prefixes make them easy to find in review, and anything with an instance is
caught by the fake's teardown check, but a missed global is otherwise silent.

### common, plugin: The profile is the plugin's

`ControlSurface::Type::profile_path` says where each surface writes its
profile, or is empty for none. `ControlSurface` holds its `ReaperProfiler` in a
`std::optional`, declared before the listener, and creates it only when there
is a path. `PluginSurface::Register()` gives `GetLogPath("jprsurf_profile.txt")`.
Tests register a type with no path, so they write nothing. The rest of
`common`'s references to JPRSurf are the backlog's *Keep common free of the
plugin's name*.

### common/testing: FakeReaper (fake_reaper.h)

`jpr_common_testing` links `jpr_common` and gtest, and `jpr_common_test` links
it; the plugin never does.

- **Exactly the API list.** `FakeReaper::Api` has a static function for each
  function in `JPR_REAPER_API`, and `GetFakeFunc` returns
  `static_cast<decltype(::name)>(Api::name)` for each, so a listed function
  without a fake, or with the wrong signature, doesn't compile. The constructor
  loads the API through `LoadReaperApi()`, as REAPER's entry point does, and
  the destructor unloads it. The plugin info's `GetFunc` returns each function
  as it is loaded at the time, hooks included, for the plugin to load. `Api` is nested in `FakeReaper`, a friend of the
  fake's other classes, so it reaches their private state, which tests can't.
- **State, not behavior.** A setter stores the value, and a getter returns it.
  The fake never calls the surface by itself: a test makes the calls REAPER
  would on the `TestControlSurface` `AddSurface()` returns (such as
  `SetTrackListChange()` after adding tracks, and `Run()`), using what REAPER
  sends from "Seen in traces" in the design doc. A test that needs one function
  to behave otherwise hooks it over the fake with `gb::FunctionHook`.
- **The clock** moves only when a surface runs (`Run()` and `RunFor()`, a
  thirtieth of a second each), or the test calls `AdvanceTime()`. No test
  waits on real time.
- **Projects:** one `FakeProject` per open tab (`AddProject()`,
  `SwitchProjectTo()`, `NewProject()`), with the API working on the current
  one when given no project, as JPRSurf always does. A track keeps its pointer
  across tab switches; a closed project's tracks fail the test.
- **Actions:** `AddCommand(FakeCommand)` adds one, with an `absl::AnyInvocable`
  handler for what it would change, and `SetToggleState()` changes its state.
  `Main_OnCommand` records every action it runs (`GetCommandsRun()`), added or
  not, and runs the handler. The fake models no action's effects itself.
- **MIDI ports:** `AddMidiInput(name)` and `AddMidiOutput(name)` list one.
- **Text** is in REAPER's formats, as JPRSurf reads it: `mkvolstr`, `mkpanstr`,
  and `format_timestr_pos` (time, beats, samples, and frames), for a project at
  REAPER's defaults of 120 BPM in 4/4, with 30 frames and 44100 samples a
  second. `ShowConsoleMsg` is recorded (`GetConsoleText()`).
- **Track groups** are modeled simply: a grouped change to a track's mute,
  solo, rec arm, volume, or pan changes every track with the same `group`.
  REAPER's leaders and followers for each property aren't modeled.
- **Not modeled**, and failing the test if used: a MIDI output's stream mode,
  `MIDI_eventlist`'s `AddItem`, `DeleteItem`, and `GetSize`, `SwapBufs` without
  a time, a section for `kbd_getTextFromCmd`, a `Main_OnCommand` flag, and a
  `format_timestr_pos` mode JPRSurf doesn't read. REAPER's own undo point for
  `ToggleTrackSendUIMute` isn't modeled, like those for track volume and pan.

### common/testing: FakeProject and FakeTrack (fake_project.h, fake_track.h)

The rule for what a test sets: a track's own values (name, color, volume, pan,
selection, mute, solo, rec arm, automation mode, group, visibility, and peak)
are fields on `FakeTrack`, which a test sets directly (`track->mute = true`).
What relates tracks to each other, or must stay unique, is the project's, which
keeps it consistent, so a test can't build a state REAPER couldn't be in:
- **Order and folders:** `AddTrack(name, parent)`, `DeleteTrack()` (children
  move up to its parent), and `RestoreTrack(deleted)`, which brings a deleted
  track back as undo does: a new pointer, with the old GUID and values, in its
  old folder (found by GUID, so a restored folder works).
- **GUIDs:** `GetGuid()`, made from the project's number and a counter, so the
  same on every run of a test, and stable for the project's life.
- **Routes:** `AddSend()`, `AddHardwareOutput()`, and `DeleteRoute()`. The
  project owns each `FakeRoute`, so a send and its receive are one route and
  can't disagree, and its ends are `const`. The fake's API uses REAPER's
  indexing: receives as `-1 - index`, and sends after hardware outputs.
- **The rest of a project:** the transport (`SetPlayState()`,
  `SetPlayPosition()`, `SetCursorPosition()`), the automation override, undo
  points (`GetUndoPoints()`, `SetRedo()`), `SetDirty()`, and
  `SetSelectedItemCount()`.

Each track has one `TrackRecord` (its owned track, GUID, parent, routes, and
whether it is deleted) in an `absl::node_hash_map`, so a record never moves.
Deleted tracks are kept, so their pointers are never reused, and a call with
one is caught.

### common/testing: MIDI ports (fake_midi.h)

- A test sends messages or sysex into a `FakeMidiInput`, as the hardware does,
  at the fake's current time. The next `SwapBufsPrecise()` delivers them with
  their frame offsets from the swap's time, so `MidiIn` sees when each was
  sent. A port that isn't open and started drops them, as REAPER's does.
- A `FakeMidiOutput` records the bytes of each `Send()` and `SendMsg()`, which a
  test takes with `TakeReceived()`.
- `CreateMIDIInput`/`Output` open a listed port, and `Destroy()` closes it,
  rather than deleting it, so a test can check it afterwards.

### Checks

The fake fails the test where a rule breaks, with `ADD_FAILURE()`, naming it:
- **The `TrackBatch` rule:** an entry point (each call on a
  `TestControlSurface`, and the test's own calls, checked at teardown) that
  changes the mute, solo, rec arm, or selection of several tracks makes them
  one change, in one `PreventUIRefresh()` scope, or one call outside of any.
- **`PreventUIRefresh()` unbalanced** at the end of an entry point.
- **A deleted or unknown track**, or a closed project, passed to any function.
- **A second control surface** while one is open, or a MIDI port created while
  open, or used after `Destroy()`.
- **At teardown:** a surface or MIDI port still open.

The checks, and the fake itself, have their own tests (`fake_reaper_test.cc`,
`fake_midi_test.cc`), the checks broken on purpose with gtest's
`EXPECT_NONFATAL_FAILURE`.

## Tests

`jpr_common_test` now covers, against the fake: `ControlSurface` (registering,
the single instance rule, runs and their time, track list changes), `Track`
(values, setters, grouping, undo points, routes and their indexing),
`TrackCache` (refresh, folders, filters, visibility, GUIDs across delete and
restore, selection, the last touched track), `TrackBatch`, `MidiPorts`, `MidiIn`
and `MidiOut` (listeners, event times, sysex, queued and changed state),
`Timeline` (ruler modes, positions in each mode), `ContinuousUndo`, automation
modes, and GUIDs.

## REAPER facts

Found while building this:
- Undo and redo, the transport, and the automation override (despite
  `GetGlobalAutomationOverride()`'s name) are each per project (2026-10-02).
  `ContinuousUndo` doesn't yet keep the project it was changed in: see the
  backlog's *Undo points in the project the changes were made in*.

## Building blocks

- **`FakeReaper` (common/testing/fake_reaper.h)**: a test creates one, builds
  the project it needs on `GetProject()`, and calls the code under test, or
  adds a surface and runs it.
- **A radio group of actions:** each action's handler turns itself on and the
  others off with `SetToggleState()`, as REAPER's ruler modes do (see
  `TimelineTest::AddModes()`).
- **`TestReset` (common/test_reset.h)**: any process state in any library
  registers its reset beside it, and every fake resets it.
- **`RunTime::Now()` (common/runner.h)**: the one way to read the
  time a run works with.

## Performance

Against the bar (aa14a5e), on the SurfaceTest project (81 tracks, 2 devices,
243 controls, 36 views, 595 mappings):
- **One clock** is the plugin's one change in cost: `time_precise` is called
  once a run, not four times (6,090 calls in the smoke run, not 23,558).
  Everything else is the same: the smoke run's actions made the same calls to
  REAPER (31 actions, 22 `PreventUIRefresh()` scopes, 11 undo points, and 6
  track list refreshes), with the same work done each run.
- **Smoke:** p50 32.1us and p99 257us, against 29.6us and 236us.
- **Idle:** p50 37.3us and p99 74.5us, against 29.6us and 64.3us, with every
  point a little slower, REAPER's calls and `device`'s unchanged code
  included. One session was about a third slower throughout, which was the
  machine.
- **The bar:** both runs replaced it, as the first started by double-clicking
  the project, and so the first with exact per-run counts.
- **Starting a scenario:** a profile starts with the surface, and runs before
  a project opens poll no tracks, which lowers the per-run counts of every
  point that does: `GetTrackState` was 13.3 a run, not 18, in a session that
  took 6 seconds to choose the project. The scenarios now start REAPER by
  double-clicking the project (see `profiles/README.md`).
