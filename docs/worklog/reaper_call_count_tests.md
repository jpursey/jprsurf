# REAPER call count tests

Performance checks that are deterministic. JPRSurf controls two things about
its cost: which calls it makes to REAPER and how many, and its own code. These
tests check the first, in `ctest`, without REAPER, so a change that makes
REAPER work harder fails a test rather than waiting to show up in a profile.
The design they come from is in
[testing_and_profiling.md](../testing_and_profiling.md) (Tests), and
CLAUDE.md's Performance section (Call counts) says how a change keeps them.

There are three kinds of check, from the most general to the most specific:
- **Rules in every surface test:** an entry point (a run, any other call on the
  surface, or a test's own calls between them) refreshes REAPER's UI once at
  most, adds one undo point at most, and never repeats a refresh or an action
  on the same track, route, or action.
- **Steady state:** a second of idle runs in each mode, bounded.
- **Bounds** on the calls of starting up, each gesture the surface tests make,
  and the infrequent events, on a large project, as exact counts.

## Behavior

None: the plugin is unchanged. Every existing surface test passed the rules as
it was, so no gesture needed a fix. What the counts showed:
- **Send/Receive mode reads every route each run.** On the large project's
  bus, all 31 receives' volume, pan, and mute, where the 15 route strips need
  30 reads. The backlog's *Poll only the routes a routes list shows* already
  had this, and now has the counts.
- **A rec arm refreshes the whole track cache,** as REAPER calls
  `SetTrackListChange()` after it: about 2,400 more cheap calls than a mute on
  the large project, and 129 `guidToString()` calls. This is accepted (see
  Decisions).
- **Two gestures act in two runs by design,** with a refresh in each: a ranged
  mute (the held track as it is pressed, then the range, with an undo point
  each, as [ranged_track_actions.md](ranged_track_actions.md) intends) and a
  selected range (the first track, then the rest). So the rules hold for each
  entry point, not each gesture.
- **Nothing idle refreshes, adds an undo point, or runs an action.** An idle
  run's one moderate call is `CountSelectedMediaItems()`, for the Cancel light.

## Names

| Name                               | What                                                             | Might be confused with                           |
| ---------------------------------- | ---------------------------------------------------------------- | ------------------------------------------------ |
| `CallCost`                         | What a call to a REAPER function costs, as a coarse category     | Profile points' self time, which is measured     |
| `GetCallCost()`                    | The cost table: each function on the API list's `CallCost`       | The profile snapshots, which the table came from |
| `CallCounter`                      | Counts REAPER calls by cost, and checks each entry point         | Game Bits' profile counters (`gb::ProfileCount`) |
| `CallCounts`                       | The calls counted, by cost                                       | `gb::Profiler::GetCount()`, one point's count    |
| `FakeReaper::SetEntryPointCheck()` | A test's own check at the end of each of the fake's entry points | `FakeReaper::EndEntryPoint()`, which runs it     |
| `AddLargeProject()`                | Builds the large project in a `FakeProject`                      | `FakeProject::AddTracks()`, which it uses        |
| `CallCountTest`                    | The default config's fixture for bounds, on the large project    | `DefaultConfigTest`, which it derives from       |

## Structure

### common/testing: The cost table (call_cost.h)

`GetCallCost(name)` returns a function's `CallCost`:
- **`kind`:** cheap (a read, under about 0.4us), moderate (text, a scan of the
  project, a function only called as the surface starts, or a deferrable
  setter's own work), a refresh of REAPER's UI (2-17ms), an undo point
  (`Undo_OnStateChangeEx()`), or a command (`Main_OnCommand()`, whose cost is
  the action's).
- **`deferrable`:** a refresh that a deferred refresh (a `PreventUIRefresh()`
  scope) puts off to its end, which is then moderate inside one: mute, solo,
  rec arm, selection, volume, pan, and the automation override.
- **`repeat_key_args`:** how many leading arguments, with the function, say
  what a refresh or command acts on (a track, a track and a route's index, or
  an action), so a second call with the same ones is a repeat.

The categories came from the per-call self times in `profiles/idle.txt` and
`profiles/smoke.txt` (an 81-track project), which fall into clear groups.
They are conservative where nobody has measured: every setter is a refresh,
the route setters included, and only the setters whose calls back a deferred
refresh is known to put off are `deferrable` (see
[deferred_refresh.md](deferred_refresh.md)).
`PreventUIRefresh()` itself is cheap; `CallCounter` finds the deferred
refresh's end from the fake.

The table lists the API in its order, and a `static_assert` checks it against
`JPR_REAPER_API`, so a function added to the list doesn't compile until it has
a cost.

### common/testing: Counting calls (call_counter.h)

`CallCounter` hooks every function on the API list (`ReaperApiHooks`), counts
each call into `CallCounts` as it returns, and checks each entry point as it
ends:
- **`CallCounts`:** `cheap`, `moderate`, `refreshes`, `undo_points`, the
  actions run (`commands`, in order), and each function's count (`calls`).
  `==` compares all but `calls`, and `ToString()` lists `calls` by name, so a
  failed `EXPECT_EQ` shows which functions moved. `TakeCounts()` returns the
  counts and starts again.
- **Deferred refreshes:** a deferrable setter inside a deferred refresh
  (`IsUIRefreshPrevented()` as it is called) is moderate. The call that ends
  the outermost deferred refresh (one was open before it, and none is after)
  is the refresh, whatever it is, if a deferrable setter was called in it, so
  an empty one is no refresh. Any other refresh in a deferred refresh is a
  refresh too, so beside a deferrable one, it is a second.
- **Entry points** are the fake's, through `FakeReaper::SetEntryPointCheck()`,
  which `EndEntryPoint()` calls after the fake's own checks. So a failure
  points at the call on the surface that caused it, and REAPER's calls between
  runs are checked on their own, not with the run before. Calls not yet ended
  when the counter is destroyed are checked then.
- **Failures** are `ADD_FAILURE()`s naming the calls: the refreshes made, the
  undo points added, or the functions repeated.
- The hooks reach the counter through a static pointer, as the trace's do, so
  only one may exist at a time. It hooks over whatever is loaded, so it is
  created after the `SurfaceNotifier`, and also counts the calls the surface
  makes from inside REAPER's calls back.

It overlaps the fake's own TrackBatch check, so a missed batch in a surface
test fails twice. The fake's check stays, as it applies to every library's
tests.

### common/testing: The large project (large_project.h)

`AddLargeProject(project)` adds 128 tracks, named as `AddTracks()` names them:
- T1 to T4, buses, each receiving from every fourth track (31 receives).
- T5 to T16, folders of 9 tracks each, and T6.1 is itself a folder of 4.
- Every track but the buses sends to a bus, in turn, and each folder sends to
  each track directly in it.

It is in `jpr_common_contract`, beside `FakeProject`, so *Benchmarks under the
fake* and *Measure REAPER's costs* (in the test install) can use the same
project.

### plugin/testing: Rules in every surface test

`SurfaceTest` holds a `CallCounter` (`calls_`), declared after the notifier, so
every surface test checks the rules, and any test can take its counts.

### plugin/default_config: Bounds (call_count_test.cc)

`CallCountTest` derives from `DefaultConfigTest` (the X-Touch and extender),
builds the large project, adds the surface, and keeps the counts of starting up
(`startup_`), so each test counts only what it does:

```
TEST_F(CallCountTest, TappingMute) {
  Tap(xtouch_, StripButton::kMute, 0);
  EXPECT_EQ(
      calls_.TakeCounts(),
      (CallCounts{
          .cheap = 852, .moderate = 10, .refreshes = 1, .undo_points = 1}));
}
```

- **Starting up**, and **steady state** (`CountASecond()`): Track mode at the
  top and in a folder, Track mode while playing (the test moves the play
  position before each run, as REAPER does), and Send/Receive mode on a bus.
  A second of runs includes one poll of the track filters, 256 cheap reads.
- **Infrequent events:** a track list refresh, a bank change, entering
  Send/Receive mode, and returning to Track mode.
- **Gestures,** by area as the surface tests are:
  - Track strips: mute, solo, rec arm, a ranged mute, a fader, the master
    fader, a pot, a pot press, select, a selected range, and going into a
    folder.
  - The track list: Channel, Global, holding Global, and a track clicked in
    REAPER.
  - Send/Receive mode, on the bus: the Send hold pick, a route's fader, pot,
    pot press, and mute, selecting a route, paging, and switching between
    sends and receives.
  - Global buttons: the selected tracks' automation mode, the override, the
    timecode mode, and one button for each way a button runs an action: a tap
    (Play), a step (Rewind), a modifier (Redo), a long press (holding Solo),
    and a tap on a button with a long press (Solo in front).

A gesture that changes something starts from a state it changes (a pan to
center for a pot press), and a route move waits for its undo point
(`WaitForUndo()`), so the bound includes it.

`DefaultConfigTest` gained the gestures its tests share: `DoublePressSelect()`,
`PressRange()`, `TapWith()`, `PickWithSendHeld()`, and `WaitForUndo()`, moved
from the files that had their own.

## Tests

| File                    | What                                                                                                                                        |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------- |
| `call_cost_test.cc`     | Finding a function's cost, and the track setters' deferral and repeat keys                                                                  |
| `call_counter_test.cc`  | Each category; deferrable setters in and out of a deferred refresh; the outermost one's end; each rule failing, and passing where it should |
| `fake_reaper_test.cc`   | The entry point check runs at the end of each entry point                                                                                   |
| `large_project_test.cc` | The track count, the folders and their depths, the buses' receives, and the folders' sends                                                  |
| `surface_test_test.cc`  | A surface test that repeats a setter fails                                                                                                  |
| `call_count_test.cc`    | The bounds (above): 40 tests                                                                                                                |

## Building blocks

- **A feature that adds a gesture** adds its bound to `call_count_test.cc`,
  in the area it belongs to, starting from a state the gesture changes. The
  first count is the bound: run the test with `CallCounts{}`, and check the
  failure's counts (and its list by function) make sense before pasting them.
- **A change that moves a bound** updates it in the same CL. A failure lists
  each function's count, so what moved is plain. A change to what every run
  polls moves every bound's cheap count, and the steady state tests show by
  how much a run. A count off by 256 cheap reads is the track filter poll
  moving in or out of a second.
- **A function added to the API list** gets a cost in `call_cost.cc`, in the
  list's order: a refresh if REAPER's UI shows what it changes, `deferrable`
  only if a deferred refresh is known to put it off, and the arguments that
  name what it acts on.
- **Counts in any surface test:** `calls_.TakeCounts()`, for a test that wants
  to show a change made fewer calls.

## Decisions

- **A hook, not the profiler.** The profiler would give each function's
  count, but not whether a deferred refresh was open, or which track a setter
  acted on.
  The profiler under the fake is left for *Benchmarks under the fake*, which
  times JPRSurf's own code.
- **Exact counts,** compared with `EXPECT_EQ`, so an improvement fails too,
  until its bound is lowered with it, and the bounds stay tight.
- **A gesture's counts include its settling runs,** as the surface test helpers
  make them. Those runs are idle, so they add cheap reads only, and the steady
  state tests show what they cost.
- **The fake keeps the play position where a test puts it.** Other tests rely
  on it, and REAPER moves it by its audio clock, which a contract test can't
  pin down, so the one test that needs it moving moves it.
- **A rec arm's full refresh is accepted.** REAPER doesn't say why the track
  list changed, so the surface can't know a rec arm was the only change, and
  checking for changes costs most of what the refresh does. It is well under
  a millisecond, on a gesture that already pays REAPER's UI refresh. A cheaper
  `TrackCache::Refresh()` would help every refresh, if one ever shows up as
  slow.
- **The idle profile stays** in CLAUDE.md's Performance section until
  *Benchmarks under the fake* covers JPRSurf's own time, as the backlog says.

## Performance

Nothing on the realtime path changes, so no profile was recorded. The 40 call
count tests take about 130ms in `ctest`, and the counter adds nothing
measurable to the other surface tests.
