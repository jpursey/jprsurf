# Testing and Profiling

JPRSurf is heading toward code that is unit tested without REAPER or hardware,
and toward a profile of where each run's time goes: to JPRSurf's own code, or
to REAPER's. This doc defines the pieces both are built from, and how they fit
together.

Both need a way to get between JPRSurf and REAPER, and REAPER's API already
provides one: every function is a global pointer that the extension fills in
when it loads. Replacing those pointers gives the profiler its timing and gives
tests a fake REAPER, without changing any code that calls REAPER.

It ties together the backlog items that build toward this. With the fake, code
that depends on REAPER is unit tested, and REAPER is run only for what
CLAUDE.md's Checking in REAPER names. Performance is read from the profile the
profiler writes.

## Principles

- **Hook REAPER where it already can be hooked.** The profiler and the fake
  both replace REAPER's function pointers. No code that calls REAPER changes,
  and a hook that isn't installed costs nothing.
- **One list of REAPER functions.** JPRSurf calls only the functions on its
  list, and calling one that isn't on it is a compile error. Nothing can call
  REAPER without being profiled, or without the fake knowing about it.
- **A fake, not mocks.** Tests check what JPRSurf asked REAPER to do and what
  it sent to the hardware, against a fake that holds REAPER's state. They don't
  script the calls JPRSurf makes, so they don't break when its caching changes.
- **The fake holds REAPER's state, not its behavior.** It is a model of the
  project and REAPER's state, as far as JPRSurf queries it: what is set reads
  back, and a test builds the project it needs. It never calls the surface by
  itself: a test that needs a callback REAPER would make, makes it. What REAPER
  actually sends is recorded from traces, for writing those tests.
- **Behavior time comes from REAPER.** Timers and timeouts read the run's time,
  which comes from REAPER's clock, so tests control it and never wait. Only
  measurement reads a real clock.
- **Profiling is always on, within a budget.** It costs no more than 3us a
  run or 1% of the run's total time, whichever is more, and it measures its own
  cost to show that it does.
- **Counts in tests, times on the machine.** Call counts are deterministic, so
  tests check them. Times depend on the machine, so they are kept in local
  snapshots and compared there.
- **Generic pieces go in Game Bits.** Hooking function pointers and the
  profiler itself know nothing of REAPER, so they are `gb/base` and
  `gb/profile`. Anything that knows REAPER or the X-Touch stays in JPRSurf.

## The pieces at a glance

| Piece            | What it is                                                                 | Where                     |
| ---------------- | -------------------------------------------------------------------------- | ------------------------- |
| API list         | Every REAPER function JPRSurf calls, and loading them                      | `jpr/common/reaper_api.h` |
| Function hooks   | Replace a function pointer with a wrapper of the same signature            | `gb/base/function_hook.h` |
| Profiler         | Timed points, frames, counters, and the report                             | `gb/profile`              |
| REAPER profiling | The profiler on the API list, MIDI ports, the surface's runs, and runners  | `jpr/common`              |
| Trace            | Every REAPER call and callback with its arguments, to see what REAPER does | `jpr/common`              |
| Fake REAPER      | REAPER's state, MIDI ports, surfaces, and clock, behind the API list       | `jpr/common/testing`      |
| Surface notifier | REAPER's calls on the surface from inside its own functions, as traced     | `jpr/common/testing`      |
| REAPER's actions | The actions JPRSurf uses, as traced, and the effects tests need            | `jpr/common/testing`      |
| Fake X-Touch     | The hardware end of an X-Touch's MIDI ports                                | `jpr/device/testing`      |
| Call counter     | Counts REAPER calls by cost, and checks each run's costly calls            | `jpr/common/testing`      |
| Surface harness  | The plugin loaded into the fake, with fake X-Touches                       | `jpr/plugin/testing`      |
| Contract tests   | Tests of the fakes through REAPER's API, run under the fake and in REAPER  | `jpr/common/testing`      |

The `testing` directories are test-only libraries, linked by unit tests and
never by the plugin.

## The REAPER boundary

Everything that passes between JPRSurf and REAPER goes one of four ways:

| Way             | Direction            | Today                                                                                   |
| --------------- | -------------------- | --------------------------------------------------------------------------------------- |
| API functions   | JPRSurf calls REAPER | 61 of REAPER's 857 functions, from 13 files, 9 of them in `common`                      |
| MIDI objects    | JPRSurf calls REAPER | `midi_Input`, `midi_Output`, and `MIDI_eventlist`: abstract classes REAPER creates      |
| Control surface | REAPER calls JPRSurf | `ControlSurface`, the one `IReaperControlSurface`                                       |
| Registration    | Both                 | The plugin entry point, its `reaper_plugin_info_t` (`GetFunc`), and `Register("csurf")` |

The API functions in use, by area:

| Area                      | Functions                                                                                                                                                                                                          |
| ------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| Tracks                    | `CountTracks`, `GetTrack`, `GetMasterTrack`, `GetParentTrack`, `GetTrackGUID`, `GetTrackState`, `GetMediaTrackInfo_Value`, `GetSetMediaTrackInfo_String`, `GetTrackColor`, `GetTrackUIVolPan`, `Track_GetPeakInfo` |
| Selection                 | `CountSelectedTracks`, `CountSelectedTracks2`, `GetSelectedTrack`, `GetSelectedTrack2`, `SetTrackSelected`, `SetOnlyTrackSelected`, `CountSelectedMediaItems`                                                      |
| Track changes             | `SetTrackUIMute`, `SetTrackUISolo`, `SetTrackUIRecArm`, `CSurf_OnVolumeChangeEx`, `CSurf_OnPanChangeEx`, `PreventUIRefresh`, `AnyTrackSolo`                                                                        |
| Routes                    | `GetTrackNumSends`, `GetSetTrackSendInfo`, `GetTrackSendUIVolPan`, `GetTrackReceiveUIVolPan`, `GetTrackSendUIMute`, `GetTrackReceiveUIMute`, `SetTrackSendUIVol`, `SetTrackSendUIPan`, `ToggleTrackSendUIMute`     |
| Transport and timeline    | `GetPlayState`, `GetPlayPosition`, `GetCursorPosition`, `format_timestr_pos`                                                                                                                                       |
| Commands                  | `Main_OnCommand`, `GetToggleCommandState`, `NamedCommandLookup`, `kbd_getTextFromCmd`                                                                                                                              |
| Automation, undo, project | `GetGlobalAutomationOverride`, `SetGlobalAutomationOverride`, `Undo_OnStateChangeEx`, `Undo_OnStateChangeEx2`, `Undo_CanRedo2`, `IsProjectDirty`, `EnumProjects`                                                   |
| MIDI ports                | `GetNumMIDIInputs`, `GetNumMIDIOutputs`, `GetMIDIInputName`, `GetMIDIOutputName`, `CreateMIDIInput`, `CreateMIDIOutput`                                                                                            |
| Text and time             | `guidToString`, `stringToGuid`, `mkvolstr`, `mkpanstr`, `time_precise`, `ShowConsoleMsg`                                                                                                                           |

