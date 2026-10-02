# Fake REAPER

A test-only library, `jpr/common/testing`, that holds REAPER's state in memory
and implements the REAPER API list over it, so code that depends on REAPER can
be unit tested, and verified in a side session. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Time, Fake REAPER);
this is the plan to build it, with the tests of `common` that come with it.

There is no change in behavior. Two changes come first, and are tested by hand
in REAPER like any other change: every run has one time, read from REAPER's
clock, and the process state in `common` can be reset by the fake. With the
second, the plugin, rather than `common`, says where the profile is written.

## Design

### Names

| Name                              | What                                                                         | Might be confused with                                                                               |
| --------------------------------- | ---------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------------------- |
| `FakeReaper`                      | REAPER's state in memory, loaded as the API. One at a time.                  | `ReaperTrace` and `ReaperProfiler`, which hook the API rather than implement it                      |
| `FakeTrack`                       | A track the fake holds. A `MediaTrack*` points to one.                       | `Track`, `common`'s cached view of a track; the file-local `FakeTrack` in `reaper_trace_test.cc`     |
| `FakeMidiInput`, `FakeMidiOutput` | The fake's `midi_Input` and `midi_Output`                                    | `MidiIn`, `MidiOut`, `common`'s ports; the profiler's `ProfiledMidiInput` and `ProfiledMidiOutput`   |
| `TestReset`                       | Registers a reset of process state, which only `FakeReaper` runs             | `MidiOut::ResetAllState()`, which resets a port's sent state                                         |
| `RunTime::Now()`                  | Reads REAPER's clock as a `RunTime`                                          | `absl::Now()`, which is real time, for measurement only                                              |

`TestReset` says it is a reset, and for tests. Alternatives were
`ResetRegistration` (wordier) and `RegisteredReset` (reads as the function, not
its registration). CL2 first gated public reset methods with a `ResetTestKey`;
once each reset registered itself, the resets could be private, and the key
went.

### One clock (common)

```
struct RunTime {
  // Reads REAPER's clock, time_precise().
  static RunTime Now();

  double precise;       // Time in seconds, with high precision.
  unsigned int coarse;  // Time in milliseconds, with lower precision.
};

class ControlSurfaceListener {
  // `time` is when the run started, which every runner in the run is given.
  virtual void OnRun(const RunTime& time) {}
};

class Runner final : public RunRegistry {
  void Run(const RunTime& time);
};
```

- `ControlSurface::Run()` reads `RunTime::Now()` once. It uses it for the
  visibility poll, and passes it to `ContinuousUndo::Update()` and the
  listener's `OnRun()`, which passes it to each `Runner::Run()` (and
  `MidiPorts::RunInput()` and `RunOutput()`). The runners no longer read the
  clock themselves.
- `ContinuousUndo::Update(time)` moves to the start of the run, before the
  listener. It creates the pending undo point if the changes stopped `kDelay`
  ago, as before, and keeps `time` as the time `OnChange()` records for changes
  until the next run. Why the move is safe is written where `Run()` calls it,
  and in `ContinuousUndo::Update()`'s comment (see below).
- The runs outside `Run()`, when `PluginSurface` and `MidiPorts` are destroyed,
  read `RunTime::Now()`.
- `absl::Now()` is then only used for measurement: the profiler, the trace,
  and the log.

**Why moving `Update()` is safe.** Today it runs after the listener, but with
the run's start time, so it can never flush a change the listener made in the
same run: that change's time is later than the start. It only ever flushes
changes from earlier runs. Moved, it flushes on the same run as before (the
first whose time is `kDelay` past the last change), just before the listener
rather than after it. What that run's listener does then comes out the same, or
better:
- **Another continuous change:** today `OnChange()` flushes the stale changes
  first itself, so the undo points and their order are the same.
