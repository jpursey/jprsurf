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
- **The call:** the time inside the call, with the profiler's clock.
- **Later work:** the cycles REAPER's UI thread ran for (Game Bits' thread
  cycles, see Depends on) from the start of the run that made the
  call, through the runs after it, less the call, against the same span of runs
  with no call. Thread cycles count only work, not the timer's wait between
  runs, so later work shows even though the runs come at the same rate.
  The surface runs on REAPER's UI thread, so it reads that thread's cycles at
  the start of each run.

A measurement alternates samples with and without the call, so drift in what
REAPER does idle cancels out, and reports the median of each, with the spread.
A setter alternates its value (mute on, then off), so each call changes
something, and an action is paired with what undoes it where it needs one
(Play, then Stop). A read changes nothing, so it has no later work: it is
timed over many calls in one run.

**Conditions**, each measured where it can matter:
- **Deferred refresh:** a deferrable setter outside one, and inside one, where
  the call and the end of the outermost deferred refresh after it are timed
  apart. The route setters too, to find whether they are really not
  deferrable.
- **Tracks:** 16, 128 (the large project), and 512 tracks, from a builder that
  scales the large project.
- **Selected tracks:** one, and every track, for selection and for setters,
  which REAPER gangs across the selection.
- **Routes:** a track with one route, and a bus with 31 receives, for route
  reads and setters.

What can't be measured in the test install is listed with why, and keeps its
category: the MIDI port functions (the machine's ports, called only as the
surface starts), `ShowConsoleMsg()` (opens a window), and the actions that
write files or open a dialog (File: Save project, Save new version, Transport:
Record, Insert new MIDI item...). A test checks that every function on the API
list is measured or listed, as the cost table's `static_assert` does.

### common/testing: Measurements (cost_measurement.h)

```
// One thing measured under one condition.
struct CostMeasurement {
  std::string name;       // "SetTrackUIMute", "Undo_OnStateChangeEx", ...
  std::string condition;  // "in a deferred refresh, 128 tracks", ...

  // Fills in the project the measurement starts from.
  absl::AnyInvocable<void(FakeProject&)> build;

  // Makes the call, and what undoes it on the next sample, if anything.
  absl::AnyInvocable<void()> call;
  absl::AnyInvocable<void()> undo;
};

// Every measurement, in the order they run.
std::vector<CostMeasurement> GetCostMeasurements();
```

- They call only REAPER's API, so they run under the fake too: `ctest` runs
  each one for a single sample, so a measurement that no longer works fails
  there rather than in the test install.

### common/testing: MeasuringSurface (measuring_surface.h)

- An `IReaperControlSurface`, as `RecordingSurface` is, registered as its own
  type (`MEASURING`), whose runs step through the measurements: open the
  project, settle, then the samples, a run at a time.
- Its clocks are injected (Game Bits' `FakeTicks` under the fake), so its tests
  never wait, and check the sequence: which runs make a call, the alternation,
  medians, and the output.
- **Output:** `costs.txt`, plain text with one line for each measurement: the
  call's time and its later work, each as a median and spread. It is read to
  set the cost table, and isn't kept. A filter (`JPR_MEASURE_FILTER`) runs
  only the measurements it names.

### common/testing: Running in the test install

- The parts of `contract_test_reaper.cc` that aren't the contract tests (the
  run directory and its files, the log, opening a `FakeProject`, quitting, and
  loading the functions that aren't on the API list) move into a library both
  DLLs use, and `check_in_reaper.cmake` is generalized to run either DLL with
  its own surface type.
- **`measure_in_reaper`** builds `reaper_jpr_measure`, runs it in the test
  install as it opens, and prints `costs.txt`, which it leaves in the run
  directory. Like `check_in_reaper`, it runs REAPER, so it runs in the
  main checkout only, never touches the user's REAPER, and, like the
  benchmark, needs a quiet machine.
- **Run time:** a full measurement should take under about 15 minutes. Reads
  take one run each; a measurement with later work takes a few runs a sample,
  so the sample counts and sizes are set from how noisy idle runs are (see To
  confirm).

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

Checked in the test install, with temporary code in the check DLL's runs after
the tests (the contract tests finish within one run, so they can't see later
work):
- **Later work shows in UI thread cycles:** a mute outside a deferred refresh
  costs clearly more cycles over the next runs than idle runs do, how many runs
  it spans, and that the surface's runs are on the main window's thread.
- **Noise:** the spread of an idle run's UI thread cycles, which sets the
  sample counts.
- **Cores:** whether enough samples run on efficiency cores to blur the
  categories, with the test install in the background. If so, the median may
  be enough, or the DLL raises its own process's priority.
- **Actions:** which of JPRSurf's actions run in the test install without a
  dialog or writing a file.

### Depends on

Game Bits *Thread cycles*: the cycles the calling thread has run for,
`QueryThreadCycleTime()` on Windows. It is generic, so it belongs in Game
Bits, and is done before this feature starts.

## CLs

### CL1 [ ] common/testing: Confirm how REAPER's later work can be measured

Depends on: Game Bits *Thread cycles*.

- Temporary code in `reaper_jpr_check`, run after the tests, for each item of
  To confirm, written to its output. It is removed again.
- Commits only the findings, in To confirm, and any changes they make to the
  later CLs.

**Verify**
- `check_in_reaper` passes, with the temporary code removed.

### CL2 [ ] common/testing: Share the test install between DLLs

Depends on: CL1.

- The test install's host code moves out of `contract_test_reaper.cc` into a
  library, and `check_in_reaper.cmake` runs a given DLL and surface type.
- `check_in_reaper` and `check_load_in_reaper` behave as before.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `check_in_reaper` and `check_load_in_reaper` pass.

### CL3 [ ] common/testing: MeasuringSurface

Depends on: CL2.

- `measuring_surface.h/.cc` and `cost_measurement.h`, with the output and the
  filter.
- Unused outside its tests, so no visible change.

**Verify**
- Standard checks.
- `measuring_surface_test.cc`, under the fake with fake clocks: the runs that
  make a call and the ones that don't, alternating values, the medians and
  spreads, settling after opening a project, the filter, and the output.

### CL4 [ ] common/testing: The measurements, and measure_in_reaper

Depends on: CL3.

- `cost_measurements.cc`: every measurement in Design, and the scaled project
  builder beside `AddLargeProject()`.
- `reaper_jpr_measure` and the `measure_in_reaper` target.

**Verify**
- Standard checks.
- `cost_measurements_test.cc`: every function on the API list is measured or
  listed with why, and each measurement runs a sample under the fake.
- `measure_in_reaper` runs to the end, within the time target.

### CL5 [ ] common/testing: Measured costs in the cost table

Depends on: CL4.

- `call_cost.cc` refined from a full run on an idle machine, and `CallCost`'s
  comments to match. The run's `costs.txt` goes to the user for review with
  the CL, and isn't committed.
- Any call count bounds a changed category moves, in
  `plugin/default_config/call_count_test.cc`.

**Verify**
- Standard checks.
- The bounds that move, each with why.

### CL6 [ ] docs: Remove the smoke test

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