Only six string-keyed parameters are read: `P_NAME`, `I_AUTOMODE`,
`B_SHOWINMIXER`, `B_SHOWINTCP`, `P_DESTTRACK`, and `P_SRCTRACK`. The part of
REAPER a fake has to hold is small.

### The API list

`jpr/common/reaper_api.h` is the only file that includes the SDK's
`reaper_plugin_functions.h`. It defines `REAPERAPI_MINIMAL` and a
`REAPERAPI_WANT_<name>` for each function JPRSurf calls, so the SDK declares
only those, and it lists the same functions again for code that needs to go
over all of them:

```
// Every REAPER API function JPRSurf calls. X(name) is expanded for each one.
#define JPR_REAPER_API(X)    \
  X(AnyTrackSolo)            \
  X(CountSelectedMediaItems) \
  ...

// Loads the REAPER API from `get_func`, which is REAPER's GetFunc or a fake's.
// Returns false, and logs the names, if any function is missing from
// `get_func`, or has a REAPERAPI_WANT_ line but isn't in JPR_REAPER_API.
bool LoadReaperApi(void* (*get_func)(const char* name));
```

- **Calling a function that isn't listed** is a compile error, as the SDK
  doesn't declare it.
- **Listing a function in one place but not the other** is caught either way.
  A name in `JPR_REAPER_API` without its `REAPERAPI_WANT_` line doesn't
  compile. A `REAPERAPI_WANT_` line without its `JPR_REAPER_API` entry is found
  when the API loads: `LoadReaperApi()` wraps `get_func` and sees every name
  the SDK asks for. An unlisted one is logged as an error and fails the load,
  in REAPER or under the fake, and `reaper_api_test` catches it before either.
- **Only the listed functions are loaded**, so JPRSurf no longer fails to load
  in a REAPER that lacks one of the 800 functions it never calls.
- `REAPERAPI_IMPLEMENT` moves from `src/reaper_sdk.cc` into
  `jpr/common/reaper_api.cc`, as the functions it defines are now JPRSurf's
  list.

**Brittleness:** every file must include `reaper_api.h` rather than the SDK
header. One that includes the SDK header sees every function, though calling
an unlisted one still fails to link, as only the listed functions are defined.
The SDK header's name appearing anywhere else is easy to find in review.

### Function hooks

A small template in `gb/base`, with no knowledge of REAPER:

```
// Replaces the function a global function pointer points to with one of the
// same signature, which calls Hook::Call(original, args...). `original` is the
// pointer's value when the hook was installed. Installing and uninstalling
// just assign the pointer.
template <auto kPointer, typename Hook>
class FunctionHook;
```

- The signature comes from the pointer's type (`decltype(::GetTrack)`), so no
  wrapper is written by hand. Expanding `JPR_REAPER_API` installs a hook on
  every function at once.
- Hooks stack: a hook installed over another calls it as its original. The
  profiler hooks whatever loaded, whether REAPER or the fake, and a trace can
  hook over the profiler.
- A hook over a function that was never loaded leaves its pointer null, so
  every function on the list can be hooked safely, and code that checks
  whether a function exists still finds it missing.
- A hook doesn't have to call its original, so one can stand in for a function
  that was never loaded, by opting in with `kHookNotLoaded`. The fake uses one
  for every function it doesn't implement, which fails the test naming the
  function.
- JPRSurf only calls REAPER from REAPER's UI thread, so hooks can be installed
  or removed between runs.

Any C API loaded into function pointers can use it, such as Vulkan in Game
Bits' renderer.

### MIDI objects

`midi_Input` and `midi_Output` are REAPER's objects, reached through virtual
calls rather than the pointers. A hooked `CreateMIDIInput` or
`CreateMIDIOutput` wraps the object it returns in one of the same interface
that forwards every call, so the profiler times them. Under the fake, the
object being wrapped is the fake's own port.

### Time

Time is read for two different jobs:

| Job         | Examples                                                                              | Clock                       |
| ----------- | ------------------------------------------------------------------------------------- | --------------------------- |
| Behavior    | Double and long press (350ms), the undo merge delay (500ms), the visibility poll (1s) | The run's time, from REAPER |
| Measurement | The profiler, and the durations events log                                            | Real time, never faked      |

`ControlSurface::Run()` reads REAPER's `time_precise()` once, at the start of
the run, and everything that times behavior reads that time. Under the fake,
the fake decides what `time_precise()` returns, so a test of a long press
advances the clock rather than waiting. MIDI event times are already relative
to `time_precise()` (`MidiIn::Poll()`), so this is the clock they agree with.

`RunTime::Now()` reads the clock, and `ControlSurface::Run()` passes the time
to the visibility poll, to `ContinuousUndo::Update()`, and to the listener's
`OnRun()`, which gives it to every `Runner::Run()`. `ContinuousUndo` records
changes at the time of the run they happen in, and `Control`'s press timers use
the time their runner is given. Only the runs outside `Run()`, when the surface
is being destroyed, read the clock themselves. `absl::Now()` is only used for
measurement.