- **An action with its own undo point** (mute, solo, rec arm, a route's mute):
  these call `Flush()` first, so the same. One that didn't would today get its
  undo point before the continuous one; now it comes after, in the order they
  happened.
- **A change with no undo point** (such as selection): today it is folded into
  the continuous undo point, though it came after those changes stopped; now it
  isn't.

**Brittleness:** a runner could still be run with a time read separately. There
is one `Run()`, and every runner is run from it, so this is easy to see in
review.

### The profile is the plugin's (common, plugin)

`common` shouldn't know which plugin it is in, but `ControlSurface` names the
profile file itself (`GetLogPath("jprsurf_profile.txt")`). The path moves into
the surface's type, which the plugin registers:

```
struct Type {
  ...
  // Where each surface writes its profile when it is destroyed, or empty for
  // no profiling (see ReaperProfiler).
  std::filesystem::path profile_path;
};
```

- `ControlSurface` holds its `ReaperProfiler` in a `std::optional`, still
  declared before the listener, and creates it only when there is a path.
- `PluginSurface::Register()` gives `GetLogPath("jprsurf_profile.txt")`, and
  logs the error `ReaperProfiler` logs today if there is nowhere to write it.
- Tests register a type with no path, so they write nothing. A test that wants
  the profiler's counts (*REAPER call count tests*) gives a path in its temp
  directory.
- Tracing needs nothing: only `Plugin` starts it, from `JPRSURF_TRACE`.

The rest of `common`'s references to JPRSurf (the profile's header and run
split, the trace's header, the build info, and some comments) are the backlog's
*Keep common free of the plugin's name*.

### Process state (common)

```
// For tests only: registers a function that resets process state, which
// FakeReaper calls when it is created and destroyed, and nothing else can.
// Defined in the .cc file that holds the state: at namespace scope for a
// file-local global, or as a private static member for a class's own state.
//   const TestReset kResetRulerModes([] { g_last_ruler_modes = {}; });
//   const TestReset TrackCache::s_test_reset_([] { ... });
class TestReset final {
 public:
  explicit TestReset(void (*reset)());
 private:
  friend class FakeReaper;
  static void ResetAll();
};
```

| State                                           | Reset                                                                           |
| ----------------------------------------------- | ------------------------------------------------------------------------------- |
| `TrackCache`, `ContinuousUndo`                  | Their `s_test_reset_` deletes the instance. They move off `absl::NoDestructor`  |
| `ControlSurface`'s registered type              | Its `s_test_reset_` forgets it, and CHECKs no instance exists                   |
| `timeline.cc`'s `g_last_ruler_modes`            | `kResetRulerModes`                                                              |
| `g_modifiers`                                   | `kResetModifiers`                                                               |
| `MidiPorts`' flush wait (100ms, on destruction) | `MidiPorts::SetFlushWait(duration)`, a plain setting, zero under the fake       |

- **Each reset registers itself**, with a `TestReset` beside the state it
  resets, and the fake calls every registered one. So every library is treated
  the same: `scene` and `device` register theirs the same way, and there is no
  list to keep. A class's reset is the initializer of a private static member,
  which can reach its private state, so no reset is public. A registration is
  linked into a binary exactly when its state is: a binary only links the
  files of a library it uses, and a dynamic initializer is never stripped. A
  file holding nothing but registrations would be left out, so they go beside
  the state. The flush wait isn't registered, as it is a setting, not state:
  the fake sets it.
- `TrackCache::Get()` and `ContinuousUndo::Get()` create their instance on
  first use, as today, from a file-local `g_` pointer rather than a function
  static, so a reset can replace it. The cost of `Get()` is the same null check
  a function static makes.
- `~Track()` no longer calls back into `TrackCache` to clear the last touched
  track. The cache never drops a track while it exists (a deleted track clears
  it as it is removed), so it only mattered as the cache was destroyed, and a
  track `scene` still holds after a reset would have created a new cache.
- The fake resets everything when it is created and when it is destroyed. It
  fails the test first if a surface is still open, as a reset under a live
  surface would leave it holding tracks that no longer exist.
- The flush wait is real time, which a test must never wait on. It is only
  there for REAPER's own ports, so it is test only, unlike the profile.
- **Not reset here:** `scene`'s own globals (`g_last_auto_override` in
  `state_properties.cc`), and `Plugin`'s instance and trace. *Scene tests* and
  *Surface tests* register their resets.

**Brittleness:** a new global needs a `TestReset` beside it. The `g_` and `s_`
prefixes make them easy to find in review, and anything with an instance is
caught by the fake's teardown check, but a missed global is otherwise silent.

### FakeReaper (common/testing)

