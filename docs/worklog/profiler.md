# Profiler

An always-on profile of where each run's time goes: to JPRSurf's own code, or
to REAPER's. It is built on Game Bits' `gb/profile`, and costs no more than 20us
a run or 1% of the run's total time, whichever is more. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Profiler); this is the
plan to build it.

Nothing changes on the surface. What changes is what JPRSurf writes:
- **`jprsurf_profile.txt`**, next to `jprsurf.log`, written when the surface is
  destroyed (REAPER exiting, or the surface being removed in preferences),
  replacing the last one: the build, the workload, the run summary, and one
  line per point.
- **A quiet log.** The `Run()` line every 5 seconds goes. The log only has a
  warning when a run is out of spec (a slow run, with its biggest points, at
  most one every 5 seconds), and one summary line when the surface is
  destroyed, with the profiler's own cost against its budget.

## Design

### Names

| Name                                      | What                                                                                | Might be confused with                                                                   |
| ----------------------------------------- | ----------------------------------------------------------------------------------- | ---------------------------------------------------------------------------------------- |
| `ReaperProfiler`                          | Owns the `gb::Profiler` and the hooks, and writes the snapshot and warnings         | `ReaperTrace`, its sibling for every call; `gb::Profiler`, which it owns                 |
| `ProfiledMidiInput`, `ProfiledMidiOutput` | A `midi_Input` or `midi_Output` that times every call on the REAPER object it wraps | `MidiIn` and `MidiOut`, JPRSurf's ports, which own one of these                          |
| snapshot                                  | `jprsurf_profile.txt`                                                               | The design doc's word for it; "report" is `gb::Profiler::GetReport()`, which it contains |

Alternatives to `ReaperProfiler`: `SurfaceProfiler` (reads as belonging to
`ControlSurface`'s listener), `RunProfile` (it covers callbacks between runs
too). `ReaperProfiler` matches `ReaperTrace`, which hooks the same boundary.

Points are named by where they are, with a prefix for each kind of place:

| Prefix      | Kind        | Example                                                |
| ----------- | ----------- | ------------------------------------------------------ |
| `csurf/`    | frame/scope | `csurf/Run` (the frame), `csurf/SetSurfaceMute`        |
| `csurf/`    | scope       | `csurf/CSURF_EXT_SETPAN_EX`, one per `Extended()` call |
| `reaper/`   | call        | `reaper/GetTrack`                                      |
| `midi_in/`  | call        | `midi_in/SwapBufsPrecise`                              |
| `midi_out/` | call        | `midi_out/Send`                                        |
| `runner/`   | scope       | `runner/Control`                                       |
| `Class::`   | scope       | `TrackCache::Refresh`, `Scene::SyncMappings`           |
| (none)      | value       | `tracks`, `routes`, `views`, `mappings`                |

A call point's count is its call count, so the MIDI messages sent are
`midi_out/Send` and `midi_out/SendMsg`, with no counter of their own.

### ReaperProfiler (common)

```
// Profiles JPRSurf's runs for as long as it exists: every function on the
// REAPER API list, every call on a MIDI port REAPER creates, and every point
// JPRSurf defines with gb/profile. It is created with the control surface,
// before its listener, and destroyed after it.
class ReaperProfiler final {
 public:
  ReaperProfiler();

  // Writes the snapshot, and logs a summary of it.
  ~ReaperProfiler();
};
```

- It lives in `common`, as `ControlSurface` owns it. `ControlSurface::Create()`
  makes it after the single instance check and before the listener, so
  creating the listener (opening MIDI ports, building the scene) is profiled as
  `csurf/Create`. The surface destroys its listener first, then the profiler.
- It is per surface, not per process: a surface removed and added again starts
  a new profile, and the snapshot is always one surface's whole life. The
  design doc's process state table changes to say so (the fake needs no reset
  for it).
