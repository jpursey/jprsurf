# Measure REAPER's costs

What each function on the API list costs in REAPER, measured on its own, under
the conditions its cost depends on, rather than through the default config's
gestures: inside a deferred refresh (a `PreventUIRefresh()` scope) or not, and
as it scales with the project (tracks, selected tracks, and routes). The end of
a deferred refresh, an undo point, and each action JPRSurf runs are measured
the same way. No device or config is involved: the measurements call REAPER's
API directly, on generated projects, in the test install that
`check_in_reaper` uses.

The result refines the cost table the call count tests weigh calls with
(`GetCallCost()` in `jpr/common/testing/call_cost.h`), which today comes from
the profile snapshots, and is conservative where nobody has measured: each
function's category, which setters are really deferrable, and how each cost
scales with the project. It is measured again when the API list grows, or
REAPER updates, rather than with each change.

The goal is to decide whether new code meets the performance requirements
deterministically, measuring nothing but JPRSurf's own code: a gesture's cost
in REAPER is its calls, bounded by the call count tests and weighed by the
table, and JPRSurf's own time comes from the benchmarks under the fake. So the
smoke test goes entirely, its profile (`profiles/smoke.txt`) and its scenario
with it. What isolated measurements miss, REAPER's effect on JPRSurf's own
code, the idle profile and the log's "Slow run" warnings show.

The figures only need to be rough. JPRSurf isn't responsible for how long
REAPER takes, and has no control over REAPER's own UI or how a user has
configured it; what matters is putting each call in the right coarse category,
so JPRSurf uses REAPER efficiently. So the measurements run in the test install
as it opens, a controlled environment that is the same each time, and nothing
tries to match any particular user's setup.

There is no change in behavior: the plugin doesn't change.

## Design

### Names

| Name                | What                                                                                          | Might be confused with                                               |
| ------------------- | --------------------------------------------------------------------------------------------- | -------------------------------------------------------------------- |
| `CostMeasurement`   | One thing measured under one condition: its project, the call, and the call that undoes it    | `CallCost`, the category the table gives a function                  |
| `MeasuringSurface`  | The control surface whose runs step through the measurements in the test install             | `RecordingSurface`, which the contract tests run from                |
| Later work          | REAPER's time on its UI thread in the runs after a call, beyond what the same runs cost idle  | A deferred refresh, which puts work off to its end, not between runs |
| `reaper_jpr_measure`| The DLL that runs the measurements in the test install                                        | `reaper_jpr_check`, which runs the contract tests                    |
| `measure_in_reaper` | The build target that runs it                                                                 | `check_in_reaper`, and the `benchmark` target                        |

Alternatives considered: `CostProbe` (jargon), `CostSample` (a measurement is
many samples); `CostRunner` and `CostSession` for the surface (`Runner` is
`common/runner.h`, and "session" is the user's work in REAPER); "deferred
time" for later work (collides with a deferred refresh, which is a different
putting off).

### How a cost is measured

Some of a call's cost comes after it returns, when REAPER redraws its UI from
its own loop, which no call's time shows. So a measurement times two things:
- **The call:** the wall time inside the call, in CPU ticks
  (`gb::ReadCpuTicks()`). A call that waits, on a file or on REAPER's audio
  thread, holds up the UI thread as much as one that works.
- **Later work:** the cycles REAPER's UI thread ran for
  (`gb::GetThisThreadCycles()`) from the start of the run that made the call,
  through the 12 runs after it (about 0.4 seconds), less the call, against the
  same span of runs with no call. Thread cycles count only work, not the
  timer's wait between runs, so later work shows even though the runs come at
  the same rate. The surface runs on REAPER's UI thread, so it reads that
  thread's cycles at the start of each run. The span is long enough to catch
  the undo point REAPER adds for a fader about 250ms after it moves (see To
  confirm).

A measurement alternates samples with and without the call, so drift in what
REAPER does idle cancels out, and reports the median of each, with the spread.
A setter alternates its value (mute on, then off), so each call changes
something, and an action is paired with what undoes it where it needs one
(Play, then Stop). A read changes nothing, so it has no later work: it is
timed over many calls in one run, one for each track (or route) in turn, as
the same track read over and over stays in the cache and reads several times
faster than the profiles show.