`ContinuousUndo::Update()` is called at the start of the run, before the
listener, so the changes the listener makes have the run's time.
`ControlSurface::Run()` says why that is safe.

Game Bits has `gb::Clock` and `gb::FakeClock`, which would also work, but
they add a second way to fake the same clock, and need a `Clock*` that undo and
`ControlSurface` can reach.

## Profiler

The profiler is built: [profiler.md](worklog/profiler.md) describes it, and
CLAUDE.md's Performance section how to read it. In brief:
- **What it records:** each run as a frame (`ControlSurface::Run`), every call
  to a function on the API list and on a MIDI port as a call point, each
  runner's run and known heavy work in JPRSurf as scopes, the work their time
  scales with as counters, and the workload as values. Points are named for
  the function they time (`GetTrack`, `TrackCache::Refresh`).
- **Nesting:** every timed point records its self time, its time less that of
  the points inside it, so a callback REAPER makes from inside one of its calls
  is charged to JPRSurf. Summing self times splits each run into JPRSurf's time
  and REAPER's. Time REAPER spends outside JPRSurf's calls, such as a redraw it
  defers to its own loop, isn't seen.
- **Cost:** a timed point is about 12ns, and a run times about 120, for about
  1.5us a run. The budget is the larger of 3us a run and 1% of the run, so a
  doubling warns.
- **Output:** a profile is of one surface's runs, leaving out its startup. It
  is written to `jprsurf_profile.txt` beside the log when the surface is
  destroyed, replacing the last one. It is plain text, one line per point in a stable order, so two
  snapshots diff cleanly. The log only has a warning for a run over 8ms, and a
  summary when the profile is written.

Where it differs from what this doc first planned: each runner is named,
rather than each runnable (a runner only ever holds one kind); `Run()` is the
only one of the surface's callbacks that is timed (the others only set flags);
and there is no periodic `Run()` log line.

Tests don't load the profiler, except the benchmarks (below). Call count tests
count with a `CallCounter` instead (see Tests), which hooks the same API list, and also sees what the
profiler doesn't: whether a deferred refresh is open, and a call's arguments,
which say whether it repeats one already made. A deferred refresh is a
`PreventUIRefresh()` scope: REAPER puts off refreshing its UI until the
outermost one ends.

### Benchmarks

JPRSurf's own time, without REAPER. `jpr_benchmark`
(`plugin/default_config/benchmark.cc`) runs the default config on the large
project (`AddLargeProject()`) under the fake, as the surface tests do, with a
`ReaperProfiler` of its own. Its times depend on the machine and its load, so
it isn't a ctest test: the `benchmark` target runs it on demand (see CLAUDE.md),
and its summary is compared against a snapshot (see `profiles/README.md`).
- **What it times:** each steady state the call count tests count (at the top,
  in a folder, while playing, and Send/Receive mode on a bus), for 3000 runs;
  and each kind of action in the smoke scenario, and a few other gestures, 100
  times, as the surface tests make them. An action is repeated with what undoes
  it (mute on and off, into a folder and back out), so each repeat does the
  same work.
- **What it reports:** a profile of each benchmark's runs alone, without
  setting up, and a summary with JPRSurf's self time per run, and for an
  action, per action. An action's runs include the ones that settle it, which
  would cost something with nothing happening, so an action's time is beyond
  the idle time of the state it starts from, timed just before it.
- **What isn't JPRSurf's:** self time leaves out every timed call inside a
  point, and every call into the fake (with the `CallCounter` and
  `SurfaceNotifier` hooked over it) and on a MIDI port is one, so neither the
  fake nor the fake X-Touches' decoding is counted. What the profile shows as
  REAPER's is theirs, which says nothing about REAPER.
- **Noise:** how closely runs agree, and so what a change must move to count,
  is in "Benchmark" in `profiles/README.md`.
- **What it misses:** REAPER's effect on JPRSurf's own code. An idle run is
  about 6us of JPRSurf's time under the fake, on 128 tracks, against about
  28us in REAPER on 81 tracks (`profiles/idle.txt`); REAPER's work between
  runs (its UI on the same thread, and audio) most likely leaves JPRSurf's code
  and data out of the cache. A change whose cost is in that, such as one that
  touches more memory, shows in REAPER, not here.

## Trace

A trace hooks every function on the list, and every surface callback, and logs
each call in order with its arguments and result, to `jprsurf_trace.txt`.
Output parameters (a non-const pointer to a number) are logged with their
values after the call, and tracks by their index and name rather than their
pointer. It is off unless the `JPRSURF_TRACE` environment variable turns it on
when the plugin loads, and costs nothing while off.

Top level calls that only read state, such as most runs, are left out and
counted, so a trace shows the events and the whole call each happened in. On
an 81-track project, a run takes about 90us with the trace on, against 45us
without it.

It is how tests learn what REAPER does. Setting a track's mute in REAPER and on
the surface, with a trace running, shows which callbacks REAPER sends, in what
order, and whether they come during the call or later. A test that makes
REAPER's calls on the surface follows what a trace showed.

Traces are not replayed as tests. A replay fails whenever JPRSurf changes the
order or number of its calls, which is exactly what caching work changes.

## Fake REAPER

A fake holds REAPER's state in memory, and implements the API list over it:

```
// A sketch of the fake. Only one may exist at a time.
class FakeReaper final {
 public:
  // Loads the fake as the REAPER API, and resets JPRSurf's process state.
  FakeReaper();

  // Fails the test if a surface, plugin, or MIDI port is still open, then
  // resets the process state again.
  ~FakeReaper();

  // What REAPER passes the plugin's entry point.
  reaper_plugin_info_t* GetPluginInfo();

  // The project.
  FakeTrack* AddTrack(std::string_view name, FakeTrack* parent = nullptr);
  void DeleteTrack(FakeTrack* track);
  FakeTrack* GetMasterTrack();

  // MIDI ports, listed under these names.
  FakeMidiInput* AddMidiInput(std::string_view name);
  FakeMidiOutput* AddMidiOutput(std::string_view name);

  // Adds a control surface of the registered type, as the user does in
  // REAPER's preferences. A test makes the calls REAPER would make on the
  // TestControlSurface it returns (Run() advances the clock by one run), which
  // passes each on to the surface, and checks it. Destroying it removes the
  // surface.
  std::unique_ptr<TestControlSurface> AddSurface(std::string_view config = {});

  // Advances the clock, for a test with no surface to run.
  void AdvanceTime(absl::Duration duration);

  // What JPRSurf did: the commands it ran, and the undo points it made.
  absl::Span<const FakeCommand> GetCommands() const;
  absl::Span<const FakeUndoPoint> GetUndoPoints() const;
};
```

### State

