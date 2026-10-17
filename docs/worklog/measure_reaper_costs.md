# Measure REAPER's costs

What each function on the REAPER API list, and each action JPRSurf runs, costs
in REAPER on its own, measured in the test install with the arguments JPRSurf
calls it with: the time in the call, and REAPER's work on its UI thread after
it. The cost table the call count tests weigh calls by (`GetCallCost()` in
`jpr/common/testing/call_cost.cc`) is set from the measurements, and is their
only record.

With it, performance is decided without measuring anything but JPRSurf's own
code: a gesture's cost in REAPER is its calls, bounded by the call count tests
and weighed by the table, and JPRSurf's own time comes from the benchmarks
under the fake. So the smoke test, its scenario, and its profile are gone.
What isolated measurements miss, REAPER's effect on JPRSurf's own code, the
idle profile and the log's "Slow run" warnings show.

The figures only need to be rough. JPRSurf isn't responsible for how long
REAPER takes, and has no control over REAPER's own UI or how a user has
configured it; what matters is putting each call in the right coarse category.
So the measurements run in the test install as it opens, the same each time.

## Behavior

The plugin is unchanged. The process changed:
- **The end of a feature** has no smoke test: the load check, no new errors
  (or "Slow run" warnings) in the log, the feature's own checks and feel on
  the hardware, and an idle profile for a feature that changed per-run work.
- **The cost table** follows the measurements. Text and GUIDs are cheap, a
  track rename is moderate, and a route's setters are deferrable refreshes
  (see What REAPER costs). 27 call count bounds moved between cheap and
  moderate, each with the same total.
