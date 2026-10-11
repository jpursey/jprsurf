# REAPER call count tests

Performance checks that are deterministic. JPRSurf controls two things about
its cost: which calls it makes to REAPER and how many, and its own code. These
tests check the first, in `ctest`, for JPRSurf's own surface, so a change that
makes REAPER work harder fails a test rather than waiting to show up in a
profile. The design they come from is in
[testing_and_profiling.md](../testing_and_profiling.md) (Tests).

There is no change in behavior, unless the rules below catch a gesture that
breaks them, which is then fixed first (see CL4).

## Design

Three kinds of check, from the most general to the most specific:
- **Rules for every run,** in every surface test: a run pays one UI refresh at
  most, adds one undo point at most, and never makes the same setter call
  twice on the same track or route. A gesture's work is all done in one run
  (REAPER's callbacks only set flags), so checking each run covers every
  gesture, however a test drives it. A new behavior that misses a batch, or
  repeats a call, fails without a test of its own.
- **Steady state:** in each mode, an idle second of runs makes no refresh, no
  undo point, and no action, and its other calls are bounded.
- **Bounds** on the calls of each gesture the surface tests make, and of the
  infrequent events: a track list refresh, a bank change, and a mode change.

The bounds are tests of the default config, on a generated project large
enough that a call made once per track stands out (128 tracks in folders, with
sends and receives).

### Names

| Name              | What                                                         | Might be confused with                                   |
| ----------------- | ------------------------------------------------------------ | -------------------------------------------------------- |
| `CallCost`        | What a call to a REAPER function costs, as a coarse category | Profile points' self time, which is measured             |
| `GetCallCost()`   | The cost table: each function on the API list's `CallCost`   | The profile snapshots, which the table is read from      |
| `CallCounter`     | Counts JPRSurf's REAPER calls by cost, and checks each run   | Game Bits' profile counters (`gb::ProfileCount`)         |
| `CallCounts`      | The calls counted, by category                               | `gb::Profiler::GetCount()`, a single point's count       |
| `AddLargeProject` | Builds the generated project in a `FakeProject`              | `FakeProject::AddTracks()`, which it uses                |
| `CallCountTest`   | The default config's fixture for bounds, on the large project | `DefaultConfigTest`, which it derives from              |

Alternatives considered: `ReaperCost` for `CallCost` (longer, and every call
here is to REAPER), and `CallTally` for `CallCounter` (avoids the profiler's
"counter", but is a less plain word).

### Counting with a hook, not the profiler

The profiler under the fake would give each function's count, but not what a
rule or a category needs: whether a setter was called inside a
`PreventUIRefresh()` scope (which puts its refresh off to the end of the
scope), whether a `PreventUIRefresh(-1)` ended the outermost scope, and which
track a setter was called on. A hook over the API sees each call's arguments,
and the fake's state as it is called, so it can. The profiler under the fake
stays for *Benchmarks under the fake*, which times JPRSurf's own code.

### common/testing: The cost table (call_cost.h)

```
// What a call to a REAPER function costs, in coarse categories, for bounding
// the calls the extension makes.
struct CallCost {
  enum class Kind {
    kCheap,      // A read, under about 0.4us.
    kModerate,   // Text, and other work up to about 1ms that refreshes nothing.
    kRefresh,    // A change REAPER's UI shows, which refreshes it: 2-17ms.
    kUndoPoint,  // An undo point the extension adds: about 1.5ms.
    kCommand,    // Main_OnCommand(), whose cost is the action's.
  };
  Kind kind;

  // For a refresh: true if a PreventUIRefresh() scope puts the refresh off to
  // the end of the scope, and the call itself is then moderate.
  bool batched = false;

  // How many leading arguments name what a refresh or command acts on, such
  // as a track, or a track and a route's index, so a second call on the same
  // thing in a run is found.
  int target_args = 0;
};

// Returns the cost of a call to `function`, which must be on the API list.
CallCost GetCallCost(std::string_view function);
```

- **Where the categories come from:** the per-call self times in
  `profiles/idle.txt` and `profiles/smoke.txt`, an 81-track project, until
  *Measure REAPER's costs* measures each function on its own. The times fall
  into clear groups: reads up to about 290ns (`GetTrackState`, route reads,
  selection counts), then text and item counts at 460-590ns (`mkvolstr`,
  `guidToString`, `CountSelectedMediaItems`) and `format_timestr_pos` at 2-3us.
  So the line between cheap and moderate is 0.4us. Route reads turn out cheap,
  not moderate as the backlog guessed.
- **Refreshes are every setter, conservatively.** A setter's own time doesn't
  show the redraw REAPER puts off to its own loop, so every function that
  changes what REAPER's UI shows is a refresh: the track setters,
  `CSurf_OnVolumeChangeEx()` and `CSurf_OnPanChangeEx()`, the route setters,
  and `SetGlobalAutomationOverride()`. Only the track setters a batch is known
  to defer (mute, solo, rec arm, and selection, whose callbacks come at the
  end of the batch) are `batched`. *Batch each run* and *Measure REAPER's
  costs* can relax this with what they find.
- **`PreventUIRefresh()`** is cheap, except a `-1` that ends the outermost
  scope, which is a refresh, whether or not anything changed in it, until
  *Batch each run* finds what an empty scope costs. It is the one function
  whose cost depends on its argument, so `CallCounter` handles it by name.
- **A call whose cost grows with the project** is weighed at the size of the
  large project (below). The table is refreshed occasionally, from a new
  snapshot, rather than with each change.
- **Functions only called as the surface starts,** such as the MIDI port
  functions and `NamedCommandLookup()`, are moderate. Starting up isn't
  bounded.
- A test expands `JPR_REAPER_API` to check that every function on the list has
  a cost, and the table has nothing else, so adding a function to the list
  fails until its cost is given, as `reaper_api_test` does for loading.

### common/testing: Counting calls (call_counter.h)

```
// The calls counted, by category.
struct CallCounts {
  int cheap = 0;
  int moderate = 0;
  int refreshes = 0;
  int undo_points = 0;
  std::vector<int> commands;  // The actions Main_OnCommand() ran, in order.

  bool operator==(const CallCounts&) const = default;
};

//==============================================================================
// CallCounter
//
// Counts the extension's calls to the REAPER API by cost (see GetCallCost()),
// for as long as it exists, and fails the test, naming the run and its calls,
// for a run that:
// - Refreshes REAPER's UI more than once.
// - Adds more than one undo point.
// - Makes a refresh or a command twice on the same target, such as a track's
//   mute, or a route's volume.
//
// It hooks every function on the API list over whatever is loaded, so it is
// created after anything that makes REAPER's calls back to the surface (a
// SurfaceNotifier), which then counts the calls the surface makes from inside
// them too.
//==============================================================================
class CallCounter final {
 public:
  // `reaper` must outlive this. Only one may exist at a time.
  explicit CallCounter(FakeReaper* reaper);

  // Returns the calls counted since the last call, or since this was created,
  // and starts again. A test compares them with EXPECT_EQ, and a failure
  // prints each function's count, so what changed is easy to find.
  CallCounts TakeCounts();
};
```

- **Runs** are the fake's (`FakeReaper::GetRuns()`): each call is counted to
  the run it was made in, and a run is checked when a call is made in a later
  one, or when the counts are taken, or the counter is destroyed.
- **Batches** come from the fake: a batched setter inside a scope
  (`FakeReaper::IsUIRefreshPrevented()`) is moderate, and a
  `PreventUIRefresh(-1)` after which no scope is open is the refresh.
- The hooks reach the counter through a file-local global, as the trace's do,
  since `ReaperApiHooks` constructs each hook from its function's name alone.
- **Undo points** are the extension's own (`Undo_OnStateChangeEx()`). REAPER
  adds its own for a track fader or pan move, and route mutes, which aren't
  the extension's calls.

**Brittleness:** a function added to the API list needs a cost, which the
table's test enforces. Whether it is a setter, and what its target arguments
are, is the table's to get right; a refresh given no target arguments counts
any second call in a run as a repeat, which errs toward failing.

### common/testing: The large project (large_project.h)

```
// Adds a large project's tracks and routes to `project`: enough that a call
// made once per track stands out, with folders, and tracks with more routes
// than a mode's strips show.
void AddLargeProject(FakeProject* project);
```

128 tracks: 4 buses at the top, then 12 folders of 10 tracks, the second of
which is a folder of 3. Every other track sends to a bus, in turn, so each bus
has about 30 receives, and the first track of each folder also sends to the
rest of its folder (9 sends). It is in `common/testing`, rather than the
default config's tests, so *Benchmarks under the fake* and *Measure REAPER's
costs* (in the test install, from a `FakeProject`) can use the same project.

### plugin/testing: Rules in every surface test

`SurfaceTest` holds a `CallCounter`, declared after the notifier, so every
surface test checks the rules for every run, including those the existing
tests already make. Nothing else in the fixture changes.

### plugin/default_config: Bounds (call_count_test.cc)

`CallCountTest` derives from `DefaultConfigTest` (the X-Touch and extender),
builds the large project, adds the surface, and takes the counts of starting
up, so each test counts only what it does (the numbers here are made up):

```
TEST_F(CallCountTest, TappingMute) {
  Tap(xtouch_, StripButton::kMute, 3);
  EXPECT_EQ(calls_.TakeCounts(), (CallCounts{.cheap = 1405,
                                             .moderate = 4,
                                             .refreshes = 1,
                                             .undo_points = 1}));
}
```

- **Exact counts.** Each bound is the count when the test is written, compared
  with `EXPECT_EQ`, so a regression fails, and so does an improvement, until
  the bound is lowered in the same change.
- **A gesture's counts include its settling runs,** as the surface test
  helpers run them (`Tap()` waits out the double press window). Those runs are
  idle, so they add only cheap reads. Exact counts still catch any change in
  the gesture's own calls, and the steady state tests show whether a change is
  the gesture's or every run's. The cost is that a change to what every run
  polls changes every gesture's cheap count.
- **Steady state:** a second of runs (30, which includes one poll of the track
  filters), in Track mode at the top level and in a folder, while playing (the
  timecode is formatted each run), and in Send/Receive mode on a bus with 30
  receives.
- **Gestures:** each one the surface tests make, by area as the surface tests
  are: track strips (mute, solo, rec arm, a ranged mute, a fader, a pot and
  its press, select, a selected range, and a double press into a folder), the
  track list (bank, channel, Global and holding it, and a track clicked in
  REAPER), Send/Receive mode (entering it and the Send hold pick, a route's
  fader, pot, mute, and select, paging, and returning with Track), and the
  global buttons (automation modes, transport, undo and redo, and the
  timecode mode).
- **Infrequent events:** a track list refresh (`SetTrackListChange()`), a bank
  change, and each mode change, which are among the gestures above or a call
  REAPER makes.

### To confirm

Nothing. The rules and categories rely only on what the fake already models
and what the profile snapshots measured. What they leave conservative (which
setters a batch defers, and what an empty batch costs) is for *Batch each run*
and *Measure REAPER's costs* to check.

## CLs

### CL1 [ ] common/testing: The cost table

Depends on: nothing.

- `call_cost.h/.cc`: `CallCost` and `GetCallCost()`, with every function on
  the API list, categorized as in Design.
- Unused, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `call_cost_test.cc`: every function on the API list has a cost, and the
  table has no other names.

### CL2 [ ] common/testing: CallCounter

Depends on: CL1.

- `call_counter.h/.cc`: `CallCounts` and `CallCounter`, as in Design.
- Unused outside its tests, so no visible change.

**Verify**
- Standard checks.
- `call_counter_test.cc`, calling the API directly on the fake, a run at a
  time: each category counted, a batched setter moderate inside a scope and a
  refresh outside one, only the outermost `PreventUIRefresh(-1)` a refresh,
  the actions run, `TakeCounts()` starting again, and each rule failing (with
  `EXPECT_NONFATAL_FAILURE`) for two refreshes, two undo points, and a repeated
  setter on the same track, but not on two tracks, or in two runs.

### CL3 [ ] common/testing: The large project

Depends on: nothing.

- `large_project.h/.cc`: `AddLargeProject()`.

**Verify**
- Standard checks.
- `large_project_test.cc`: the track count, the folders' depths, and the
  routes of a bus and of a folder's first track.

### CL4 [ ] plugin/testing: Rules in every surface test

Depends on: CL2.

- `SurfaceTest` holds a `CallCounter` (`calls_`).
- If a surface test breaks a rule, the gesture is fixed first, in a CL of its
  own in the library at fault, inserted before this one, with the surface test
  that shows it. Which gestures break a rule isn't known until this runs.

**Verify**
- Standard checks: every surface test passes the rules.
- `surface_test_test.cc`: a surface test that repeats a setter in a run fails
  (with a test hook over the fake that makes the repeat).

### CL5 [ ] plugin/default_config: Steady state and infrequent events

Depends on: CL3, CL4.

- `call_count_test.cc`: `CallCountTest`, the steady state tests, and the
  bounds of a track list refresh, a bank change, and the mode changes.

**Verify**
- Standard checks.

### CL6 [ ] plugin/default_config: Gesture bounds

Depends on: CL5.

- Bounds of every other gesture the surface tests make, as listed in Design.

**Verify**
- Standard checks.

### CL7 [ ] docs: Call count tests in the workflow

Depends on: CL6.

- `testing_and_profiling.md` (Profiler, Tests): call count tests count with a
  `CallCounter`, not the profiler.
- CLAUDE.md, the smoke test bullet: a feature that adds a gesture adds its
  call count bounds, as it adds its surface tests. Replacing the idle profile
  waits for *Benchmarks under the fake*, as the backlog says.

**Verify**
- The docs read correctly.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow).

## Checks in REAPER

None: the plugin doesn't change, so there is nothing new to load or profile.
If CL4 inserts a fix for a gesture that broke a rule, that fix gets the usual
end of feature checks: the extension loads with no new errors in the log, the
fixed gesture behaves on the hardware, and the smoke test passes.