| Area                   | Held                                                                                                                |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------- |
| Tracks                 | Order, parent, GUID, name, color, volume, pan, mute, solo, rec arm, automation mode, TCP and mixer visibility, peak |
| Selection              | Selected tracks (the master too), and the selected media item count                                                 |
| Routes                 | Each send and receive: the other track, volume, pan, and mute                                                       |
| Transport and timeline | Play state, play and cursor positions                                                                               |
| Commands               | Toggle states, named command IDs, and command names. Commands the test gives a handler run it.                      |
| Automation and undo    | The global automation override, undo points, whether redo is possible, the dirty flag                               |
| MIDI ports             | Named inputs and outputs (see [Fake X-Touch](#fake-x-touch))                                                        |
| Clock                  | What `time_precise()` returns                                                                                       |

A `MediaTrack*` is opaque in the SDK, so the fake hands out pointers to its
own tracks.

### Behavior

The fake holds REAPER's state, not its behavior. Every function on the list is
a C function pointer, stubbed as the trace and profiler hook them, and the MIDI
ports are the SDK's C++ interfaces, implemented by the fake's own classes. Both
work on the fake's model of the project: a setter stores the value, and a
getter returns it. A test that needs one function to behave otherwise, such as
to check an assumption about REAPER, hooks it over the fake with
`gb::FunctionHook`, and every other function still reads the model.
- **No callbacks of its own.** The fake never calls the surface by itself.
  `device` and most of `common` never see `IReaperControlSurface`, so their
  tests need none. `scene` reads REAPER through `TrackCache`, which only a
  `ControlSurface` keeps current, so its tests run it from one, with a
  `SurfaceNotifier`. A test of `ControlSurface`, or of the plugin, makes
  the calls REAPER would make itself, such as `SetTrackListChange()` after
  adding tracks. The calls REAPER makes from inside its own functions, such as
  `SetSurfaceMute()` after `SetTrackUIMute()`, a `SurfaceNotifier` makes, by
  hooking those functions over the fake, so a test of a whole surface gets them
  at the right moment. [Seen in traces](#seen-in-traces) records what REAPER
  sends, and when, which the notifier follows.
- **Text** from `mkvolstr`, `mkpanstr`, and `format_timestr_pos` is in
  REAPER's formats ([Seen in traces](#seen-in-traces)), as `Timeline` parses
  positions, for a project at REAPER's default tempo and rates.
  `kbd_getTextFromCmd` returns the text a test gave the action.
- **Commands** are recorded, and run a handler if the test gave one.

Tests are then of two kinds, which need little of REAPER's own behavior:
- **Output:** given REAPER's state and some input, what JPRSurf asked REAPER to
  do, and what it sent to the hardware.
- **Input and response:** REAPER's state, or a notification, set up before the
  next run, and what JPRSurf does in response.

### Seen in traces

Traces of an 81-track project with one JPRSurf surface (2026-09-28, and the
smoke scenario on 2026-09-29) showed the following. The notifier's contract
tests (`surface_notifier_contract_test.cc`), run in the test install
(2026-10-08), corrected what REAPER calls from inside its own functions, and
are now the record of those calls. `SurfaceNotifier` and tests that make
REAPER's calls on the surface follow these.

- **Mute and solo notify at the next refresh.** During `SetTrackUIMute()` and
  `SetTrackUISolo()`, REAPER only calls `SetSurfaceSolo(master)`, even if
  nothing changed. A track's `SetSurfaceMute()` and `SetSurfaceSolo()` come at
  the refresh, for each track whose mute or solo differs from what was last
  sent, in track order: at `PreventUIRefresh(-1)` for the outermost deferred
  refresh, or outside one, before the next run. Until then, every track's
  state leaves them out. The surface that made the change is notified too.
- **Selection notifies each track whose selection changed,** in track order:
  outside a deferred refresh during the call (a track already selected isn't
  sent), and in one at `PreventUIRefresh(-1)`.
- **Rec arm changes the track list,** if it changes a track. A track's change
  is its `SetSurfaceRecArm()`, `SetSurfaceMute()`, `SetSurfaceSolo()`, and
  volume and pan (as in every track's state, below). Outside a deferred
  refresh, during `SetTrackUIRecArm()`, REAPER sends the track's change,
  `SetTrackListChange()`, every track's state, `SetSurfaceSolo(master)`, and
  the track's change again. In a deferred refresh, it calls
  `SetTrackListChange()` and `SetSurfaceSolo(master)`, and at
  `PreventUIRefresh(-1)` come `Extended(CSURF_EXT_SETMIXERSCROLL)`,
  `SetTrackListChange()`, `SetSurfaceSolo(master)`, and every track's state,
  once for the deferred refresh. In it, a track whose rec arm changed has only its title, rec arm, and
  selection, then its change. A track whose mute or solo is still to be sent
  has them after its selection, and one whose selection changed has its
  selection again.
- **Ganging and grouping:** with the setters' group flags without `&2`, a
  mute, solo, or rec arm of a selected track changes every selected track,
  and without `&1`, of a track in a group changes the group (see "Track
  changes" in `fake_reaper.cc`). Either way, a rec arm is then sent as a
  change to every track.
- **Send volume and pan notify,** even if nothing changed.
  `SetTrackSendUIVol()` and `SetTrackSendUIPan()` call
  `Extended(CSURF_EXT_SETSENDVOLUME)` or `SETSENDPAN` with the source track,
  the route's index as these functions index it (hardware outputs first), and
  the value, then for a send to a track, `SETRECVVOLUME` or `SETRECVPAN` with
  the destination and its index among its receives. A call that ends an edit
  (isend 1) calls nothing back, and neither does `ToggleTrackSendUIMute()`. A
  deferred refresh changes none of this.
- **Automation modes resend volume, pan, and selection.** Inside
  `Main_OnCommand()` for the automation mode actions (40400 to 40404, and
  42023 for Latch Preview), REAPER called `SetAutoMode()` with the mode (0 to
  5), then, if a selected track's mode changed, for every track, master first,
  `SetSurfaceVolume()`, `SetSurfacePan()`, `Extended(CSURF_EXT_SETPAN_EX)`,
  the track's `SetSurfaceMute()` and `SetSurfaceSolo()` if they aren't sent
  yet, and `SetSurfaceSelected()`. `SetGlobalAutomationOverride()` calls the
  same, without `SetAutoMode()`, even if nothing changed. In a deferred
  refresh, the override calls nothing, and the same round comes once at its
  end (see the refresh below).
- **A volume change in a mode that writes automation** (Write or Latch, as
  the override) adds an envelope the first time for each track, which REAPER
  sends as a change to the track: its selection twice, then its rec arm, mute,
  solo, volume, and pan, before the last touched track. In a deferred refresh,
  it is a track list change at its end. The fake doesn't model this.
- **`SetSurfaceSolo(master, on)`** reports whether any track is soloed, as the
  SDK says.
- **Faders and pans notify only in a deferred refresh.** During
  `CSurf_OnVolumeChangeEx()` and `CSurf_OnPanChangeEx()`, REAPER calls
  `IsKeyDown(VK_SHIFT)`, then `Extended(CSURF_EXT_SETLASTTOUCHEDTRACK)` with
  the track. Outside a deferred refresh, they call neither `SetSurfaceVolume()`
  nor `SetSurfacePan()`. In one, at its end, each track whose volume or pan was
  set in it (ganged tracks too) has its `SetSurfaceVolume()`,
  `SetSurfacePan()`, and `Extended(CSURF_EXT_SETPAN_EX)` sent once, with its
  last values, even if they changed back.
- **The end of a deferred refresh** sends, for each track in order, its volume
  and pan, then its mute and solo, then its selection, for those that wait for
  it. After a rec arm, every track's state comes first, leaving out the volume
  and pan that wait, which follow it.
- **Changes in REAPER's own UI** arrive between runs. Muting a track with its
  button called `Extended(CSURF_EXT_SETLASTTOUCHEDTRACK)`, then the track's
  `SetSurfaceMute()` and `SetSurfaceSolo()`. Clicking a track in the track
  panel (2026-10-05) called `SetSurfaceSelected()` for each track whose
  selection changed, in track order (the track unselected too), then
  `OnTrackSelection()` and `Extended(CSURF_EXT_SETLASTTOUCHEDTRACK)` with the
  track clicked. Ctrl+clicking a track to add it to the selection
  called the same, with only that track's `SetSurfaceSelected()`. Each click
  was followed, a run later, by `Extended(CSURF_EXT_SETMIXERSCROLL)` with the
  track. Setting the global automation override in REAPER called what
  `SetGlobalAutomationOverride()` does. In the test install, REAPER called
  `IsKeyDown(VK_SHIFT)` before every run.
- **Undo resends everything, during the call.** Inside `Main_OnCommand()` for
  Edit: Undo, REAPER called the master's `SetSurfaceMute()`,
  `SetSurfaceVolume()`, and `SetSurfacePan()`, `SetRepeatState()`,
  `Extended(CSURF_EXT_SETBPMANDPLAYRATE)`, `Extended(CSURF_EXT_SETMIXERSCROLL)`,
  and `SetTrackListChange()`, then every track's state (below), then the
  master's solo, mute, volume, and pan again, then each track's rec arm, input
  monitor, mute, solo, volume, and pan. The second round's volumes were the
  project's after the undo, where the first round's weren't always. Edit: Redo
  calls the same. With nothing to undo or redo, they call nothing back. The
  ruler's time unit actions (40365, 40369, 40370), and every other action the
  smoke scenario ran, called nothing back.
- **Every track's state** is sent master first, then each track in order:
  `SetSurfaceVolume()`, `SetSurfacePan()`, `Extended(CSURF_EXT_SETPAN_EX)`
  (with the track's pan mode: 3, REAPER 4 and later's balance, in a project
  file with `PANMODE 3`, and 0 without it), `SetSurfaceMute()`,
  `SetSurfaceSolo()` (not for the master),
  `SetTrackTitle()`, `SetSurfaceRecArm()`, `Extended(CSURF_EXT_SETINPUTMONITOR)`,
  and `SetSurfaceSelected()`.
- **Creating the surface:** at startup, REAPER calls `create` with the saved
  config string before the project loads, and the surface opens its MIDI ports
  inside it. Then come `Extended(CSURF_EXT_SETBPMANDPLAYRATE)`, and on loading
  the project, `Extended(CSURF_EXT_SETPROJECTMARKERCHANGE)`,
  `Extended(CSURF_EXT_SETMIXERSCROLL)`, three rounds of `SetTrackListChange()`
  each followed by every track's state, and `SetRepeatState()` and
  `SetPlayState()`. `GetTypeString()`, `GetDescString()`, and
  `GetConfigString()` weren't called during the session.
- **Exiting:** REAPER destroys the surface, then unloads the plugin, so the
  profiler's snapshot can be written when the surface is destroyed.
- **Text:** `mkvolstr` writes `-14.2dB`, `-5.10dB`, `+4.23dB`, and `-inf dB`
  (two decimals below 10dB, and one from there), and 0dB as `0.00dB`.
  `kbd_getTextFromCmd` names commands with their section, such as
  `Edit: Undo`. `format_timestr_pos` in beats mode (2) writes 3.5 seconds as
  `2.4.00`. The contract tests check each format's rounding and negative
  positions (see `TextContractTest`).

### Checks

The fake also checks rules for using REAPER that are only prose today, and
fails the test when one is broken:
- **One refresh:** an entry point that changes the mute, solo, rec arm, or
  selection of more than one track makes them one change, in one deferred
  refresh, for one UI refresh. Each run is one, so this catches changes made
  outside a run, such as by a test, or REAPER's calls between runs. This is
  the rule on track setters in CLAUDE.md's Performance section, enforced.
- An action run inside a deferred refresh fails: the extension runs every
  action with `RunAction()`, outside one, as an action may open REAPER's UI.
- `PreventUIRefresh()` must be balanced by the end of each entry point.
- A call with a deleted track's pointer fails, where REAPER might crash.
- At teardown, a surface, plugin, or MIDI port still open fails.

The fake implements every function on the API list, and only those: a function
the SDK declares but JPRSurf doesn't load isn't the fake's concern. Adding a
function to the list without a fake for it doesn't compile.

### Process state

REAPER is a process-wide singleton, and JPRSurf's process state mirrors it.
Rather than threading it through every property and mapping, it is reset
between tests:

| State                                                      | Reset by                                                         |
| ---------------------------------------------------------- | ---------------------------------------------------------------- |
| `TrackCache`, `ContinuousUndo`                             | The fake, at creation and teardown                               |
| `ControlSurface`'s registered type                         | The fake                                                         |
| `ControlSurface`'s instance, and `Plugin`'s                | Destroying the surface, unloading the plugin, and the leak check |
| `g_modifiers`, and `timeline.cc`'s `g_last_ruler_*` caches | The fake                                                         |
| `scene`'s `g_last_auto_override` and `g_next_const_id`     | The fake                                                         |
| The run's time                                             | The fake                                                         |
| The API table                                              | The fake: unloaded at teardown                                   |
| The profiler                                               | Destroying the surface, which owns it                            |

The profile file isn't process state: the plugin gives its path when it
registers the surface type, and a test registers a type with none.

- Only the fake can reset anything. Each reset takes a key type that only
  `FakeReaper` can create, so no other code can call it.
- `TrackCache` and `ContinuousUndo` move from `absl::NoDestructor`, which
  can't be reset, to a holder that can.
- gtest runs a binary's tests one at a time, and ctest runs each binary in its
  own process, so one fake at a time is no limit.

**Brittleness:** a new global has to be added to the reset. The `g_` prefix
makes them easy to find in review, and anything with an instance is caught by
the leak check, but a missed global is otherwise silent.

### Running the plugin

The fake supplies the `reaper_plugin_info_t` REAPER gives the entry point, so a
test loads the plugin exactly as REAPER does: `Plugin::Load()` loads the API
through the fake's `GetFunc`, `Register("csurf")` records the surface type,
and `AddSurface()` creates it through the recorded `create`. The fake's
`GetFunc` returns each function as it is loaded at the time, so hooks already
over the fake, such as a `SurfaceNotifier`'s, survive the plugin loading the
API again.

The plugin is split into a library and a DLL:
- `jpr_plugin` is a static library with `plugin.cc` and `plugin_surface.cc`,
  which tests link.
- `reaper_jprsurf` is the DLL: `dll_main.cc` with `DllMain`, and the exported
  `REAPER_PLUGIN_ENTRYPOINT`. A static library would drop an export nothing
  references.

`dll_main.cc` reads the environment, whether to trace (`JPRSURF_TRACE`) and
where the trace and profile go (beside the log), and passes it to
`Plugin::Load()` in `Plugin::Options`. So it is the only code no test runs, and
a test, which loads the plugin with no options, never writes the user's trace
or profile.

## Fake X-Touch

`CreateMIDIInput()` and `CreateMIDIOutput()` are on the API list, so the fake
lists named ports (`"X-Touch"`, `"X-Touch-Ext"`) and returns its own:
- A **fake input** queues events, and delivers them on the next
  `SwapBufsPrecise()`.
- A **fake output** passes each `Send()` and `SendMsg()` to the fake hardware
  connected to it (`Connect()`), and records it for a test that asks
  (`SetRecording()`), independently.

A `FakeXTouch` (`jpr/device/testing`), for the X-Touch or its extender, lists a
pair of them and connects to the output:
- **Out of JPRSurf:** it decodes each message as it is sent into the hardware's
  state: each button's light (off, on, or blinking), fader positions, encoder
  rings, meters, scribble strip text and colors, and the timecode display.
  Meters fall each run until they are sent again, as the hardware's do, only
  faster. A fader let go of goes back to where it was last sent, as the
  hardware's does by itself (a second later, where the fake's goes at once),
  so a fader isn't sent the same position again.
- **Into JPRSurf:** press, release, touch, move, and turn, encoded as the
  hardware sends them.

It is written from the protocol (the Mackie Control messages the X-Touch
speaks, and its scribble strip sysex), as tables, and not from
`DeviceXTouch`'s code, so a misreading of the protocol isn't copied into both.
Its library doesn't link `jpr_device`. Where the X-Touch differs from the
Mackie (its meters, and its rings' end lights), it follows the X-Touch, as
checked on the hardware (see [fake_xtouch.md](worklog/fake_xtouch.md)). It is
strict: a message the hardware can't take fails the test.

## Surface harness

Tests of the whole surface load the plugin into the fake, as REAPER loads it
(see Running the plugin), with fake X-Touches connected, and act as a user
does: press buttons, move faders, and turn pots, then check what REAPER was
asked to do and what the hardware shows. The design and how it was built are
in [surface_tests.md](worklog/surface_tests.md).

- **`SurfaceTest`** (`jpr/plugin/testing`) is the fixture for any config: the
  fake, a `SurfaceNotifier`, and the surface, with `AddSurface()` and
  `RemoveSurface()` as REAPER does at startup and exit. It has helpers for
  what is easy to get wrong by hand: `AddTracks()`, which names each track for
  where it is (T2, T2.1), `RunUntilShown()`, and presses and moves that get
  the clock right (`Tap()`, `DoublePress()`, `LongPress()`, `Hold()`, and
  `MoveFader()`). Anything logged at `ERROR` or above fails the test, as
  `jprsurf.log` with no new errors is the first check in REAPER.
- **`DefaultConfigTest`** (`jpr/plugin/default_config`) is the fixture for the
  config `PluginSurface` builds: an X-Touch with an extender to its left (or
  the X-Touch alone), its strips numbered across both, and what its tests
  share (how a strip shows 0dB and a pan, a strip's lights, and selecting).
  Each file's fixture derives from it, and builds the least project that
  shows its behavior.
- **REAPER's actions:** the fake has every action JPRSurf uses, as traced
  (`AddReaperActions()`), and records each one run (`GetCommandsRun()`), which
  is what most tests check. Those whose effect a test needs (the ruler modes,
  and the automation modes) have a handler that makes it.
- **REAPER's calls back:** the notifier makes the calls REAPER makes from
  inside its own functions, during the run, and the calls for a change made in
  REAPER's own UI, between runs, when a test asks (`ClickTrack()`,
  `CtrlClickTrack()`, and `ClickMute()`). A change no trace has shown fails the
  test.

A test reads like the smoke test:

```
TEST_F(TrackStripTest, MovingAFaderSetsTheVolume) {
  MoveFader(xtouch_, 1, 0);
  EXPECT_EQ(tracks_[9]->volume, 0.0);
  EXPECT_EQ(xtouch_.GetFader(1), 0);
}
```

**Timing.** Each run reads the project before it acts, so what a run changes
on other tracks, or sends to a fader, is shown on the next: `RunUntilShown()`
is two runs. The press helpers also wait out the double press time, in case a
press is held back as the first of two.

**Setting the fake, or acting through REAPER.** What the surface polls each
run (a track's mute, solo, volume, pan, color, and selection light, toggle
states, the override, the cursor, and route values) a test sets on the fake
directly, and the surface follows it. What the surface caches until REAPER
calls back (the selected tracks' automation modes) a test changes through the
surface or REAPER's API, so the notifier makes the call that refreshes it.
Setting the fake directly for those is a change REAPER would have told the
surface about, and the surface doesn't follow it.

## Contract tests in REAPER

Contract tests act only through REAPER's API, so the same tests run under the
fake, in `ctest`, and inside REAPER, which checks the fake and
`SurfaceNotifier` against REAPER. Each derives from `ContractTest`
(`jpr/common/testing/contract_test.h`), and builds the project it starts from
as a `FakeProject`, which REAPER opens from a project file. The design is in
[check_fakes_in_reaper.md](worklog/check_fakes_in_reaper.md).

In REAPER, they run in a test install: a portable REAPER of their own, which
has the `reaper_jpr_check` DLL in its `UserPlugins` only while they run, and
the plugin only while `check_load_in_reaper` runs (see Loading the plugin).
The user's own REAPER and its settings are never touched.

### Setting up the test install

Once, by hand:
1. Run REAPER's installer, choose **Portable install**, and install it in a
   folder of its own, outside the repositories, such as
   `E:\Projects\reaper-check`.
2. Set the user environment variable `JPR_REAPER_CHECK_DIR` to that folder,
   and open a new shell (or restart Visual Studio) so it is seen.
3. Run `reaper.exe` in that folder once:
   - Register it with your REAPER license, so it starts without the
     evaluation reminder.
   - Answer any first run questions, so they aren't asked again.
   - In Preferences, Audio, Device, set the audio system to **Dummy Audio**,
     so it never takes an audio device from your own REAPER.
   - Quit.

Each run sets up the rest (`jpr/common/testing/run_in_test_install.cmake`): it
copies the DLL that runs, `reaper_jpr_check.dll` for the contract tests, into
the test install's `UserPlugins`, makes that DLL's surface its only control
surface in its `reaper.ini`, and removes the DLL again once REAPER quits. Any
DLL that runs there is built on `jpr/common/testing/test_install.h`: it does
its work from its surface's runs, and writes its files to the run directory,
the test install's `run` folder.

### Running them

```
cmake --build out/build/x64-Release --target check_in_reaper
```

It builds the DLL, and runs REAPER in the test install, which runs every
contract test and quits. It prints what gtest printed, and fails if a test
failed, REAPER didn't quit within five minutes (or the seconds
`JPR_REAPER_CHECK_TIMEOUT` sets), or no tests ran. Each test takes about half
a second, opening its project. Setting `GTEST_FILTER` runs only the tests it
names, as for any gtest binary. The
files from the last run are in the test install's `run` folder: gtest's
output, the result, a log of how far the DLL got (which a failed run prints
too), and the last project file opened.

It runs REAPER, so it runs from the main checkout only, one run at a time, as
other work that needs REAPER does. It doesn't touch the user's REAPER, so
Claude runs it to verify a change to the fake or the notifier. A change that
doesn't touch them is verified under the fake alone.

### Loading the plugin

```
cmake --build out/build/x64-Release --target check_load_in_reaper
```

`dll_main.cc` is the only code no unit test runs, so the same test install
checks that REAPER loads and unloads the plugin
(`jpr/plugin/check_load_in_reaper.cmake`). It copies `reaper_jprsurf.dll` into
`UserPlugins`, beside `reaper_jpr_check.dll`, and runs the contract tests with
a `GTEST_FILTER` that runs none, so the check DLL just quits REAPER. REAPER
runs with `JPRSURF_TRACE` set, and with `APPDATA` set to
`out/build/x64-Release/src/jpr/plugin/check_load_in_reaper/`, so the log and
the trace go there rather than over the user's. It fails if the run failed,
`jprsurf.log` has an error, or the trace isn't there with its header and
"Trace ended". `run_in_test_install.cmake` removes the plugin from
`UserPlugins`, with the check DLL, once REAPER quits, whether or not it
passed.

The test install has only the recording surface, so the plugin creates no
surface, which would open the X-Touch's MIDI ports that the user's REAPER may
hold. It checks loading and unloading, not a surface. It builds the plugin
without deploying it (the deploy is a target of its own), so it runs while the
user has REAPER open. If a run is stopped part way, the DLLs may be left in
`UserPlugins`, where the next run would load them too, and the plugin would
write its log over the user's: remove them by hand.

## Tests

| Library  | Tested against               | What                                                                                                                                                                  |
| -------- | ---------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `common` | The fake                     | `Track`, `TrackCache`, `TrackBatch`, `Timeline`, MIDI ports, `ContinuousUndo`, and `ControlSurface`'s callbacks and single instance rule                              |
| `device` | The fake, and other fakes    | `Control`, its inputs and outputs, and their MIDI variants, each on its own; and every control on `DeviceXTouch`, on a fake X-Touch                                   |
| `scene`  | The fake, and fake controls  | Properties against REAPER's state, views (conditions, subjects, lists, references), mappings (modifiers, taps, picks), and `TrackActions` (ranges, anchors, grouping) |
| `plugin` | The surface harness          | JPRSurf's own surface, a file per area: track strips, the track list and its navigation, Send/Receive mode, and the global controls                                   |

Existing tests that need no REAPER stay as they are.

**Call count tests** check performance where it is deterministic. The design
and what they found are in
[reaper_call_count_tests.md](worklog/reaper_call_count_tests.md).
- **Costs:** every function on the API list has a cost (`GetCallCost()` in
  `jpr/common/testing/call_cost.h`): cheap (a read, text included), moderate
  (a scan of the project, a rename, or a deferrable setter's own work), a
  refresh of REAPER's UI, an undo point, or a command. Each is what the
  function costs in REAPER on its own, measured in the test install (see
  [measure_reaper_costs.md](worklog/measure_reaper_costs.md)). A
  `static_assert` keeps the table in line with the list.
- **Rules in every surface test:** `SurfaceTest` holds a `CallCounter`
  (`calls_`), which fails any of the fake's entry points (a run, another call
  on the surface, or the test's own calls between them) that refreshes
  REAPER's UI twice, adds two undo points, or repeats a refresh or a command
  on the same track, route, or action. A setter whose refresh REAPER puts off
  in a deferred refresh (every setter the extension calls: a track's mute,
  solo, rec arm, selection, volume, and pan, a route's, and the automation
  override) isn't a refresh there; the deferred refresh's end is, if one was
  made in it.
- **Bounds:** `CallCountTest` (`plugin/default_config/call_count_test.cc`)
  runs JPRSurf's own surface on the large project (`AddLargeProject()`, 128
  tracks in folders, with buses and sends), and bounds the calls of starting
  up, a second of steady state in each mode, each gesture the surface tests
  make, and the infrequent events, as exact `CallCounts`. A regression fails,
  and so does an improvement, until its bound is lowered in the same change. A
  failure lists each function's count, to show what changed.

Weighting the counts by what each call costs in REAPER, measured on its own
(*Measure REAPER's costs*), estimates what they cost.

**What still needs REAPER:** `dll_main.cc`, whether tests make the calls on the
surface that REAPER does (settled by traces), whether the fake X-Touch agrees
with the hardware, what REAPER's own UI shows, the undo points REAPER adds
between runs, and REAPER's times, with its effect on JPRSurf's (the benchmarks
time JPRSurf's own code alone). The smoke test in CLAUDE.md checks those by hand.

## Getting there

Game Bits' backlog has the generic pieces: *Function hooks*, then *Profiler
module*. They are done in Game Bits sessions, ahead of the JPRSurf items that
depend on them.

JPRSurf's backlog items build the rest in order:
1. *REAPER API list*: the list, and loading through it.
2. *Profiler*: JPRSurf's use of `gb/profile`, ending with the snapshot.
3. *Trace REAPER calls*: records what REAPER calls on the surface, for tests
   that make those calls.
4. *Fake REAPER*: one clock, resettable process state, the fake, and tests of
   `common`.
5. *Fake X-Touch and device tests*.
6. *Surface tests*: the plugin split, the harness, and the smoke test as tests.
7. *Scene tests*.
8. *Check the fakes in REAPER*: contract tests of the fake and
   `SurfaceNotifier`, run against both.
9. *REAPER call count tests*.
10. *Measure REAPER's costs*: what each function costs in REAPER, which the
    call count tests weigh calls by. The smoke profile then goes.

One more waits until it is needed: *Profile snapshots on demand*, if a whole
session mixes too much together.

This work comes before the rest of the config work, so that work can use it and
needs less testing by hand.

CLAUDE.md changes along the way: the include rule for `reaper_api.h` with the
API list, reading performance from the snapshot with the profiler, the testing
rules (code that depends on REAPER can now be unit tested, so side sessions can
verify it) with the fake, and the smoke test with surface tests.

**With the config work.** Two config items assumed things these fakes change,
and *Fake X-Touch and device tests* updated them, once devices could be
created in tests:
- *Device types and catalogs*: a device can be created in a unit test, on the
  fake hardware's ports, so the check that a device's controls match its
  catalog is a unit test as well as a check at startup.
- *Build the scene from a SurfaceSpec*: building is unit tested against the
  fake too, and surface tests check that the scene built from JPRSurf's spec
  behaves as the C++ surface did.
- Backlog items that are "only worth doing if it shows up in the `Run()`
  average" can be measured with the profiler first.
