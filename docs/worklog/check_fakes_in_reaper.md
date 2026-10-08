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
  fake. gtest writes its results as XML to the path in an environment
  variable. The next `Run()` opens an empty project with
  `Main_openProject("noprompt:...")`, which closes the changed one without
  asking, then quits REAPER (File: Quit REAPER, 40004), with nothing left to
  save. Nothing is torn down from inside the call that ran the tests.
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
- `Main_openProject()` is the only function the DLL calls that isn't on the
  API list. It loads it by name in `contract_test_reaper.cc`, rather than
  adding it to the list, as JPRSurf never calls it and the fake must never
  fake it.

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
1. **The test install runs unattended:** no license or evaluation prompt, no
   first run dialogs, and no audio device error with the dummy device, and it
   starts as its own instance while the user's REAPER is open (`-newinst`).
   The transport still plays and stops on the dummy device, for the play
   state's tests.
2. **Opening a project from inside `Run()`** with `Main_openProject("noprompt:")`
   finishes within the call, and the project reads back as written, GUIDs
   included. What REAPER calls on the surface while it opens.
3. **Quitting:** opening an empty project with `noprompt:`, then File: Quit
   REAPER from the next `Run()`, exits without any prompt.
4. **Undo:** what Edit: Undo restores after each setter on the API list (mute,
   solo, rec arm, selection, volume, pan, the send setters, and the override),
   which settles whether the fake models undo. These are contract tests that
   fail under the fake until it does, so they are checked in REAPER first.

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

### CL2 [ ] common/testing: ContractTest, with the notifier's tests on it

Depends on: CL1.

- `RecordingSurface` in its own file, and `ContractTest` with
  `contract_test_fake.cc`, which writes the project too (see above).
- `surface_notifier_test.cc` splits: its contract tests move to
  `surface_notifier_contract_test.cc` on `ContractTest`, and those that need
  the fake stay.

**Verify**
- Standard checks.
- The notifier's tests pass unchanged on the new fixture.

### CL3 [ ] common/testing: Run the contract tests in REAPER

Depends on: CL2.

- To confirm 1 to 3, with the user, as the runner is built.
- `contract_test_reaper.cc`, the `reaper_jprsurf_check` DLL, and the
  `check_in_reaper` target and script.
- How to install the test install and set it up (the portable install, the
  license, `JPR_REAPER_CHECK_DIR`, and its `reaper.ini`: the dummy audio
  device and the `RecordingSurface`), and how to run it, in
  `testing_and_profiling.md`.
- Any notifier test that fails in REAPER is a gap in the notifier, fixed here
  with its test, or in its own CL if it is more than a line.

**Verify**
- Standard checks.
- `check_in_reaper` passes, and fails with a test made to fail.

### CL4 [ ] common/testing: Contract tests of tracks

Depends on: CL3.

- `CountTracks`, `GetTrack`, `GetMasterTrack`, `GetParentTrack`,
  `GetTrackGUID`, `GetTrackState`, `GetMediaTrackInfo_Value`,
  `GetSetMediaTrackInfo_String`, `GetTrackColor`, `GetTrackUIVolPan`, and the
  GUID text functions, moved from `fake_reaper_test.cc` where they act only
  through the API.
- Fixes to the fake that REAPER shows, each with its test.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL5 [ ] common/testing: Contract tests of track changes and selection

Depends on: CL4.

- The track setters, grouping, `PreventUIRefresh`, `AnyTrackSolo`, and the
  selection functions.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL6 [ ] common/testing: Contract tests of routes

Depends on: CL4.

- Route counts and indexing (hardware outputs before sends, receives as
  negative indexes), `P_DESTTRACK` and `P_SRCTRACK`, and the route getters and
  setters.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL7 [ ] common/testing: Contract tests of actions, the timeline, and text

Depends on: CL4.

- `Main_OnCommand` and the actions `AddReaperActions()` adds (their text,
  toggle states, and the effects of those with handlers: the ruler and
  automation modes), `NamedCommandLookup`, the automation override, the
  transport, `format_timestr_pos`, `mkvolstr`, `mkpanstr`, undo points,
  `Undo_CanRedo2`, `IsProjectDirty`, and `CountSelectedMediaItems`.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL8 [ ] common/testing: Undo

Depends on: CL5, CL6.

- To confirm 4. Then either the fake models what Undo restores, with the
  contract tests passing under both, or it doesn't, and why is recorded here
  and in the fake's class comment.

**Verify**
- Standard checks, and `check_in_reaper`.

### CL9 [ ] docs: Coverage, and CLAUDE.md

Depends on: CL3 to CL8.

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