- The hooks are `ReaperApiHooks<ReaperCallHook>`, where `ReaperCallHook` is a
  `gb::ProfileCallHook` named `reaper/<name>`, plus hooks on
  `CreateMIDIInput()` and `CreateMIDIOutput()` that wrap the port they return.
  They are installed over whatever is loaded, so over a trace when one runs
  (the trace's own cost then shows as REAPER's, which is fine for a debugging
  aid that is far too slow to leave on).
- `gb::Profiler` options: `budget_per_frame` 20us, `budget_fraction` 0.01,
  `slow_frame` 5ms.

**Performance:** each timed point is about 12ns (measured by Game Bits), and
about 1ns with no profiler. The profiler measures its own cost per run, and the
snapshot and the summary line report it against the budget. Nothing runs per
run beyond the timed points themselves.

**Brittleness:** `gb::Profiler` CHECK-fails if a timed point ends out of order,
or if the profiler is destroyed inside one. Every point is a scoped object, and
the profiler is destroyed by the surface's destructor, which REAPER never calls
from inside one of the surface's own callbacks. A new REAPER function is
profiled automatically, as the hooks come from the API list. A new call on a
MIDI object needs a method on the wrapper, but the wrappers implement every
virtual method of `midi_Input` and `midi_Output`, so there is none to miss.

### Entry points (common)

Each `IReaperControlSurface` callback in `ControlSurface` opens with a
`gb::ProfileScope`, and `Run()` with the `gb::ProfileFrame`. `Extended()` times
each case of its switch under the `CSURF_EXT_` name, and anything else as
`csurf/Extended`. A callback REAPER makes from inside a REAPER call (such as
`SetSurfaceMute()` during `PreventUIRefresh(-1)`) is charged to JPRSurf, not to
the call. Callbacks REAPER might make on another thread aren't recorded, as the
profiler only records its own thread.

### Named runnables (common)

```
// `name` groups runnables: those with the same name run together, and are
// timed together as the scope point runner/<name>.
RunHandle AddRunnable(std::string_view name, Runnable runnable);
```

- A name is required, so a runnable can't be added without one. Today's
  callers are `MidiIn` and `MidiOut` (`common`), `Control` (`device`), and
  `Scene` (`scene`).
- A hundred `Control` runnables cost one pair of timer reads. A counter,
  `runner/<name>/runnables`, counts how many ran, so the snapshot shows time
  per runnable.
- The order runnables run in is arbitrary today, so grouping them changes
  nothing a caller can rely on. A runnable removed during a run still doesn't
  run later in that run.

### Device and scene

- **Device:** counters for what controls do, which their time scales with:
  inputs handled (`control/inputs`) and outputs sent (`control/outputs`).
- **Scene:** scopes on the parts of `Scene::OnRun()`
  (`Scene::UpdateReferences`, `Scene::UpdateState`,
  `Scene::ApplyViewConditions`, `Scene::SyncMappings`), counters for the views
  and mappings synced each run, and the workload as values (`devices`,
  `controls`, `views`, `mappings`, `properties`), set when the scene is
  activated.
- **Common workload:** `tracks` and `routes`, set by `TrackCache::Refresh()`,
  which is a scope, as is `TrackCache::RefreshVisibility()`.

### Output (common)

- **The snapshot** is the Game Bits report after JPRSurf's header:
  ```
  JPRSurf profile
  Build:    40f0906 (modified)
  Date:     2026-09-28 14:32, 612s
  Per run:  JPRSurf 15us, REAPER 12us
  ```
  The report already has the values (the workload), the run summary with the
  profiler's cost against its budget, and every point. The per run split sums
  the self time of every call point (REAPER) and every other timed point
  (JPRSurf), including callbacks between runs. The build is the git commit,
  and whether the tree was modified, generated at build time by CMake. It is
  written with `std::fopen`, as one string written once needs no abstraction
  (and no iostreams).
