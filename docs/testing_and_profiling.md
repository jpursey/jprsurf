# Testing and Profiling

JPRSurf is heading toward code that is unit tested without REAPER or hardware,
and toward a profile of where each run's time goes: to JPRSurf's own code, or
to REAPER's. This doc defines the pieces both are built from, and how they fit
together.

Both need a way to get between JPRSurf and REAPER, and REAPER's API already
provides one: every function is a global pointer that the extension fills in
when it loads. Replacing those pointers gives the profiler its timing and gives
tests a fake REAPER, without changing any code that calls REAPER.

It ties together the backlog items that build toward this. Until they land,
code that depends on REAPER is tested by hand in REAPER, and performance is
read from the `Run()` log line.

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
- **The fake says how it knows.** It stores what is set and returns what is
  stored. Anything more, such as what REAPER notifies or how it formats text,
  is modeled only once it has been seen in REAPER, and says so where it is
  modeled.
- **Behavior time comes from REAPER.** Timers and timeouts read the run's time,
  which comes from REAPER's clock, so tests control it and never wait. Only
  measurement reads a real clock.
- **Profiling is always on, within a budget.** It costs no more than 20us a
  run or 1% of the run's total time, whichever is more, and it measures its own
  cost to show that it does.
- **Counts in tests, times on the machine.** Call counts are deterministic, so
  tests check them. Times depend on the machine, so they are kept in local
  snapshots and compared there.
- **Generic pieces go in Game Bits.** Hooking function pointers and the
  profiler itself know nothing of REAPER, so they are `gb/base` and
  `gb/profile`. Anything that knows REAPER or the X-Touch stays in JPRSurf.

## The pieces at a glance

| Piece            | What it is                                                                    | Where                     |
| ---------------- | ----------------------------------------------------------------------------- | ------------------------- |
| API list         | Every REAPER function JPRSurf calls, and loading them                         | `jpr/common/reaper_api.h` |
| Function hooks   | Replace a function pointer with a wrapper of the same signature               | `gb/base/function_hook.h` |
| Profiler         | Timed points, frames, counters, and the report                                | `gb/profile`              |
| REAPER profiling | The profiler on the API list, the surface's entry points, runnables, and MIDI | `jpr/common`              |
| Trace            | Every REAPER call and callback with its arguments, to see what REAPER does    | `jpr/common`              |
| Fake REAPER      | REAPER's state, MIDI ports, surfaces, and clock, behind the API list          | `jpr/common/testing`      |
| Fake X-Touch     | The hardware end of an X-Touch's MIDI ports                                   | `jpr/device/testing`      |
| Surface harness  | The plugin loaded into the fake, with fake X-Touches                          | `jpr/plugin/testing`      |

The `testing` directories are test-only libraries, linked by unit tests and
never by the plugin.

## The REAPER boundary

Everything that passes between JPRSurf and REAPER goes one of four ways:

| Way             | Direction            | Today                                                                                   |
| --------------- | -------------------- | --------------------------------------------------------------------------------------- |
| API functions   | JPRSurf calls REAPER | 59 of REAPER's 857 functions, from 13 files, 9 of them in `common`                      |
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
| Automation, undo, project | `GetGlobalAutomationOverride`, `SetGlobalAutomationOverride`, `Undo_OnStateChangeEx`, `Undo_CanRedo2`, `IsProjectDirty`                                                                                            |
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
- A hook doesn't have to call its original, so one can stand in for a function
  that was never loaded. The fake uses one for every function it doesn't
  implement, which fails the test naming the function.
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

**Today:** behavior reads two clocks. The runners read `time_precise()` for
`RunTime`, and `Control`'s press timers use it, but `ControlSurface::Run()`
reads `absl::Now()` for the visibility poll and `ContinuousUndo::Update()`,
and `ContinuousUndo::OnChange()` reads `absl::Now()` itself. Each `Runner`
also reads the clock for itself, so the device and scene runners in one run
see different times. After the change, a run has one time, which the runners
are given, and `absl::Now()` is only used for measurement.