**Conditions**, each measured where it can matter, with the arguments the
extension calls each function with:
- **Deferred refresh:** the extension only changes REAPER inside a run, which
  is a deferred refresh, so every change is measured inside one, where the
  call and the end of the deferred refresh are timed apart. The route setters
  too, to find whether they are really not deferrable. A mute and a send's
  volume are also measured outside one, to compare.
- **Tracks:** 28, 128 (the large project), and 508 tracks, from
  `AddLargeProject()` with 2, 12, and 50 folders, for the reads that may scale
  and a mute, rec arm, selection, and undo point.
- **Every track in one deferred refresh:** a mute, rec arm, and selection of
  every track, as a ranged gesture makes them, to show whether the end grows
  with the setters before it.
- **Selected tracks:** one, and every track, for the selection reads, and a
  mute and fader ganged across the selection, as the extension's grouped
  changes are.
- **Routes:** a track's send, and a bus's receive (31 receives), for route
  reads and setters, as an edit in progress (`isend` 0).

What can't be measured in the test install is listed with why, and keeps its
category: the MIDI port functions (the machine's ports, called only as the
surface starts), `ShowConsoleMsg()` (opens a window), and Transport: Record
(it records media, and stopping it may ask what to do with the files). Every
other action runs in the test install, the saves included, which write into
the run directory, as the project opened from there. A test checks that every
function on the API list is measured or listed, as the cost table's
`static_assert` does.

### common/testing: Measurements (cost_measurement.h)

```
// One thing measured under one condition: a read or a change.
struct CostMeasurement {
  std::string name;                  // "SetTrackUIMute", matched by the filter
  std::string condition;             // The project: "128 tracks", ...
  bool in_deferred_refresh = false;  // The surface says so in the results.

  absl::AnyInvocable<void(FakeProject&)> build;  // An empty project if unset.
  absl::AnyInvocable<void()> prepare;  // Untimed, once the project is open.

  absl::AnyInvocable<int()> read;    // Returns how many calls it made.
  absl::AnyInvocable<void()> call;   // A change: exactly one of these is set.
  absl::AnyInvocable<void()> reset;  // After a change's window, if needed.
};

// Every measurement, in the order they run (CL4).
std::vector<CostMeasurement> GetCostMeasurements();
```

- They call only REAPER's API, so they run under the fake too: `ctest` runs
  each one for a single sample, so a measurement that no longer works fails
  there rather than in the test install.

### common/testing: MeasuringSurface (measuring_surface.h)

- An `IReaperControlSurface`, as `RecordingSurface` is, registered as its own
  type (`MEASURING`), whose runs step through the measurements: open the
  project, settle, then the samples, a run at a time.
- What it needs from where it runs is a `MeasuringEnvironment`: opening a
  project, the wall clock and the UI thread's time (as `absl::Duration`s), and
  the results. In the test install that is the test install and Game Bits'
  clocks (CL4); in tests, `FakeMeasuringEnvironment`, over the fake REAPER,
  with clocks the test moves, so its tests never wait. They check the
  sequence: which runs make a call, the alternation, medians, and the output.
- **Output:** `output.txt` in the run directory, plain text with one line for
  each measurement: the
  call's time and its later work, each as a median and spread, in
  `FormatRunTime()`'s units. Each line is reported as its measurement is
  done, so a run that stops part way keeps what it measured. It is read to set
  the cost table, and isn't kept. A filter (`JPR_MEASURE_FILTER`,
  `FilterCostMeasurements()`) runs only the measurements it names.

### common/testing: Running in the test install

- The parts of `contract_test_reaper.cc` that aren't the contract tests (the
  run directory and its files, the log, opening a `FakeProject`, quitting, and
  loading the functions that aren't on the API list) move into a library both
  DLLs use (`test_install.h`), and `check_in_reaper.cmake` becomes
  `run_in_test_install.cmake`, which runs either DLL with its own surface
  type.
- **`measure_in_reaper`** builds `reaper_jpr_measure`, runs it in the test
  install as it opens, and prints its `output.txt`, which it leaves in the run
  directory. Like `check_in_reaper`, it runs REAPER, so it runs in the
  main checkout only, never touches the user's REAPER, and, like the
  benchmark, needs a quiet machine. It allows 40 minutes
  (`run_in_test_install.cmake`'s `TIMEOUT_SECS`, which
  `JPR_REAPER_CHECK_TIMEOUT` can only raise).
