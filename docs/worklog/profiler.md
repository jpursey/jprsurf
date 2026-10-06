# Profiler

An always-on profile of where each of JPRSurf's runs spends its time: in
JPRSurf's own code, or in REAPER's. It is built on Game Bits' `gb/profile`,
and costs about 1.5us a run, against a budget of 3us or 1% of the run,
whichever is more. The design it came from is in
[testing_and_profiling.md](../testing_and_profiling.md) (Profiler).

## Behavior

Nothing changes on the surface. What changes is what JPRSurf writes:
- **`jprsurf_profile.txt`**, beside `jprsurf.log`, replaced each time the
  surface is destroyed (REAPER exiting, or the surface removed in
  preferences). It covers that surface's runs, from once it was created:
  ```
  JPRSurf profile
  Build:    8afe455 (modified)
  Date:     2026-09-29 22:55, 157s
  Per run:  JPRSurf 37.3us, REAPER 92.2us
  ```
  then Game Bits' report: the workload values, the run summary (count, total,
  average, p50, p90, p99, max, and the profiler's cost against its budget), one
  line per point with its count and self time (in total, per run, and per
  call), and the slowest run's breakdown.
- **A quiet log.** The `Run()` line every 5 seconds is gone, and so are the
  track list refresh and view activation timing lines. What is left:
  - "Slow run (N so far)", for a run over 8ms, with Game Bits' breakdown of
    that run by point, at most one every 5 seconds (`LOG_EVERY_N_SEC`).
  - "Profile: …" when the snapshot is written: runs, p50, p99, max, and the
    average with its JPRSurf and REAPER split, and the profiler's cost against
    its budget. It is a warning, "Profile over budget: …", if the profiler went
    over.

CLAUDE.md's Performance section says how to read these to check a change.

## Names

A point that times one function is named for it, as C++ names it. Counters and
workload values have plain names.

| Point                  | Kind    | Examples                                                                     |
| ---------------------- | ------- | ---------------------------------------------------------------------------- |
| A run                  | frame   | `ControlSurface::Run`                                                        |
| JPRSurf's own code     | scope   | `TrackCache::Refresh`, `Scene::UpdateReferences`, `View::SyncMappings`       |
| A runner's run         | scope   | `Runner: Device`, `Runner: Scene`, `Runner: MidiIn`, `Runner: MidiOut`       |
| REAPER API functions   | call    | `GetTrack`, `Main_OnCommand`                                                 |
| Methods on a MIDI port | call    | `midi_Input::SwapBufsPrecise`, `midi_Output::Send`                           |
| Work done              | counter | `MIDI messages in`, `Device runnables`, `control inputs`, `mappings synced`  |
| Workload               | value   | `tracks`, `routes`, `devices`, `controls`, `views`, `mappings`, `properties` |

REAPER's functions are global, and JPRSurf's points are all qualified by a
class, so the bare names don't collide. If one ever did with another kind,
Game Bits CHECK-fails when the second is registered. Game Bits' report groups
points by kind, so REAPER's calls sit together.

A call point's count is its call count, so the MIDI messages sent are
`midi_Output::Send` and `midi_Output::SendMsg`, with no counter of their own.

## Structure

### common: ReaperProfiler (reaper_profiler.h/.cc)

`ReaperProfiler` owns the `gb::Profiler`, and the hooks that time REAPER:
- **REAPER's API:** `ReaperApiHooks<gb::ProfileCallHook>` hooks every function
  on the API list (`reaper_api.h`), each named for its function, so a newly
  listed function is timed with no more work. The hooks go over whatever is
  loaded, including a trace's hooks when `JPRSURF_TRACE` is set; the trace's
  cost then shows as REAPER's.
- **MIDI ports:** hooks on `CreateMIDIInput()` and `CreateMIDIOutput()` wrap
  each port REAPER returns in a `ProfiledMidiInput` or `ProfiledMidiOutput`
  (private to the `.cc`). These implement every virtual method of the SDK
  class, so no call is missed, and time each call except opening and closing
  the port (`start()`, `stop()`, and `Destroy()`), which happen once. An
  input's read buffer isn't wrapped: reading it is a walk over memory that a
  timer would cost more than, so `MidiIn::Poll()` counts `MIDI messages in`
  instead.
- **Its life:** `ControlSurface` holds it as a member declared before the
  listener, so its hooks wrap the ports the listener opens, and it outlives
  the listener. The surface calls `Reset()` once the listener is created, so
  one time costs of starting up (opening ports, building the scene) are left
  out. It is destroyed with the surface, and writes the snapshot and logs the
  summary then. So a profile is per surface, not per process: a surface removed
  and added again starts a new one.
- **The split:** the self time of every call point is REAPER's, and of every
  other timed point JPRSurf's, per run. Game Bits' totals include time outside
  any run, which is why the profile leaves out starting up and opening and
  closing ports. What is left outside runs is small: the devices' and MIDI
  outputs' last run as the surface is destroyed, and REAPER's callbacks
  between runs, which only set flags.