```
// REAPER's state in memory, loaded as the REAPER API for as long as it exists.
// Only one may exist at a time.
class FakeReaper final {
 public:
  // Loads the fake as the REAPER API, and resets the process state in common.
  FakeReaper();

  // Fails the test if a surface or MIDI port is still open, or a
  // PreventUIRefresh() is unbalanced, then resets the process state, and
  // unloads the API.
  ~FakeReaper();

  // What REAPER passes the plugin's entry point. Register("csurf") records the
  // surface type.
  reaper_plugin_info_t& GetPluginInfo();

  // Creates a control surface of the registered type, as REAPER does when the
  // user adds one in preferences, and returns it (null if the type refused).
  // A test makes the calls REAPER would make on it, and destroying it removes
  // the surface.
  std::unique_ptr<TestControlSurface> AddSurface(std::string_view config = {});

  // Advances the clock, for a test with no surface to run.
  void AdvanceTime(absl::Duration duration);

  // What time_precise() returns.
  double GetTime() const;

  // The open projects, one per project tab, and the current one.
  int GetProjectCount() const;
  FakeProject& GetProjectAt(int index);
  FakeProject& GetProject();

  // Opens a new tab, switches tabs, or replaces the current tab's project (as
  // File: New project tab, clicking a tab, and File: New project do). Only a
  // replaced project is closed, so only its tracks no longer exist.
  FakeProject& AddProject();
  void SwitchProjectTo(int index);
  FakeProject& NewProject();
  ...
};

// A project: its tracks, and the rest of the state REAPER keeps per project.
class FakeProject final {
 public:
  FakeTrack* GetMasterTrack();
  FakeTrack* AddTrack(std::string_view name, FakeTrack* parent = nullptr);
  void DeleteTrack(FakeTrack* track);
  ...
};
```

- **Projects:** the state REAPER keeps per project (tracks, selection, routes,
  and later the transport and undo) is in a `FakeProject`, one for each open
  project tab, so a test can see what switching tabs, or opening another
  project, does. A track in any open project is valid, and keeps its pointer
  across tab switches, as in REAPER, which is how `TrackCache` gets its tracks
  back on switching back. The API works on the current project when it is
  given no `ReaProject*`, as JPRSurf always does. Standard test projects, such
  as a folder hierarchy or a large project with routes, are functions that fill
  in a `FakeProject`, added as tests need them.

- **Which functions:** exactly those in `JPR_REAPER_API`, the functions JPRSurf
  loads, and nothing else the SDK declares. The fake's table is generated from
  that list, so it grows with it rather than with REAPER's versions.
- **Loading:** `GetPluginInfo().GetFunc` returns the fake's function for every
  name on the list, so `LoadReaperApi()` succeeds as it does in REAPER, and
  returns null for anything else. The constructor loads the API through
  `LoadReaperApi()` itself, so a test of `common` needs no plugin.
- **Every listed function is faked.** While CL3 to CL8 build the fake up by
  area, a function without its fake yet gets a stub of the same signature that
  fails the test, naming it. CL8 fakes the last of them and removes the stub,
  so from then on, adding a function to the list without a fake doesn't
  compile.
- **REAPER's state, not its behavior.** Every function on the list is a C
  function pointer, stubbed as the trace and profiler hook them, and the MIDI
  ports are the SDK's C++ interfaces, implemented by the fake's own classes.
  Both work on the fake's model of the project, as far as the libraries query
  it: a setter stores the value, a getter returns it, and a test builds the
  project with `AddTrack()` and the like, and reads and sets a `FakeTrack`'s
  fields directly (`track->mute`). A test that needs one function to behave
  otherwise hooks it over the fake with `gb::FunctionHook`.
- **The fake never calls the surface by itself.** Most code under test
  (`device`, `scene`, and most of `common`) never sees `IReaperControlSurface`,
  so it needs none of it. A test of `ControlSurface` (or later, of the plugin)
  makes the calls REAPER would make on the `TestControlSurface` `AddSurface()`
  returns, such as `SetTrackListChange()` after adding tracks, and `Run()`,
  which advances the clock by one run. `TestControlSurface` passes each call on
  to the surface, and the fake checks it when it returns. JPRSurf only ever has
  one surface (more surfaces would be more listeners on one `ControlSurface`),
  so the fake holds one at a time: a type that creates a second while one is
  open fails the test. What REAPER actually
  sends, and when, is in the design doc's Seen in traces, for writing those
  tests.
- **Track groups** are modeled simply: a `FakeTrack` has a `group`, and a
  grouped change to its mute, solo, rec arm, volume, or pan changes every track
  in the group. REAPER's groups have leaders and followers for each property,
  which aren't modeled. It lets a test check the grouping JPRSurf asks for
  through the tracks' state.
- **Text** from `mkvolstr`, `mkpanstr`, `format_timestr_pos`, and
  `kbd_getTextFromCmd` is a plain format of the fake's own. The code under test
  only passes it through to the display, so a test checks that it does, not
  REAPER's exact format.