- **Run time:** a full measurement should take about 15 minutes, for about
  110 measurements. Reads take one run a sample. A change takes 12 samples of
  13 runs, and as many with no call, after 15 runs to settle, about 11
  seconds. 12 samples rather than the 20 To confirm found idle noise needs
  put later work within about ±0.4ms rather than ±0.3ms, which still separates
  the categories. If more precision is needed, the windows with no call can
  be shared between measurements, as they barely drift.
- **The machine is left alone** while it runs: REAPER takes the foreground
  as it starts, which keeps it on performance cores (see To confirm).

### common/testing: The cost table

`call_cost.cc` is refined from a measurement run, at the large project's size,
as now: each function's category, `deferrable` for the setters whose later
work moves to the end of a deferred refresh, and a comment on each cost that
scales. `CallCost` keeps coarse categories, not times, as the bounds compare
counts; its comments give each category's rough range, as they do today. A
category that changes moves the call count bounds in the same CL.

The table is the only record. No snapshot of the measurements is kept, as
nothing compares against one: JPRSurf isn't looking for regressions in
REAPER, so a question about a cost is answered by measuring again. That is a
step for a function added to the API list (see the call count tests' building
blocks) and for a REAPER update.

### To confirm

Checked in the test install (2026-10-16), with temporary code in the check
DLL's runs after the tests (the contract tests finish within one run, so they
can't see later work), on the large project, with 20 samples of 13 runs each,
alternating with 13 runs with no call. Times are medians.
- **Later work shows in UI thread cycles.** The surface's runs are on the main
  window's thread. Selecting a track takes 10us in the call, then 2.5ms of
  later work in the run after it, and a deferred refresh doesn't change that.
  The undo point a volume change from a surface holds adds 1.4ms of later work
  about 8 runs (250ms) after the change, inside a deferred refresh or not.
  Otherwise, later work all lands before the next run.
- **Noise:** idle, REAPER's UI thread works about 0.7ms in two runs of every
  three, and 0.1ms in the third (its own redraw timer, beside the surface's
  31.5ms runs). A 13-run window is 6.8ms, varying by about ±0.6ms (p10 to
  p90), so 20 samples put later work within a few hundred microseconds, which
  is plenty for coarse categories.
- **Cores:** on an otherwise idle machine, REAPER takes the foreground as it
  starts, and 99% of runs were on performance cores, so nothing needs to
  steer it. With other programs running, 12% weren't.
- **Wall time and cycles agree** for every call except those that write a file
  or insert an item (Save project, 6.1ms wall against 3.0ms of cycles; Insert
  click source, 20.5ms against 13.1ms), which wait. So the call is timed by
  wall time.
- **Actions:** every action JPRSurf runs ran without a dialog, but Transport:
  Record, which wasn't tried. The saves wrote into the run directory, where
  the project was opened from.

What the setters cost, to start CL5 from (call, then the end of the deferred
refresh where there is one, then later work):

| Call                                  | Outside a deferred refresh | Inside one           |
| ------------------------------------- | -------------------------- | -------------------- |
| `SetTrackUIMute`                      | 0.76ms, none               | 0.09ms, 0.69ms, none |
| `SetTrackUIRecArm`                    | 4.6ms, none                | 0.28ms, 4.9ms, none  |
| `SetTrackSelected`                    | 10us, 2.5ms                | 1us, 11us, 2.5ms     |
| `CSurf_OnVolumeChangeEx`              | 12us, 1.4ms (undo point)   | 7us, 13us, 1.4ms     |
| `SetTrackSendUIVol` (an instant edit) | 1.5ms, none                | 0.29ms, 1.25ms, none |
| `Undo_OnStateChangeEx`                | 1.3ms, 1.0ms               |                      |
| An empty deferred refresh             |                            | 0.1us                |