- **The build:** `build_info.cmake` writes `jpr/common/build_info.h` on every
  build, with the git commit and whether tracked files have changes. It only
  rewrites the header when it changes, so a build with no change recompiles
  nothing.
- **The file** is written with `std::fopen`, as one string written once needs
  no abstraction (and no iostreams). Its date uses Game Bits'
  `GetLocalTimeZone()` (`gb/base/local_time.h`), as `absl::LocalTimeZone()` is
  UTC inside REAPER.

**Brittleness:** `gb::Profiler` CHECK-fails if a timed point ends out of order,
or if it is reset or destroyed inside one. Every point is a scoped object; the
reset is in the surface's constructor and the destruction in its destructor,
neither of which REAPER calls from inside a timed point.

### common: what is timed

- **The run:** `ControlSurface::Run()` is the frame. The other
  `IReaperControlSurface` callbacks aren't timed, as each only sets a flag or a
  version for the next run to act on. A callback REAPER makes from inside a
  REAPER call is charged to that call. A callback that starts doing real work
  gets a scope then.
- **Runners:** `Runner` takes a name, times each run as `Runner: <name>`, and
  counts `<name> runnables`. Each runner holds one kind of runnable (`MidiIn`
  and `MidiOut` in `MidiPorts`, `Device` and `Scene` in `PluginSurface`), so a
  name per runner says as much as one per runnable, and a hundred controls cost
  one pair of timer reads.
- **TrackCache:** `Refresh()` and `RefreshVisibility()` are scopes, and
  `Refresh()` sets `tracks` and `routes` (counting each send once).

### device: Control

`Control` counts `control inputs` (each input handled, in its value, delta,
and press handlers) and `control outputs` (each output sent, in
`SendPendingOutput()`, which clearing a control goes through too).

### scene: Scene and View

- `Scene::OnRun()` times its parts: `Scene::UpdateReferences`,
  `SceneStateProperty::UpdateState` (all of them together),
  `Scene::ApplyViewConditions`, and `View::SyncMappings` (around the root
  view's, which syncs the rest).
- `View::SyncMappings()` counts `views synced` and `mappings synced`, for each
  active view.
- `Scene::SetWorkloadValues()` walks the scene for `devices`, `controls`,
  `views`, `mappings`, and `properties`. It is called when the scene is
  activated, and again when it is destroyed, before the snapshot is written,
  so the snapshot has everything ever added to the scene however it was built
  (and after the profiler's reset).

## REAPER facts

Found while building this, on an 81-track project:
- REAPER creates the surface and calls `Run()` on the same thread, which is
  the thread the profiler records.
- An action that opens a modal dialog from inside `Run()` (Save As from the
  surface's Save button, in an unsaved project) blocks that run until the
  dialog closes. REAPER doesn't call `Run()` again meanwhile, so runs don't
  nest. Surface input waits in REAPER's MIDI buffer until the next runs read
  it.
- Opening the four X-Touch MIDI ports takes about 80ms, and closing the two
  inputs about 50ms each.
- A single mute from the surface costs about 2.5ms in `PreventUIRefresh(-1)`,
  REAPER's batched UI refresh.

## Building blocks

- **`ReaperProfiler` (common/reaper_profiler.h)**: profiles a surface's runs.
  Declare it before anything that opens MIDI ports, and `Reset()` it once
  startup is done.
- **`Runner(name)` (common/runner.h)**: a runner times its own runs, so a new
  kind of runnable only needs its own runner.
- **`gb::GetLocalTimeZone()` (gb/base/local_time.h)**: the local time zone, for
  any time JPRSurf writes, as `absl::LocalTimeZone()` is UTC inside REAPER.
- **`build_info.h` (generated in `jpr_common`)**: the build's commit, and
  whether it was modified.

## Performance

On an 81-track project (2 devices, 243 controls, 36 views, 595 mappings):
- **The profiler** times about 120 points a run, at about 12ns each, for about
  1.5us a run. Idle sessions with and without each CL, minutes apart, were
  within noise.
- **An idle run** is about 40us: 30us of JPRSurf and 11us of REAPER's reads.
  JPRSurf's time is mostly the scene (about 15us, 10us of it syncing 446
  mappings at about 23ns each) and the controls (about 12us for the 145
  controls that run, about 80ns each). REAPER's reads are led by
  `GetTrackState` (17 a run, about 165–250ns each) and
  `GetToggleCommandState` (14 a run, about 100ns each).
- **Events** are REAPER's time: `Main_OnCommand` from about 4ms to over 100ms,
  `PreventUIRefresh` about 3ms, and `Undo_OnStateChangeEx` about 1.5ms. A
  track list refresh is about 90us of JPRSurf's own time.
- **Between sessions**, the same build's run times varied by about 30%. The
  cause is the kind of core the frames ran on, not the profiler's tick rate:
  frames on efficiency cores take about 25-30% longer, and sessions with REAPER
  in the foreground, which stay on performance cores, agree within about 10%
  (see `profiles/README.md`).