- **Commands:** `Main_OnCommand()` records the command, and runs its handler if
  the test gave it one. A handler for the ruler modes or undo sets what those
  commands would.
- **Tracks** are owned by the fake, and a deleted track is kept until the fake
  is destroyed, so its pointer is never reused, and a call with it is caught.
  GUIDs are made from a counter, so they are the same on every run of a test.
- **What a test sets, and what the project keeps:** a track's own values are on
  `FakeTrack`, for a test to set directly. What relates tracks to each other,
  or must stay unique (order, folders, routes, and GUIDs), is the project's,
  which keeps it consistent, so a test can't build a state REAPER couldn't be
  in. The project keeps it in one record per track (from CL6). A field fixed
  when the project creates an object is `const`, such as a `FakeRoute`'s ends.
- **Where it lives:** `src/jpr/common/testing`, the `jpr_common_testing`
  library, which links `jpr_common` and gtest (it reports failures with
  `ADD_FAILURE()`), and which `jpr_common_test` links through
  `jpr_common_TEST_DEPS`. The plugin never links it. Files by area:
  `fake_reaper.h/.cc` (loading, surfaces, clock, checks, and the API
  functions, in `FakeReaper::Api`), `fake_project.h/.cc` and `fake_track.h`
  (the state a test reads and writes directly), and `fake_midi.h/.cc` (MIDI
  ports).

**Performance:** none in REAPER; the plugin never links it.

### Checks

Each fails the test where it happens, with `ADD_FAILURE()`, naming what broke:
- **The `TrackBatch` rule:** an entry point that changes the mute, solo, rec
  arm, or selection of more than one track makes them one change: one
  `PreventUIRefresh()` scope, or one call outside of any. Each change costs a
  refresh of REAPER's UI (2–17ms), and JPRSurf adds an undo point for each, so
  a batch is one of each. Volume and pan aren't included: REAPER makes their
  undo points itself, and `CSurf_OnVolumeChangeEx()` measured about 10us a
  call, so there is no refresh to save.
- **`PreventUIRefresh()` unbalanced** at the end of an entry point.
- An **entry point** is each call on a `TestControlSurface`, and the test's
  own calls, which are checked when the fake is destroyed.
- **A deleted or unknown track pointer** passed to any function.
- **At teardown:** a surface or MIDI port still open. The fake then closes it,
  so the next test starts clean.

The checks have their own tests (`fake_reaper_test.cc`), each broken on
purpose, with gtest's `EXPECT_NONFATAL_FAILURE`.

### To confirm

Nothing. The fake doesn't model REAPER's own behavior, so it depends on no
facts about it.

## CLs

### CL1 [x] common, plugin: One clock

Depends on: nothing.

- `RunTime::Now()`, and `Runner::Run(const RunTime&)`; `RunRegistry` no
  longer reads the clock.
- `ControlSurface::Run()` reads the time once, and passes it to
  `ContinuousUndo::Update()` (moved to the start of the run), and to
  `OnRun(const RunTime&)`. The visibility poll uses it.
- `ContinuousUndo::OnChange()` records the time `Update()` was last given.
  `Update()`'s comment, and the call in `Run()`, say why it is safe before the
  listener (see Why moving `Update()` is safe).
- `MidiPorts::RunInput()` and `RunOutput()` take the time.
- `PluginSurface::OnRun()` passes the time to its runners, and its destructor
  reads `RunTime::Now()`.
- The design doc's Time section: "Today" becomes what was built.

**Verify**
- Standard checks.
- Double and long press on select still work, with the same timing.
- Move a send fader for a few seconds, stop, and move it again after a second:
  two undo points, each named for the send. Moving a track fader then a send
  fader quickly: one undo point, as before (REAPER folds its open track volume
  undo point into the next one it is given; see the backlog's *Separate undo
  points for track faders*).
- Hide a track in the Track Manager: the surface follows within a second.
- Performance: a short idle run, committed as `profiles/fake_reaper/idle.txt`,
  against `profiles/idle.txt`: p50 and p99, and one `time_precise` call per
  run instead of one per runner.

### CL2 [x] common, plugin: Resettable process state, and the plugin's profile

Depends on: CL1.

- `reset_test_key.h`: `ResetTestKey`.
- `TrackCache` and `ContinuousUndo` move to a file-local instance with
  `Reset(ResetTestKey)`, and `~Track()` no longer reaches the cache.