Game Bits has `gb::Clock` and `gb::FakeClock`, which would also work, but
they add a second way to fake the same clock, and need a `Clock*` that undo and
`ControlSurface` can reach.

## Profiler

### What it measures

| Kind         | What                                                                                                                       | Named                  |
| ------------ | -------------------------------------------------------------------------------------------------------------------------- | ---------------------- |
| Frame        | Each `ControlSurface::Run()`                                                                                               | `Run`                  |
| Entry points | Every call REAPER makes into JPRSurf: each `IReaperControlSurface` callback, including each `Extended()` call, and loading | `csurf/SetSurfaceMute` |
| REAPER calls | Every function on the API list, and every call on a MIDI object                                                            | `reaper/GetTrack`      |
| Scopes       | Known heavy work in JPRSurf, such as `TrackCache::Refresh()` and `RefreshVisibility()`                                     | `TrackCache::Refresh`  |
| Runnables    | Each runner's runnables, by the name they were registered under                                                            | `runner/Control`       |
| Counters     | Work done, which the time scales with: MIDI messages in and out, mappings and properties updated, track setters            | `midi_out/messages`    |
| Values       | The workload, recorded in the snapshot: tracks, routes, devices, views, mappings, properties                               | `tracks`               |

- **Runnables** are registered with a name (`AddRunnable("Control", ...)`),
  which is required, so a runnable can't be added without one. Runnables with
  the same name are run and timed together, so a hundred `Control` runnables
  cost one pair of timer reads. The order runnables run in is arbitrary today,
  so grouping them changes nothing a caller can rely on.
- **Counters** go on the work whose cost grows with it. `Scene::OnRun()` scales
  with its views, mappings, and properties, and `MidiIn` and `MidiOut` with the
  messages they handle, so the snapshot can show time per item as well as
  totals.
- **Values** are set when they change, such as when `TrackCache` is refreshed
  or a scene is built, and cost nothing per run.

### Nesting

