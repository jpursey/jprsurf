# Check the fakes in REAPER

Surface tests trust two fakes: the fake REAPER's model of REAPER's state, and
`SurfaceNotifier`'s model of what REAPER calls on the surface. This checks both
against REAPER, with contract tests: tests that act only through REAPER's API,
so the same source runs under the fake (in `ctest`) and inside a test install
of REAPER. A test that passes under the fake but fails in REAPER is a gap in the
fake, which is fixed, with that test, as Checking in REAPER in CLAUDE.md says.

So the surface is tested in three separate parts:
- **The surface's behavior**, and later its call counts: surface tests, under
  the fake only.
- **The fake REAPER:** what each function on the API list does to REAPER's
  state, and the effects of the actions `AddReaperActions()` gives handlers.
  What Undo restores settles whether the fake should model undo.
- **`SurfaceNotifier`:** what REAPER calls on a surface during each function.

Every behavior the fake models, and every call the notifier makes, has a
contract test, and the API list is finite, so a review can check it (see
Coverage).

There is no change in behavior.

## Design

### Names

| Name                    | What                                                                       | Might be confused with                                                  |
| ----------------------- | -------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| Contract test           | A test that acts only through REAPER's API, and runs under both            | Surface tests, which run under the fake only                            |
| `ContractTest`          | The gtest fixture contract tests derive from                               | `SurfaceTest`, the fixture for a whole surface                          |
| `RecordingSurface`      | A control surface that records every call it gets, as text                 | `TestControlSurface`, the fake's wrapper of the surface under test      |
| `WriteProjectFile()`    | Writes a `FakeProject` as an RPP file REAPER opens                         | REAPER's own `Main_SaveProject()`                                       |
| Test install            | A portable REAPER only for contract tests, with its own `UserPlugins`      | The user's REAPER, which the plugin deploys to                          |
| `reaper_jprsurf_check`  | The DLL that runs the contract tests inside the test install               | `reaper_jprsurf`, the plugin                                            |
| `check_in_reaper`       | The build target that runs `reaper_jprsurf_check` in the test install      | `ctest`, which runs the same tests under the fake                       |

Alternatives for "contract test": "REAPER test" (but every test is about
REAPER), "API test" (`reaper_api_test.cc` already tests the list itself).

### common/testing: The project a test starts from (project_file.h)

A contract test builds its project with a function that fills in a
`FakeProject`, as `surface_notifier_test.cc` already does. Under the fake, it
builds the fake's current project. In REAPER, it builds a `FakeProject` of its
own, which is written as an RPP file and opened with
`Main_openProject("noprompt:<file>")`.

```
// Writes `project` as the text of an RPP file, which opens in REAPER as the
// same project. Fails the test, naming it, for state an RPP file can't hold,
// such as the play state or a peak.
std::string WriteProjectFile(const FakeProject& project);
```

- **One builder for both,** so the project in REAPER can't drift from the one
  under the fake, and every contract test checks the writer too: a field it
  gets wrong reads back wrong in REAPER.
- **The fake writes it too,** and throws it away, so a test that builds a
  project REAPER can't open fails in `ctest`, not only in REAPER.
- **What it writes:** the tracks in order, with their folders, names, GUIDs,
  colors, volume and pan, mute and solo, rec arm, selection (the master's
  too), automation mode, group, and visibility; each send and hardware output;
  the selected media items, as empty items; the cursor position; and the
  automation override. The play state, play position, peaks, dirty flag, and
  redo are left to what REAPER does (a project opens stopped, clean, and with
  no redo), and setting them fails the test.
- `FakeProject` can be created on its own (`FakeProject::Create()`), rather
  than only by `FakeReaper`, so the REAPER side doesn't need the fake.

Each test opens its own project, so tests start from the same state without
undoing anything, and nothing carries from one test to the next. Opening a
small project should take a few milliseconds (To confirm).