- **No periodic log line.** Today's `Run()` line every 5 seconds goes, with
  `ControlSurface`'s own run timing. The snapshot has everything it showed, and
  more. If a view of a session in progress turns out to be needed, that is the
  backlog's *Profile snapshots on demand*, rather than a periodic line.
- **Slow runs:** a run over 5ms logs a warning with Game Bits' report of that
  run, limited to one every 5 seconds, so a burst of slow runs doesn't flood
  the log.
- **The summary line**, logged when the snapshot is written, with a warning
  instead if the profiler was over its budget:
  ```
  Profile: 18360 runs, p50 24us, p99 180us, max 3.1ms, avg 27us (JPRSurf 15us, REAPER 12us), profiler 1.9us/run (budget 20us)
  ```

### To confirm

| Fact                                                                                           | Checked by |
| ---------------------------------------------------------------------------------------------- | ---------- |
| REAPER creates the surface and calls `Run()` on the same thread, so runs are recorded          | CL1        |
| What a timed point costs inside REAPER's process, against Game Bits' 12ns                      | CL1        |
| Whether `Run()` is re-entered during a modal dialog an action opens (nested frames CHECK-fail) | CL1        |
| How many REAPER calls a steady state run makes with a large project, and the profiler's cost   | CL2        |
| How many runnables a steady state run makes, and the cost with every point in place            | CL4        |

Findings so far:
- **CL1 (2026-09-29, 81 tracks, 161s):** runs are recorded (5012 frames), so
  REAPER creates the surface and runs it on the same thread. A timed point
  costs 11.8ns inside REAPER, matching Game Bits' measurement. Runs averaged
  69.6us (p50 27us, p99 514us, max 19.7ms). Creating the surface took 108ms,
  about 100ms of it opening the four MIDI ports. `absl::LocalTimeZone()` is UTC
  inside REAPER, so the snapshot uses `GetLocalTimeZone()` (`local_time.h`).
  Against a baseline session without the profiler (5048 runs, avg 75.3us,
  32.9us in quiet intervals, max 22.8ms), CL1's `Run()` log line (30.5us in
  quiet intervals) shows no regression; the difference is noise.
- **CL1, modal dialogs:** pressing Save in an unsaved project opens the Save As
  dialog from inside `Run()` (in `Main_OnCommand()`). REAPER doesn't call
  `Run()` again while it is open, so frames don't nest. That run lasts until
  the dialog closes (26.5s and 10.8s in the test), and surface input waits in
  REAPER's MIDI buffer until the next runs read it. Such a run skews the
  average, and so the budget's fraction of it (370us in that session), but
  not p50 or p99, so the summary line (CL7) leads with those.

**Counting first.** The backlog asks for calls to be counted before they are
timed, to see how many points a run would time against the budget. Game Bits'
profiler now measures its own cost per run, so CL2 times the calls and reads
both the count and the cost from the snapshot.

**If it is over budget,** most of the calls are likely queries that are always
cheap (such as `GetTrack()` and `GetMediaTrackInfo_Value()`), so the first
fallback is to stop timing those, chosen by their time per call in the
snapshot. They can still be counted, for a few adds and no timer reads, so call
counts stay complete, and their time is charged to the JPRSurf point that makes
them. Only if that isn't enough is there a CMake option that turns profiling
off.

## CLs

### CL1 [x] common: ReaperProfiler, the frame, and the snapshot

Depends on: nothing.

- `jpr_common` links `gb_profile`.
- `reaper_profiler.h/.cc`: `ReaperProfiler`, owning the `gb::Profiler`, and
  writing the snapshot (the report, with a header of the date and duration;
  the report has the runs, as frames) when destroyed.
- `ControlSurface` creates it in `Create()` and owns it. `Run()` is the frame
  `csurf/Run`, and `Create()` is timed as `csurf/Create`.
- The design doc's process state table: the profiler is per surface.