So a deferred refresh moves a mute's, rec arm's, and send volume's refresh to
its end, the route setters included, while a selection's cost is later work
either way. The actions ranged from 11us (Item: Unselect all items) to about
1ms to move the cursor, 2.5ms to play, 7.5ms for Undo and Redo (with 2.5-3ms
of later work), and 12-20ms to insert an item. Reads of one track over and
over were 7ns (`GetTrackUIVolPan`) and 28ns (`GetTrackState`), against 70ns
and 166ns in the smoke profile, so reads are timed across tracks.

### Depends on

Game Bits *Thread cycles*: the cycles the calling thread has run for,
`QueryThreadCycleTime()` on Windows. It is generic, so it belongs in Game
Bits, and is done before this feature starts.

## CLs

### CL1 [x] common/testing: Confirm how REAPER's later work can be measured

Depends on: Game Bits *Thread cycles*.

- Temporary code in `reaper_jpr_check`, run after the tests, for each item of
  To confirm, written to its output. It is removed again.
- Commits only the findings, in To confirm, and any changes they make to the
  later CLs.

**Verify**
- `check_in_reaper` passes, with the temporary code removed.

### CL2 [x] common/testing: Share the test install between DLLs

Depends on: CL1.

- The test install's host code moves out of `contract_test_reaper.cc` into
  `test_install.h/.cc` (`jpr_common_test_install`), with `FinishRun()` writing
  the result, opening an empty project, and quitting in one call.
- `check_in_reaper.cmake` becomes `run_in_test_install.cmake`, which runs a
  given DLL and surface type, and removes every DLL it copied once REAPER
  quits. The run directory is the test install's `run` folder
  (`JPR_REAPER_RUN_DIR`).
- `check_in_reaper` and `check_load_in_reaper` behave as before.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `check_in_reaper` and `check_load_in_reaper` pass.

### CL3 [x] common/testing: MeasuringSurface

Depends on: CL2.

- `measuring_surface.h/.cc` and `cost_measurement.h`, with the output and the
  filter.
- Unused outside its tests, so no visible change.

**Verify**
- Standard checks.
- `measuring_surface_test.cc`, under the fake with fake clocks: the runs that
  make a call and the ones that don't, alternating values, the medians and
  spreads, settling after opening a project, the filter, and the output.

### CL4 [x] common/testing: The measurements, and measure_in_reaper

Depends on: CL3.

- `cost_measurements.cc`: every measurement in Design, and the scaled project
  builder beside `AddLargeProject()`.
- `reaper_jpr_measure` and the `measure_in_reaper` target, with a time limit
  parameter for `run_in_test_install.cmake`, as a full run takes longer than
  its 300 second default.

**Verify**
- Standard checks.
- `cost_measurements_test.cc`: every function on the API list is measured or
  listed with why, and each measurement runs a sample under the fake.
- `measure_in_reaper` runs to the end, within the time target.

### CL5 [x] common/testing: Measured costs in the cost table

Depends on: CL4.

- `call_cost.cc` refined from a full run on an idle machine, and `CallCost`'s
  comments to match. The run's `output.txt` goes to the user for review with
  the CL, and isn't committed.
- Any call count bounds a changed category moves, in
  `plugin/default_config/call_count_test.cc`.

**Verify**
- Standard checks.
- The bounds that move, each with why.

### CL6 [x] docs: Remove the smoke test

Depends on: CL5.

- `profiles/smoke.txt` and the smoke scenario in `profiles/README.md` go.
- CLAUDE.md:
  - Commands gains "Measure REAPER's costs": when to run it (a function added
    to the API list, or a REAPER update) and what to do with the result.
  - Checking in REAPER: the end of a feature drops the smoke test. It is
    `check_load_in_reaper`, no new errors in the log once REAPER creates the
    surface, and the feature's own checks and feel on the hardware.
  - Performance: "In REAPER" is the idle profile alone, and a gesture's cost
    in REAPER is its call count bounds, weighed by the cost table.
- `profiles/README.md`, `testing_and_profiling.md`, and the benchmark's
  description (whose actions were the smoke scenario's) to match, and the
  worklog template's Checks in REAPER.

**Verify**
- The docs read correctly.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow).

## Checks in REAPER

None in the user's REAPER: the plugin doesn't change, and every measurement
runs in the test install. The end of the feature is a full
`measure_in_reaper` run on an idle machine, whose results the user reviews
with CL5.