### common/testing: The fixture (contract_test.h)

```
// The fixture for contract tests. Under the fake, each test has its own
// FakeReaper, with a SurfaceNotifier and AddReaperActions(), and a
// RecordingSurface added. In REAPER, the test install's RecordingSurface is
// the one REAPER created, and the tests run from inside its Run().
class ContractTest : public ::testing::Test {
 protected:
  // Opens the project `build` makes, in place of the one open, and forgets
  // the calls opening it made.
  void OpenProject(absl::FunctionRef<void(FakeProject&)> build);

  // Returns the calls the recording surface got since the last time, as text,
  // and forgets them.
  std::vector<std::string> TakeCalls();
};
```

- `contract_test.h` is the same in both. Its implementation is one of two
  files, chosen by what links it: `contract_test_fake.cc` for `ctest`, and
  `contract_test_reaper.cc` for the DLL. A contract test that reaches for the
  fake (`FakeReaper`, `GetProject()`) doesn't link into the DLL, as it doesn't
  link the fake.
- `RecordingSurface` moves out of `surface_notifier_test.cc` into its own file,
  as both the fixture and the DLL need it.
- Under the fake, the test's calls are one entry point (see "Checks" in
  `FakeReaper`), so a contract test changing several tracks batches them, as
  JPRSurf must.