**Verify**
- Standard checks.
- Close REAPER: `jprsurf_profile.txt` has the runs, and `csurf/Run` and
  `csurf/Create` with plausible times. The point cost is recorded under To
  confirm.
- Run an action that opens a modal dialog from a surface button (such as a
  command mapping for File: Save project as, or Preferences), leave it open a
  few seconds, and close it. REAPER must not crash. If it does, `Run()` is
  re-entered, and CL1 times only the outer run as the frame.
- Performance: the `Run()` log line's avg and max against before the change,
  with a large project.

### CL2 [ ] common: Time every REAPER call

Depends on: CL1.

- `ReaperCallHook`, and `ReaperApiHooks<ReaperCallHook>` in `ReaperProfiler`.

**Verify**
- Standard checks, including with `JPRSURF_TRACE` set (hooks stack over the
  trace).
- The snapshot has a `reaper/` line for each function called, with calls per
  run. Record the steady state calls per run and the profiler's cost against
  its budget under To confirm, with a large project. If it is over budget, stop
  and apply the fallback (see Counting first).

### CL3 [ ] common: Entry points, MIDI ports, and TrackCache

Depends on: CL2.

- A scope on every `ControlSurface` callback, and on each `Extended()` call.
- `ProfiledMidiInput` and `ProfiledMidiOutput`, and the hooks on
  `CreateMIDIInput()` and `CreateMIDIOutput()` that wrap what they return.
- Scopes on `TrackCache::Refresh()` and `RefreshVisibility()`, and the values
  `tracks` and `routes`.

**Verify**
- Standard checks.
- The snapshot: `csurf/` scopes for callbacks between runs (such as
  `SetSurfaceSelected` after selecting a track in REAPER), `midi_in/` and
  `midi_out/` calls that move with pressing buttons and moving faders, and the
  workload values matching the project.

### CL4 [ ] common: Named runnables

Depends on: CL3.

- `RunRegistry::AddRunnable()` takes a name, groups runnables by it, and times
  each group as `runner/<name>`, counting its runnables.
- The callers pass names: `MidiIn`, `MidiOut`, `Control`, and `Scene`. The
  `device` and `scene` changes are one line each, so they come with the API
  change rather than in their own CLs.

**Verify**
- Standard checks, especially the smoke test, since every control and the
  scene run through the changed registry.
- The snapshot has the four `runner/` scopes with runnables per run. Record the
  steady state runnables per run and the profiler's cost under To confirm.

### CL5 [ ] device: Control counters

Depends on: CL4.

- `control/inputs` and `control/outputs` counters in `Control`.

**Verify**
- Standard checks.
- The counters move with pressing buttons and moving faders, and outputs with
  changes in REAPER.

### CL6 [ ] scene: Scene scopes, counters, and workload

Depends on: CL5.

- Scopes on the parts of `Scene::OnRun()`, counters for views and mappings
  synced, and the workload values, set in `Scene::Activate()`.

**Verify**
- Standard checks.
- The snapshot's workload values match the scene, and the `Scene::` scopes add
  up to about `runner/Scene`.

### CL7 [ ] common: A quiet log, slow runs, and the build

Depends on: CL6.

- `ControlSurface` loses its own run timing and the `Run()` log line.
- The slow run warning, limited to one every 5 seconds.
- The summary line when the snapshot is written, or a warning if over budget.
- The build in the snapshot's header, generated by CMake at build time, and the
  per run JPRSurf and REAPER split.
- CLAUDE.md's Performance section reads performance from the snapshot, the
  summary line, and slow run warnings.

**Verify**
- Standard checks.
- The log has no line per interval, and the summary line on closing REAPER,
  with the profiler's cost under its budget.
- A slow run (a track list refresh on a large project, or a temporary 10ms
  sleep in a test mapping) logs one warning with its breakdown.
- The snapshot's header has the commit, and "(modified)" with local changes.