- **CLAUDE.md** was reorganized around where each check runs (the fake, the
  test install, or the user's REAPER), with one table of every check, when it
  is needed, and what it needs, and a rule for asking the user for a quiet
  machine. The benchmark is a judgment call, once at the end of a feature
  whose risk calls for it. The benchmark and the cost measurement are project
  skills (`.claude/skills/`).

## Names

| Name                     | What                                                                                         | Might be confused with                                    |
| ------------------------ | -------------------------------------------------------------------------------------------- | --------------------------------------------------------- |
| Later work               | REAPER's time on its UI thread in the runs after a call, beyond what the same runs cost idle | A deferred refresh, which puts work off to its end        |
| `CostMeasurement`        | One thing measured under one condition: a read or a change, on the project it builds         | `CallCost`, the category the table gives a function       |
| `MeasuringSurface`       | The control surface whose runs step through the measurements                                 | `RecordingSurface`, which the contract tests run from     |
| `MeasuringEnvironment`   | What the surface needs from where it runs: projects, clocks, and the results                 | `FakeEnvironment`, the contract tests' fake REAPER        |
| `test_install.h`         | What any DLL that runs in the test install needs                                             | `run_in_test_install.cmake`, which runs such a DLL        |
| `reaper_jpr_measure`     | The DLL that measures in the test install                                                    | `reaper_jpr_check`, which runs the contract tests         |
| `measure_in_reaper`      | The build target that runs it                                                                | `check_in_reaper`, and the `benchmark` target             |

## Structure

### common/testing: The test install (test_install.h, run_in_test_install.cmake)

- **`jpr_common_test_install`** holds what a DLL running in the test install
  needs: `LoadTestInstall()` from its entry point (the log, REAPER's API, the
  functions off the API list, and registering its surface type), the run
  directory (`GetRunDir()`, `WriteRunFile()`), `OpenProjectFile()`, which
  opens a `FakeProject` from a project file, and `FinishRun()`, which writes
  `result.txt`, opens an empty project, and quits once the run returns.
- **`run_in_test_install.cmake`** runs any such DLL (`RUN_DLL`) with its
  surface type (`SURFACE_TYPE`) as the only control surface, and removes every
  DLL it copied once REAPER quits, so one run's DLL never loads in another.
  `TIMEOUT_SECS` sets the time limit (300 seconds by default), which
  `JPR_REAPER_CHECK_TIMEOUT` can only raise. The run directory is the test
  install's `run` folder (`JPR_REAPER_RUN_DIR`).
- `check_in_reaper` and `check_load_in_reaper` run through it, as before.

### common/testing: Measurements (cost_measurement.h, cost_measurements.h)

```
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

std::vector<CostMeasurement> GetCostMeasurements();
absl::Span<const UnmeasuredFunction> GetUnmeasuredFunctions();
```

`GetCostMeasurements()` (`cost_measurements.cc`) has about 110, each named for
the function it calls:
- **Reads**, timed per call across tracks, routes, or 100 repeats (one track
  read over and over stays in the cache, and reads several times faster than
  in use): on 28, 128, and 508 tracks where they may scale (`AddLargeProject()`
  with 2, 12, and 50 folders), and with one or every track selected.
- **Changes**, inside a deferred refresh, as the extension makes them, with
  the call and its end timed apart: each setter, a rename, the override, a
  track's send and a bus's receive (as an edit in progress), every track in
  one deferred refresh, a mute and fader ganged across the selection, an
  empty deferred refresh, and undo points, with the mute, rec arm, selection,
  and undo point also on 28 and 508 tracks. A mute and a send's volume are
  also measured outside one, to compare. A setter alternates its value, so
  each call changes something.
- **Actions**: each one JPRSurf runs, with what it needs first (`prepare`) and
  what resets it after each sample (`reset`, such as Stop after Play, or
  making the change an undo point holds), and one ruler mode, restored after
  each, as REAPER keeps it as a preference.

`GetUnmeasuredFunctions()` lists those that can't be: the MIDI port functions
(the machine's ports, only used as the surface starts) and `ShowConsoleMsg()`
(it opens a window). Transport: Record isn't measured either, as it records
media and stopping it may ask what to do with the files.
`FilterCostMeasurements()` keeps those `JPR_MEASURE_FILTER` names.

### common/testing: MeasuringSurface (measuring_surface.h)

- A control surface (type `MEASURING`) whose runs step through the
  measurements, a run at a time, from a list of steps built up front: open
  the project and prepare it, settle (15 runs), then each sample. A read is
  one run a sample. A change's sample is a window of 13 runs from the call
  (about 0.4s, which covers the undo point REAPER adds for a fader about
  250ms after it moves), a window for its reset if it has one, and a window
  with no call.
- **The call** is wall time (a call that waits, on a file or on REAPER's audio
  thread, holds up the UI thread as much as one that works). **Later work** is
  the UI thread's time in the call's window (`gb::GetThisThreadCycles()`,
  read at the start of each run), less the call, against the median window
  with no call. Thread time counts only work, not the wait between runs, so
  later work shows though the runs come at the same rate.
- Each measurement's line is reported as it is done, so a run that stops part
  way keeps what it measured: the call, the end of its deferred refresh, and
  later work, each as a median with its 10th and 90th percentiles, in
  `FormatRunTime()`'s units (which now has nanoseconds under 1us).
- **`MeasuringEnvironment`** is what it needs from where it runs: opening a
  project, the wall clock and the UI thread's time, and the results. In the
  test install, `measuring_surface_reaper.cc` implements it on Game Bits'
  clocks, and writes `output.txt`. In tests, `FakeMeasuringEnvironment`
  (`jpr_common_testing`) opens projects in the fake REAPER, with clocks the
  test moves.
- It is in `jpr_common_measure`, without the fake, so the DLL doesn't link it.

### common/testing: measure_in_reaper

`reaper_jpr_measure` measures every cost (12 samples each), or those
`JPR_MEASURE_FILTER` names, and fails at once if the machine can't count a
thread's cycles, or nothing matched the filter. `measure_in_reaper` runs it
(about 14 minutes for everything, with a 40 minute limit) and prints its
results. It needs a quiet machine, and runs from the main session only.

### common/testing: The cost table (call_cost.h)

The categories come from a full run, at 128 tracks, with each category's
measured range in its comment, and a comment beside each cost that grows with
the project. `kReaperApiNames` in `reaper_api.h` lists the API's names, for the
table's `static_assert` and the coverage test.

### Along the way

- `AddLargeProject()` scales by its folders (10 tracks a folder, and 8 more),
  with `kLargeProjectBusCount` and `GetLargeProjectBusReceiveCount()`.
- The action IDs JPRSurf runs are named in `action_ids.h`, and the action list
  uses them.

## What REAPER costs

Measured in the test install (2026-10-16), at 128 tracks unless it says, as
medians. A change's figures are the call, the end of its deferred refresh, and
later work; later work under about 0.5ms is within the noise.

- **Reads:** 3-100ns: most track, route, and project reads. Text: 68ns
  (`mkpanstr`) to 261ns (`format_timestr_pos`), and `stringToGuid()` 1.4us.
  `AnyTrackSolo()` and `CountSelectedMediaItems()` grow with the project: 86ns
  and 98ns at 128 tracks, 315ns and 399ns at 508. `GetTrackState()` and
  `GetTrack()` don't.
- **Track setters:** a mute or solo 91us, 0.8ms (flat from 28 to 508
  tracks); a rec arm 0.29ms, 4.9ms (4.5ms on 28, 6.6ms on 508); a selection
  1.6us, 6us, then 2.7ms of later work; selecting only a track 5us, 0.66ms,
  4.3ms; a fader or pan 2.7us, 10us, then about 1.6ms of later work, most of
  it the undo point REAPER holds and adds after; the override 0.1ms, 0.4ms,
  3.1ms; a rename 4us, and nothing after.
- **Routes:** a volume or pan 2us, 0.19ms, so a deferred refresh puts their
  refresh off too, though their calls back come during the call; a mute
  toggle 0.36ms, 2.1ms, with the undo point REAPER adds for it.
- **Many at once:** muting every track (128) in one deferred refresh costs
  3.9ms in its calls and 1.7ms at its end, against 0.8ms for one; arming
  every track 13.6ms and 6.2ms; selecting every track 98us, then 5.7ms. A mute
  ganged across every selected track ends in 1.9ms, and a ganged fader in
  0.35ms, then 3.1ms.
- **Outside a deferred refresh:** a mute's refresh is in its call (0.9ms), and
  a send's volume 0.2ms.
- **Undo points:** 0.28ms, then 1.1ms at the end of the deferred refresh
  they're added in: 1.2ms in all on 28 tracks, and 2.4ms on 508. An empty
  deferred refresh costs 0.4us.
- **Actions:** from 5us (unselecting items) and 32us (repeat), through about
  1ms (moving the cursor, Stop, Play, which then has 1.6ms of later work) and
  1.8ms (the automation modes, then 3.3ms), to 10ms (Undo and Redo, then
  2.5ms), 11-14ms (inserting an item), and 21ms (saving a new version).

**The environment:**
- REAPER's UI thread works about 0.7ms in two runs of every three when idle
  (its own redraw timer, beside the surface's 31.5ms runs), so a 13-run window
  varies by about ±0.6ms, and 12 samples put later work within about ±0.4ms.
- On an otherwise idle machine, REAPER takes the foreground as it starts, and
  99% of runs are on performance cores. With other programs busy, 12% weren't.
- Every action runs without a dialog in the test install, the saves included,
  which write into the run directory the project was opened from.

## Tests

| File                         | What                                                                                                                                   |
| ---------------------------- | -------------------------------------------------------------------------------------------------------------------------------------- |
| `measuring_surface_test.cc`  | The runs that make a call, alternating windows, waits not in later work, percentiles, deferred refresh ends, reads, resets, preparing, reporting as it goes, and nothing after finishing |
| `cost_measurement_test.cc`   | The filter                                                                                                                             |
| `cost_measurements_test.cc`  | Every function on the API list is measured or says why not, and every measurement runs a sample under the fake                         |
| `large_project_test.cc`      | The large project scales by its folders                                                                                                |
| `reaper_profiler_test.cc`    | `FormatRunTime()` in nanoseconds, and negative times                                                                                   |
| `call_cost_test.cc`          | Route setters are deferrable, text is cheap, and a rename moderate                                                                     |

## Building blocks

- **A function added to the API list** gets a measurement in
  `cost_measurements.cc` (or a reason in `kUnmeasured`), and its cost is set
  from a run filtered to it: the `measure-reaper-costs` skill has the steps.
- **A DLL that runs in the test install** is built on `test_install.h` and run
  by `run_in_test_install.cmake` with its surface type.
- **Measuring something new** is a `CostMeasurement`: a `read` returning its
  call count, or a `call` that changes something each time, with `prepare`
  and `reset` for what it needs around it.
- **Exploring REAPER across runs**, which contract tests can't (they finish
  within one run): temporary code in a DLL's runs, as CL1 did, timing with
  `gb::ReadCpuTicks()` and `gb::GetThisThreadCycles()`.

## Decisions

- **Rough and coarse, in a fixed environment**: the test install as it opens,
  not any user's layout, as JPRSurf only needs each call's category.
- **No snapshot of the measurements**: nothing compares against one, so the
  table is the record, and a question about a cost is answered by measuring
  again. A function already measured that moves category points at the
  environment first.
- **Measured inside a deferred refresh**, as the extension only changes REAPER
  inside a run; outside one only to compare.
- **12 samples**, not the 20 idle noise suggested, to keep a full run near 15
  minutes. Sharing the windows with no call between measurements would buy
  precision back if it is ever needed.
- **`CountSelectedMediaItems()` stays moderate**, though it measured cheap: it
  scans items too, and the measured project had one.
- **The smoke test is gone entirely**, rather than kept by hand: the surface
  tests cover existing behavior, and each feature's own checks cover what it
  touches.
- **Thread cycles came from Game Bits** (*Thread cycles*), as generic code.

## Performance

Nothing on the realtime path changed. The measurements run only in the test
install, and the cost table only weighs counts in tests.