- Tests that need the fake (its checks, `FakeProject`'s own methods, changes
  in REAPER's UI, actions given handlers) stay in `fake_reaper_test.cc` and
  `surface_notifier_test.cc`, as they are.

### common/testing: Running them in REAPER (reaper_jprsurf_check)

`reaper_jprsurf_check` is a DLL with the contract tests, gtest, and an entry
point that loads the API from REAPER's `GetFunc` (`LoadReaperApi()`) and
registers `RecordingSurface`'s type.
- **The test install** is a portable REAPER the user installs, in a folder
  named by the `JPR_REAPER_CHECK_DIR` environment variable, set up as
  `testing_and_profiling.md` says. It has its own `reaper.ini`, which adds a
  `RecordingSurface` and uses REAPER's dummy audio device, so it never
  competes with the user's REAPER for a real one, and its own `UserPlugins`,
  with `reaper_jprsurf_check.dll` and not the plugin. The user's REAPER and
  setup are untouched.
- **Running:** the surface's first `Run()` runs every test
  (`RUN_ALL_TESTS()`), each finishing within the call, as it does under the
  fake. gtest's output, the result, and a log of how far the DLL got go to the
  folder in the `JPRSURF_CHECK_DIR` environment variable, which the runner
  sets. Then it opens an empty project with `Main_openProject("noprompt:...")`,
  which closes the changed one without asking. The next `Run()` posts File:
  Quit REAPER (40004) to REAPER's main window, so REAPER quits after the run
  returns, with nothing left to save. Quitting from inside the run destroys
  the surface while it is still running, which crashes.
- **Main session only:** it runs REAPER, so it runs in the main checkout, one
  run at a time, as other work that needs REAPER does. Claude runs it, as it
  doesn't touch the user's REAPER. Most work doesn't need it: a CL that
  doesn't change the fake or the notifier is verified under the fake alone.
- **`check_in_reaper`** builds the DLL, copies it into the test install, runs
  REAPER there with a time limit (`cmake -P`, no new dependencies), and
  prints the results, failing if any test failed, REAPER didn't quit, or no
  results were written. It is a build target rather than a `ctest` test, as a
  plain `ctest` can't skip a test by label, and it is only run on purpose.
- **What it leaves out:** what REAPER calls between runs, as each test
  finishes within one call; anything that reads the machine (MIDI ports,
  `time_precise()`) or opens a window (`ShowConsoleMsg()`), which stay fake
  only.
- `Main_openProject()`, `GetMainHwnd()`, and `CSurf_FlushUndo()` are the only
  functions the DLL calls that aren't on the API list. It loads them by name
  in `contract_test_reaper.cc`, rather than adding them to the list, as
  JPRSurf never calls them and the fake must never fake them.

It isn't for performance: the tests' calls aren't JPRSurf's, and the DLL has no
profiler.

**Risks**, accepted: contract tests only check what someone thought to check,
where running whole surface tests in REAPER would also find what nobody
expected; the test install's preferences aren't the user's; the fake X-Touch
against the hardware stays a check by hand; what REAPER calls while
creating the surface and loading the project is only seen in traces; and
`WriteProjectFile()` grows with every field `FakeProject` gains, in a format
REAPER doesn't document. A field it can't write fails the test, and every
contract test reads its fields back in REAPER, so a gap shows the first time a
test uses it. If it becomes a burden, the alternative is building the project
with REAPER's own calls, which puts more functions on the API list, each with
a fake and a contract test of its own.

### Coverage

Each function on the API list is checked by a contract test, or is listed here
as fake only, with why. The table is filled in as the CLs land, so the last CL's
review checks it against `JPR_REAPER_API`.

| Function                      | Contract test | Or fake only, because |
| ----------------------------- | ------------- | --------------------- |
| `CreateMIDIInput`, ...        |               | The machine's ports   |
| `time_precise`                |               | The real clock        |
| `ShowConsoleMsg`              |               | Opens a window        |

### To confirm

Checked in REAPER, with the test install, before the CLs that rely on them:
1. **The test install runs unattended:** confirmed (2026-10-08). Registered
   with the user's license and set to the dummy audio device, it starts with
   no dialogs, and with `-newinst` runs as its own instance beside the user's
   REAPER, which is untouched. REAPER only creates a control surface whose
   line in `reaper.ini` has a config string after its type, so the runner
   writes `csurf_0=RECORDING null`, as the user's has `JPRSurf null`. Still to
   check, with the play state's tests: the transport plays and stops on the
   dummy device.
2. **Opening a project from inside `Run()`** with
   `Main_openProject("noprompt:")`: confirmed (2026-10-08). It finishes within
   the call, in about 0.55 seconds, and the notifier's tests read the project
   back as written. GUIDs are checked with the tracks' tests. REAPER holds open
   the undo point of a volume or pan change made with `CSurf_On*ChangeEx()`,
   and if the project it is in is closed first, REAPER crashes after the run.
   So the DLL calls `CSurf_FlushUndo(true)` before it opens each project.
3. **Quitting:** confirmed (2026-10-08). Opening an empty project with
   `noprompt:`, then posting File: Quit REAPER, exits with no prompt. Quitting
   from inside a run crashed REAPER.
4. **Undo:** what Edit: Undo restores after each setter on the API list (mute,
   solo, rec arm, selection, volume, pan, the send setters, and the override),
   which settles whether the fake models undo. These are contract tests that
   fail under the fake until it does, so they are checked in REAPER first.

### Found in REAPER

The notifier's contract tests, run in REAPER first (2026-10-08), with 8 of 13
failing. Each was a gap in the notifier, fixed in CL4:
- **Mute and solo outside a batch:** during the call, REAPER only sends the
  master's solo. The track's own mute and solo come later, not in the call.
- **Rec arm outside a batch:** REAPER sends the track's rec arm, mute, solo,
  volume, and pan first, then the track list change, every track's state, and
  more.
- **Rec arm in a batch:** the calls in the call are as traced, but the end of
  the batch sends the master's state, then for each track its title, rec arm,
  and selection, then its rec arm, mute, solo, volume, and pan, rather than
  every track's state.
- **The automation mode actions with no track selected** only send
  `SetAutoMode()`. The traces had tracks selected, so the resend is probably
  only for the tracks whose mode changes.
- **Undo and Redo** send 45 calls where the notifier sends 47, with some of a
  track's calls missing in the first round.
- **`Extended(CSURF_EXT_SETPAN_EX)` sends pan mode 0** for the test projects'
  tracks, where the traces' tracks had 3. `PANMODE 3` in the project file sets
  the tracks' mode, and `MASTER_PANMODE 3` the master's, which was still 0
  when it seemed `PANMODE` changed nothing.
- **Send volume and pan changes do notify,** where the traces said sends
  notify nothing: `Extended(CSURF_EXT_SETSENDVOLUME)` and `SETSENDPAN` for the
  source track, and `SETRECVVOLUME` and `SETRECVPAN` for the destination. Mute
  sends nothing. JPRSurf polls every route each run on the belief that REAPER
  doesn't report them, which *Poll only the routes a routes list shows* in the
  backlog should now weigh.

CL4 explored each of these in the test install with temporary tests, and found
the rule behind them (see "Seen in traces" in `testing_and_profiling.md`, and
`SurfaceNotifier`):
- **A track's mute and solo wait for the refresh:** the end of the outermost
  batch, or outside a batch, before the next run, when REAPER sends those that
  differ from what it last sent. Until then, every track's state leaves them
  out, which is why Undo sent 45 calls.
- **Rec arm** sends the track's change around every track's state outside a
  batch, and in a batch sends each changed track's change in every track's
  state at its end. A rec arm that changes nothing sends nothing.
- **The automation mode actions** resend only if a selected track's mode
  changed.
- **Selection ganging:** with the setters' group flags without `&2`, as
  JPRSurf passes for a grouped change, a mute, solo, or rec arm of a selected
  track changes every selected track, which the fake doesn't do (see CL6), and
  a rec arm is then sent as a change to every track.
- **Undo** with nothing to undo calls nothing back: after only
  `CSurf_OnVolumeChangeEx()`, or only `SetTrackUIMute()` outside a batch,
  there was nothing to undo (see CL9).

The tracks' contract tests, in CL5 (2026-10-08), found these gaps in the fake,
each fixed with its test:
- **`GetTrackState()`** sets &1 for a folder, and &512 and &1024 for a track
  hidden in the track panel or the mixer, which the fake left out. It also
  sets &128 (input monitoring on), which REAPER's new tracks have, and the
  project file now turns off, as the fake has no input monitoring. Solo
  writes &16, and solo in place &16 and &32.
- **The master** is shown in the track panel by the project:
  `MASTERTRACKVIEW`'s first field, which View: Toggle master track visible
  (40075) toggles, and whose toggle state it is. A project file without it
  hides the master. Hidden, the master has &512, though its `B_SHOWINTCP`
  reads 1 either way. The project file writes it. Its `GetTrackColor()`
  reads 0, even when it has a color, which the project file writes as
  REAPER does (`MASTERPEAKCOL`). `P_NAME` can't read or set its name: both
  return false, and reading empties the buffer. `GetTrackState()` names it
  `MASTER`.
- **Unchecked by contract tests,** as nothing in REAPER's API can show them:
  `GetTrackGUID()` keeps returning the same pointer as tracks are added, and
  REAPER keeps the GUIDs in the project file (it did here), so they stay
  fake only.

The track changes' contract tests, in CL6 (2026-10-09), explored with
temporary tests first, found these gaps in the fake, each fixed with its test
(see "Track changes" in `fake_reaper.cc`):
- **Solo** is in place by default: `SetTrackUISolo()` with 1, or a toggle,
  sets &16 and &32 and returns 2. 2 solos not in place and returns 1, and 4
  solos in place. A grouped or ganged solo carries its mode. The master's
  solo returns 2 but sets only &16, and `AnyTrackSolo()` leaves it out.
- **The master** can't be rec armed: `SetTrackUIRecArm()` returns -1.
- **Selection ganging** changes every selected track, the master too, and
  then grouping changes each of their groups, but a group's selected tracks
  aren't ganged in turn. `CSurf_On*ChangeEx()`'s `allowGang` allows both.
- **Mute and solo** set the ganged and grouped tracks to the track's new
  value, even when the track's didn't change.
- **Rec arm** changes nothing if the track's doesn't change. When it does,
  ganged tracks are set to its new value, but grouped tracks are toggled.
  And a rec arm that ganging or grouping could take to other tracks is sent
  as a change to every track, as `SurfaceNotifier` now does for grouping too.
- **Volume and pan** move ganged and grouped tracks by the same change, where
  the fake set them to the same value: volume by its ratio, and pan by its
  difference. REAPER measures the change from where each track was when the
  gesture began (it held across a test's calls), so a track clamped at an
  end of pan, or a volume through -inf, comes back to where it was; the fake
  fails the test there, as it isn't faked. A pan past an end is clamped.

## CLs

### CL1 [x] common/testing: Write a FakeProject as an RPP file

Depends on: nothing.

- `FakeProject::Create()`, for a project outside the fake.
- `WriteProjectFile()` (`project_file.h`), with every field above, and a test
  failure for the rest.
- Unused, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `project_file_test.cc`: each field's text, and the state it can't write
  failing the test.

### CL2 [x] common/testing: ContractTest, with the notifier's tests on it

Depends on: CL1.

- `RecordingSurface` in its own file, and `ContractTest` with
  `contract_test_fake.cc`, which writes the project too (see above).
- `surface_notifier_test.cc` splits: its contract tests move to
  `surface_notifier_contract_test.cc` on `ContractTest`, and those that need
  the fake stay.

**Verify**
- Standard checks.
- The notifier's tests pass unchanged on the new fixture.

### CL3 [x] common/testing: Run the contract tests in REAPER

Depends on: CL2.

- To confirm 1 to 3, with the user, as the runner is built.
- A library without the fake, for what the DLL links: `RecordingSurface`,
  `FakeProject`, `WriteProjectFile()`, and `contract_test.h`, which
  `jpr_common_testing` then depends on. What its files take from the fake's
  headers moves out with them: `FakeReaper::kBeatsPerMinute`, and the action
  IDs in `reaper_actions.h` that contract tests use. Then a contract test that
  uses the fake doesn't build into the DLL.
- `contract_test_reaper.cc`, the `reaper_jprsurf_check` DLL, and the
  `check_in_reaper` target and script.
- How to install the test install and set it up (the portable install, the
  license, `JPR_REAPER_CHECK_DIR`, and its `reaper.ini`: the dummy audio
  device and the `RecordingSurface`), and how to run it, in
  `testing_and_profiling.md`.
- The notifier's tests that fail in REAPER (see Found in REAPER) are fixed in
  CL4, so they fail in REAPER until then.

**Verify**
- Standard checks.
- `check_in_reaper` runs the notifier's contract tests in REAPER, and reports
  the failures above, with REAPER quitting cleanly. A run of only the tests
  that pass (`GTEST_FILTER`) passes.

### CL4 [x] common/testing: Make the notifier match REAPER

Depends on: CL3.

- `SurfaceNotifier`, and its contract tests, follow what REAPER does (see Found
  in REAPER), and its comments and "Seen in traces" in
  `testing_and_profiling.md` say so. A call REAPER makes later, outside the
  call, isn't made, as no test spans runs: a mute or solo left for the next
  run is forgotten when the surface runs (`FakeReaper::GetRuns()`).
- The automation mode actions are tested with tracks selected too.
- The route tests get a fixture of their own that opens the project with a
  send, rather than opening a second project, as each open takes about half a
  second in REAPER. `RecordingSurface` names the route calls.
- The project file writes `PANMODE 3` and `MASTER_PANMODE 3`.
- Surface tests that relied on the old calls are updated, or show a change in
  the surface, which is its own follow-up. Only the trace's test relied on
  them.

**Verify**
- Standard checks, and `check_in_reaper` passes.

### CL5 [x] common/testing: Contract tests of tracks

Depends on: CL4.

- `CountTracks`, `GetTrack`, `GetMasterTrack`, `GetParentTrack`,
  `GetTrackGUID`, `GetTrackState`, `GetMediaTrackInfo_Value`,
  `GetSetMediaTrackInfo_String`, `GetTrackColor`, `GetTrackUIVolPan`, and the
  GUID text functions, moved from `fake_reaper_test.cc` where they act only
  through the API, into `fake_reaper_contract_test.cc`.
- Fixes to the fake that REAPER shows, each with its test (see Found in
  REAPER): `GetTrackState()`'s folder and hidden flags, and the master's color
  and name. The project file writes input monitoring off, which the fake
  doesn't have, and the master's visibility in the track panel.
- `GetTrackState()`'s flags in `common/track_state.h`, for `Track`, the fake,
  and the contract tests, where each had its own.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL6 [x] common/testing: Contract tests of track changes and selection

Depends on: CL5.

- The track setters, grouping, `PreventUIRefresh`, `AnyTrackSolo`, and the
  selection functions, moved from `fake_reaper_test.cc` where they act only
  through the API, into `fake_reaper_contract_test.cc`.
- Selection ganging (see Found in REAPER), which the fake then models, and
  `SurfaceNotifier`'s comment no longer excepts.
- Fixes to the fake that REAPER shows, each with its test (see Found in
  REAPER): solo in place (`FakeTrack::solo_in_place`, which the project file
  writes), the setters' return values, the master's rec arm, ganging, how
  grouping changes each property, and pan clamping.
- `SurfaceNotifier` sends a grouped rec arm as a change to every track, as it
  does a ganged one.
- The setters' group flags in `common/track_state.h`, for `Track`, the fake,
  and the contract tests, where `Track` and the fake each had their own.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL7 [ ] common/testing: Contract tests of routes

Depends on: CL5.

- Route counts and indexing (hardware outputs before sends, receives as
  negative indexes), `P_DESTTRACK` and `P_SRCTRACK`, and the route getters and
  setters.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL8 [ ] common/testing: Contract tests of actions, the timeline, and text

Depends on: CL5.

- `Main_OnCommand` and the actions `AddReaperActions()` adds (their text,
  toggle states, and the effects of those with handlers: the ruler and
  automation modes), `NamedCommandLookup`, the automation override, the
  transport, `format_timestr_pos`, `mkvolstr`, `mkpanstr`, undo points,
  `Undo_CanRedo2`, `IsProjectDirty`, and `CountSelectedMediaItems`.
- View: Toggle master track visible (40075), if JPRSurf comes to use it: it
  toggles the master's `show_in_tcp`, and its toggle state is that (see Found
  in REAPER).

**Verify**
- Standard checks, and `check_in_reaper`.

### CL9 [ ] common/testing: Undo

Depends on: CL6, CL7.

- To confirm 4. Then either the fake models what Undo restores, with the
  contract tests passing under both, or it doesn't, and why is recorded here
  and in the fake's class comment.
- Which setters leave an undo point, as an undo with nothing to undo calls
  nothing back, where `SurfaceNotifier` always sends Undo's calls (see Found
  in REAPER).

**Verify**
- Standard checks, and `check_in_reaper`.

### CL10 [ ] docs: Coverage, and CLAUDE.md

Depends on: CL3 to CL9.

- The Coverage table complete, against `JPR_REAPER_API`.
- CLAUDE.md's Checking in REAPER: a fact about REAPER's API is checked with a
  contract test in the test install where it can be, rather than with
  temporary code in the user's REAPER. "Seen in traces" stays for what REAPER
  calls between runs and from its own UI. Commands gains `check_in_reaper`,
  and Parallel sessions says it runs in the main session only.

**Verify**
- Standard checks.

## Checks in REAPER

`check_in_reaper` is the check: it passes. Nothing in the plugin changes, so
there is no smoke test or profile.