- `ControlSurface::Reset(ResetTestKey)`, `ResetRulerModes(ResetTestKey)`, and
  `MidiPorts::SetFlushWait(ResetTestKey, absl::Duration)`.
- `ControlSurface::Type::profile_path`, the optional `ReaperProfiler`, and
  `PluginSurface::Register()` giving the path.
- The resets are unused until CL3, so no visible change.

**Verify**
- Standard checks, including `jprsurf_profile.txt` still written on exit.
- Performance: a short idle run, replacing `profiles/fake_reaper/idle.txt`,
  against CL1's and `profiles/idle.txt`: p50 and p99 (`TrackCache::Get()` is
  on every path).

### CL3 [x] common/testing: FakeReaper, surfaces, clock, and checks

Depends on: CL2.

- The `jpr_common_testing` library, and `jpr_common_test` linking it.
- `FakeReaper`: loading (functions not faked yet fail the test), the plugin
  info and `Register("csurf")`, `AddSurface()` and `RemoveSurface()`, the
  clock (`time_precise`, `Run()`, `RunFor()`), `ShowConsoleMsg` (recorded),
  the process state reset, and the teardown and unknown track checks. The
  entry point checks come with `PreventUIRefresh()`, in CL4.
- `TestReset`, registered beside each piece of `common`'s process state,
  replacing CL2's `ResetTestKey`, its public resets, and `ResetCommonState()`.
- `FakeProject`, and project tabs (`AddProject()`, `SwitchProjectTo()`, and
  `NewProject()`), with just enough of a project for `ControlSurface` to run:
  an empty project with a master track
  (`CountTracks`, `GetMasterTrack`, `GetTrackGUID`, `GetTrackState`,
  `GetTrackUIVolPan`, `GetTrackColor`, and `guidToString`), which CL4 fills
  out.
- Tests: `fake_reaper_test.cc` (the checks), and `control_surface_test.cc`:
  registering once, the single instance rule (a second surface is refused and
  shows a console message), `OnRun()` getting the fake's time, the listener
  destroyed with the surface, and `SetTrackListChange()` refreshing
  `TrackCache` on the next run.

**Verify**
- Standard checks. The plugin changes only in the resets' registrations and
  `FormatGuid()`, so REAPER loading it with a clean log is enough.
- `ctest` passes in Release, and leaves `jprsurf_profile.txt` untouched.

### CL4 [x] common/testing: Tracks and selection

Depends on: CL3.

- Tracks: order and folders (`FakeProject` keeps them consistent), name,
  color, volume, pan, mute, solo, rec arm, `GetTrackState` flags, automation
  mode, TCP and mixer visibility, and peak. `GetTrackNumSends` returns 0 until
  CL5, as a track list refresh reads every track's routes.
- Selection: `SetTrackSelected`, `SetOnlyTrackSelected`, and the
  `CountSelectedTracks`/`GetSelectedTrack` pairs, with and without the master.
- `Undo_OnStateChangeEx` (moved up from CL8), recorded in the project, as
  `TrackBatch` adds undo points.
- `TestControlSurface`, which `AddSurface()` returns in place of the surface,
  and which runs it (`Run()` and `RunFor()`, moved from `FakeReaper`, which
  keeps `AdvanceTime()`), and checks each call. `RemoveSurface()` goes:
  destroying it removes the surface.
- `PreventUIRefresh`, entry points, and the unbalanced `PreventUIRefresh`,
  `TrackBatch` rule, and deleted track checks.
- `FakeProject::AddTrack()` and `DeleteTrack()`.
- Track groups, simply (see Track groups above), so the grouping `Track` asks
  for is tested through the tracks' state.
- Tests: `track_test.cc` (values, setters, undo points, `SelectOnly()`,
  deleted tracks, and the meter), `track_cache_test.cc` (refresh, folders and
  filters, visibility, GUID lookup across delete and re-add, selection,
  automation modes, the last touched track), and `TrackBatch` (one undo point,
  and the check failing without it).

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- `ctest` passes.

### CL5 [x] common/testing: Routes

Depends on: CL4.

- Sends, receives, and hardware outputs: `GetTrackNumSends`,
  `GetSetTrackSendInfo` (`P_DESTTRACK`, `P_SRCTRACK`), the `UI` getters and
  setters, and `ToggleTrackSendUIMute`, with REAPER's indexing (receives as
  `-1 - index` in the send functions, sends after hardware outputs). REAPER
  only documents receives in the setters; the fake's getters take them the
  same way. Route changes aren't grouped, and add no undo point (REAPER's own
  for `ToggleTrackSendUIMute` isn't modeled, like those for track volume and
  pan).