Every timed point records its **self time**: its time less the time of the
points inside it. This matters because REAPER calls back into JPRSurf from
inside some calls. `SetTrackUIMute()` can call `SetSurfaceMute()` on the
surface, and `Main_OnCommand()` can run anything. The callback is an entry
point inside a REAPER call, so its time is JPRSurf's, not REAPER's. Summing
self times splits every run into JPRSurf's time and REAPER's, and the profiler
estimates its own share (see [Cost and budget](#cost-and-budget)).

Time REAPER spends outside JPRSurf's calls, such as a redraw it defers to its
own loop, isn't seen.

### Cost and budget

The budget is the larger of 20us a run and 1% of the run's total time, JPRSurf
and REAPER together. The floor keeps steady state cheap, and the percentage
takes over for heavy runs, crossing at 2ms.

- A timed point is two reads of the CPU's timestamp counter (`__rdtsc`), and
  adding the difference into a fixed slot, with no lookups: an estimated
  20-25ns. The 20us floor allows about 800 a run.
- The profiler measures the cost of a timed point when it starts, counts the
  points each run times, and reports its own cost per run against the budget.
  It logs a warning if its average over the log interval goes over.
- Counting alone costs almost nothing, so the first profiler CL counts every
  call before any timing is added. That shows how many timed points a run
  would have before committing to timing them all.
- If profiling can't meet the budget, the fallback is a build option that
  leaves the hooks uninstalled and compiles scopes out.

### Output

- **The `Run()` log line** stays, every 5 seconds, computed from the profile,
  with the profiler's own cost added.
- **Slow runs:** a run over a threshold of a few milliseconds logs a warning
  with its biggest points by self time. The warning is limited to one per log
  interval.
- **The snapshot:** when the surface is destroyed (REAPER exiting, or the
  surface being removed in preferences), the profile is written to
  `jprsurf_profile.txt` next to `jprsurf.log`, replacing the last one, just as
  the log is replaced. Keeping one means copying it somewhere outside the repo
  (`out/` is transient). It is plain text, one line per point in a stable
  order, so two snapshots diff cleanly:

```
JPRSurf profile
Build:    40f0906 (modified)
Date:     2026-09-28 14:32, 612s, 18360 runs
Workload: 142 tracks, 310 routes, 2 devices, 97 views, 1204 mappings, 880 properties
Profiler: 1.9us/run (budget 20us)

Run          avg    p50    p90    p99    max    JPRSurf  REAPER
             27us   24us   41us   180us  3.1ms  15us     12us

Point                    kind    calls/run  self/call  self/run  max
csurf/Run                entry   1          ...
reaper/GetTrackUIVolPan  call    32         ...
runner/Control           scope   1          ...
...

Slowest run (3.1ms)
TrackCache::Refresh      scope   ...
...
```

(The numbers are only illustrative.)

Percentiles come from a histogram of each frame's time, with a few buckets for
each power of two, which is cheap to add to. The slowest run's breakdown is
kept by copying that run's points when it beats the slowest so far, which is
rare.

### Game Bits and JPRSurf

The Game Bits side is two items in Game Bits' own backlog, *Function hooks* and
*Profiler module*, which state it in Game Bits' terms. This is how the work
divides, and what JPRSurf needs from them.

| `gb/profile`                                                                 | `jpr/common`                                                                                     |
| ---------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------ |
| The timestamp counter, and converting it to time                             | Hooks on every function in `JPR_REAPER_API`, and the MIDI object wrappers                        |
| Named points of each kind, registered once, in a fixed array                 | Entry points in `ControlSurface`, and `Run()` as the frame                                       |
| Self time through nesting, frames, counters, values, and the frame histogram | Named runnables in `RunRegistry`, and the scopes and counters in `common`, `device`, and `scene` |
| The slowest frame's breakdown, the profiler's own cost, and the budget check | The snapshot's build, workload, and file, and the `Run()` log line                               |
| The text report                                                              |                                                                                                  |

```
// A sketch of gb/profile. Macros define each point once, where it is used.
#define GB_PROFILE_SCOPE(name) ...        // Times the enclosing scope.
#define GB_PROFILE_COUNT(name, count) ... // Adds to a counter.

class Profiler {
 public:
  void BeginFrame();
  void EndFrame();
  void SetValue(std::string_view name, int64_t value);

  // What has been counted, for tests.
  int64_t GetCount(std::string_view name) const;

  void WriteReport(std::ostream& out) const;
  void Reset();
};
```

Tests use the same profiler: installed over the fake, its call counts are what
call count tests check.

## Trace

A trace hooks every function on the list, and every surface callback, and logs
each call in order with its arguments and result, to `jprsurf_trace.txt`.
Output parameters (a non-const pointer to a number) are logged with their
values after the call, and tracks by their index and name rather than their
pointer. It is off unless the `JPRSURF_TRACE` environment variable turns it on
when the plugin loads, and costs nothing while off.

It is how the fake learns what REAPER does. Setting a track's mute in REAPER
and on the surface, with a trace running, shows which callbacks REAPER sends,
in what order, and whether they come during the call or later. Each behavior
the fake models points to the trace that showed it.

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

  // The project, changed as the user would change it in REAPER. Changes
  // notify the surfaces as REAPER does.
  FakeTrack* AddTrack(std::string_view name, FakeTrack* parent = nullptr);
  void DeleteTrack(FakeTrack* track);
  FakeTrack* GetMasterTrack();

  // MIDI ports, listed under these names.
  FakeMidiInput* AddMidiInput(std::string_view name);
  FakeMidiOutput* AddMidiOutput(std::string_view name);

  // Adds or removes the registered control surface, as the user does in
  // REAPER's preferences.
  void AddSurface(std::string_view config = {});
  void RemoveSurface();

  // Advances the clock by one frame (1/30s), and runs the surface.
  void Run();

  // Runs frames until `duration` has passed.
  void RunFor(absl::Duration duration);

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

The fake is literal by default: a setter stores the value, and a getter returns
it. Anything REAPER does beyond that is modeled only where it has been seen,
each with a comment on how it was seen (usually a trace):
- **Notifications** are the main one: which setters call the surface back (such
  as `SetSurfaceMute()` after `SetTrackUIMute()`), whether the surface that
  made the change is called too, and whether the call comes during the setter
  or later. A surface that relies on the notification to update its own state
  shows stale lights under a fake that gets this wrong, so it is the first thing
  traces settle.
- **Text** REAPER formats: `mkvolstr`, `mkpanstr`, `format_timestr_pos`, and
  command names, in the formats traces showed.
- **Changes made as the user**, through `FakeReaper`'s own methods, notify the
  surface as REAPER does: `SetTrackListChange()` when tracks are added or
  removed, and the matching callback for each property.

Tests are then of two kinds, which need little of REAPER's own behavior:
- **Output:** given REAPER's state and some input, what JPRSurf asked REAPER to
  do, and what it sent to the hardware.
- **Input and response:** REAPER's state, or a notification, set up before the
  next run, and what JPRSurf does in response.

### Checks

The fake also checks rules for using REAPER that are only prose today, and
fails the test when one is broken:
- **Batching:** changes to UI-visible properties of more than one track, within
  one entry point, must all be inside one `PreventUIRefresh()` scope. This is
  the `TrackBatch` rule in CLAUDE.md, enforced.
- `PreventUIRefresh()` must be balanced by the end of each entry point.
- A call with a deleted track's pointer fails, where REAPER might crash.
- A call to a function the fake doesn't implement fails, naming it, so the fake
  can grow with the tests that need it.
- At teardown, a surface, plugin, or MIDI port still open fails.

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
| The run's time                                             | The fake                                                         |
| The API table                                              | The fake: back to functions that fail the test                   |
| The profiler's counts                                      | The fake                                                         |

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
and `AddSurface()` creates it through the recorded `create`.

This needs the plugin split into a library and a DLL:
- `jpr_plugin` is a static library with `plugin.cc` and `plugin_surface.cc`,
  which tests link.
- `reaper_jprsurf` is the DLL: `dll_main.cc` with `DllMain`, and the exported
  `REAPER_PLUGIN_ENTRYPOINT`, which moves there from `plugin.cc`. A static
  library would drop an export nothing references.

`dll_main.cc` is then the only code no test runs.

## Fake X-Touch

`CreateMIDIInput()` and `CreateMIDIOutput()` are on the API list, so the fake
lists named ports (`"X-Touch"`, `"X-Touch-Ext"`) and returns its own:
- A **fake input** queues events, and delivers them on the next
  `SwapBufsPrecise()`.
- A **fake output** records every `Send()` and `SendMsg()`.

A `FakeXTouch` is the hardware end of a pair of them:
- **Out of JPRSurf:** it decodes what is sent into the hardware's state: each
  button's light (off, on, or blinking), fader positions, encoder rings, meters,
  scribble strip text and colors, and the timecode display.
- **Into JPRSurf:** press, release, touch, move, and turn, encoded as the
  hardware sends them.

It is written from the protocol (the Mackie Control messages the X-Touch
speaks, and its scribble strip sysex), as tables, and not from
`DeviceXTouch`'s code, so a misreading of the protocol isn't copied into both.
The device's own tests also check raw messages.

A test through the whole surface then reads like the smoke test:

```
TEST_F(SurfaceTest, MuteButtonMutesTrack) {
  FakeTrack* track = reaper_.AddTrack("Drums");
  AddSurface();
  xtouch_.Press(FakeXTouch::kMute, /*strip=*/0);
  reaper_.Run();
  EXPECT_TRUE(track->mute);
  EXPECT_EQ(xtouch_.GetLight(FakeXTouch::kMute, /*strip=*/0), kLightOn);
}
```

## Tests

| Library  | Tested against               | What                                                                                                                                                                  |
| -------- | ---------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `common` | The fake                     | `Track`, `TrackCache`, `TrackBatch`, `Timeline`, MIDI ports, `ContinuousUndo`, and `ControlSurface`'s callbacks and single instance rule                              |
| `device` | The fake, and fake X-Touches | Every control's inputs and outputs on `DeviceXTouch`                                                                                                                  |
| `scene`  | The fake, and fake X-Touches | Properties against REAPER's state, views (conditions, subjects, lists, references), mappings (modifiers, taps, picks), and `TrackActions` (ranges, anchors, grouping) |
| `plugin` | The surface harness          | The smoke test: faders, pots, buttons, select presses, navigation, modes, transport, timecode, meters, and scribble strips                                            |

Existing tests that need no REAPER stay as they are.

**Call count tests** check performance where it is deterministic. A generated
project (100+ tracks, with sends and receives) is run in the harness, and a
test bounds the REAPER calls in a steady state run, and in each infrequent
event (a track list refresh, a bank change, a mode change). A bound is set to
the count when the test is written, so a regression fails, and an improvement
lowers it in the same change. Weighting the counts by each call's time in a
local snapshot estimates the time they cost on that machine.

**What still needs REAPER:** `dll_main.cc`, whether the fake agrees with REAPER
(settled by traces), whether the fake X-Touch agrees with the hardware, what
REAPER's own UI shows, and times. The hand smoke test shrinks to those.

## To confirm

Facts the design depends on that nobody has checked yet, with the item that
checks each:

| Fact                                                                                                                   | Checked by                  |
| ---------------------------------------------------------------------------------------------------------------------- | --------------------------- |
| How many REAPER calls and runnables a steady state run makes with a large project                                      | *Profiler*, counting first  |
| What a timed point costs with `__rdtsc`, and that the timestamp counter is invariant on this machine                   | Game Bits *Profiler module* |
| That REAPER destroys the surface before unloading the plugin at exit, so the snapshot is written                       | *Profiler*                  |
| Which setters notify surfaces, whether the surface making the change is notified, and whether during the call or later | *Trace REAPER calls*        |
| What calls back into the surface during `Main_OnCommand()` and `PreventUIRefresh()`                                    | *Trace REAPER calls*        |
| What REAPER calls on a surface as it is created, and in what order                                                     | *Trace REAPER calls*        |
| The formats of `mkvolstr`, `mkpanstr`, `format_timestr_pos`, and `kbd_getTextFromCmd`                                  | *Trace REAPER calls*        |

## Getting there

Game Bits' backlog has the generic pieces: *Function hooks*, then *Profiler
module*. They are done in Game Bits sessions, ahead of the JPRSurf items that
depend on them.

JPRSurf's backlog items build the rest in order:
1. *REAPER API list*: the list, and loading through it.
2. *Profiler*: JPRSurf's use of `gb/profile`, ending with the snapshot.
3. *Trace REAPER calls*: settles what the fake models before it is written.
4. *Fake REAPER*: one clock, resettable process state, the fake, and tests of
   `common`.
5. *Fake X-Touch and device tests*.
6. *Surface tests*: the plugin split, the harness, and the smoke test as tests.
7. *Scene tests*.
8. *REAPER call count tests*.

Two more wait until they are needed: *Profile snapshots on demand*, if a
whole session mixes too much together, and *Tests inside REAPER*, if the fake
turns out to disagree with REAPER in ways traces don't catch.

This work comes before the rest of the config work, so that work can use it and
needs less testing by hand.

CLAUDE.md changes along the way: the include rule for `reaper_api.h` with the
API list, reading performance from the snapshot with the profiler, the testing
rules (code that depends on REAPER can now be unit tested, so side sessions can
verify it) with the fake, and the smoke test with surface tests.

**With the config work.** Two config items assume things these fakes change.
They are updated by *Fake X-Touch and device tests*, once devices can be
created in tests, not before:
- *Device types and catalogs* says a device can't be created in a unit test,
  because it needs MIDI ports. Under the fake it can, so the check that a
  device's controls match its catalog can be a unit test as well as a check at
  startup.
- *Build the scene from a SurfaceSpec* says building needs REAPER. Under the
  fake, building is unit tested too, and surface tests check that the scene
  built from JPRSurf's spec behaves as the C++ surface did.
- Backlog items that are "only worth doing if it shows up in the `Run()`
  average" can be measured with the profiler first.