- `FakeProject::AddSend(from, to)`, `AddHardwareOutput(from)`, and
  `DeleteRoute()`. The project owns its routes (each a source, destination,
  volume, pan, and mute, with no destination for a hardware output, and its
  ends `const`), so a send and its receive are one route, and can't disagree.
  It also keeps each track's sends, receives, and hardware outputs, which only
  adding and removing a route change, so a lookup is a span, with no scan.
  Deleting a track removes its routes. A send to or from the master, or to
  itself, fails the test, as REAPER can't make one.
- Tests: the fake's indexing, and each end listing a route, in
  `fake_reaper_test.cc`, and routes in `track_test.cc`: building them on
  refresh, notifying when they change, deleting a track, `RefreshRoutes()`,
  each setter by send and receive index, and route mute following REAPER and
  flushing pending undo.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- `ctest` passes.

### CL6 [ ] common/testing: Track records

Depends on: CL5.

- One `TrackRecord` per track in `FakeProject`, the master's included, in an
  `absl::node_hash_map` keyed by the track, so a record never moves: the
  track, which it owns whether or not it is deleted, its GUID, its parent, its
  sends, receives, and hardware outputs, and whether it is deleted. It
  replaces `parents_`, `track_routes_`, `deleted_tracks_`, and
  `deleted_track_storage_`, and `tracks_` keeps only the order.
- `FakeTrack` keeps only the values a test sets: `guid` moves to the record,
  which a test reads with `FakeProject::GetGuid()`. `GetTrackGUID()` returns
  the record's, which stays put for the track's life, as REAPER's does, and a
  zero GUID for an unknown track, so the scratch track needs none. The rule
  (see What a test sets, and what the project keeps) goes in `FakeTrack`'s and
  `FakeProject`'s class comments.
- `FakeProject::RestoreTrack(deleted)`, as undo brings back a deleted track: a
  new track (so a new pointer) with the deleted one's GUID and values, added
  as `AddTrack()` adds one, under its old parent if that is still in the
  project. Its routes aren't restored; a test adds them again.
- Tests: `fake_reaper_test.cc` (records across adding, deleting, and
  restoring, and GUIDs staying put), and `track_cache_test.cc` restoring a
  track with `RestoreTrack()` in place of copying its GUID.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- `ctest` passes.

### CL7 [ ] common/testing: MIDI ports

Depends on: CL3.

- `FakeMidiInput` (queues events with their time, and delivers them in a
  `MIDI_eventlist` on the next `SwapBufsPrecise()`) and `FakeMidiOutput`
  (records every `Send()` and `SendMsg()`), listed by `AddMidiInput(name)` and
  `AddMidiOutput(name)`.
- `GetNumMIDIInputs`/`Outputs`, the names, and `CreateMIDIInput`/`Output`.
- Tests: `midi_ports_test.cc` (listing, opening by name, reopening, and final
  output sent on destruction) and `midi_port_test.cc` (listeners by status and
  data, event times, state messages sent only on change, and sysex).

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- `ctest` passes, with no real wait (the flush wait is zero).

### CL8 [ ] common/testing: Transport, commands, automation, and undo

Depends on: CL4 to CL7 (the last of the list).

- Transport and timeline: play state, play and cursor positions, and
  `format_timestr_pos`.
- Commands: `Main_OnCommand` (recorded, with handlers), toggle states,
  `NamedCommandLookup`, and `kbd_getTextFromCmd`.
- Automation and undo: the global override, `Undo_CanRedo2`, and
  `IsProjectDirty`.
- The rest of the list: `AnyTrackSolo`, `CountSelectedMediaItems`,
  `mkvolstr`, `mkpanstr`, `stringToGuid`.
- The stub for functions not faked yet goes, so a listed function without a
  fake doesn't compile.
- Tests: `timeline_test.cc` (ruler modes, and remembering the last one),
  `undo_test.cc` (merging, `kDelay` on the fake's clock, `Flush()`, a different
  description), `automation_test.cc`, and `guid_test.cc`.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- `ctest` passes.

### CL9 [ ] docs: Testing rules

Depends on: CL8.

- CLAUDE.md: code that depends on REAPER can be unit tested against the fake,
  and so verified in a side session, with what still needs REAPER.

**Verify**
- The docs read correctly, and the links work.
