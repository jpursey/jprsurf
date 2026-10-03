# Surface tests

The smoke test as unit tests. The plugin is loaded into the fake REAPER exactly
as REAPER loads it, a fake X-Touch and extender are connected, and the tests
press buttons, move faders, and turn pots, then check what REAPER was asked to
do and what the hardware shows. They follow the smoke scenario in
`profiles/README.md`, step by step, on the same SurfaceTest project, and cover
the rest of CLAUDE.md's smoke test list: faders, pots, meters, scribble strips,
the master fader, and the timecode display.

CLAUDE.md's smoke test then shrinks to what the fakes can't show, and each
feature from then on adds surface tests of its own behavior, so the smoke test
grows as tests rather than as a list to run by hand. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Running the plugin,
Tests).

There is no change in behavior. The plugin's entry point moves to
`dll_main.cc`, which now reads the environment (whether to trace, and where
the profile goes) and passes it to `Plugin::Load()`, so nothing a test loads
reads it.

## Design

### Names

| Name              | What                                              | Might be confused with                     |
| ----------------- | ------------------------------------------------- | ------------------------------------------ |
| `jpr_plugin`      | The plugin as a static library, which tests link  | `reaper_jprsurf`, now just the DLL         |
| `Plugin::Options` | The trace and profile paths `dll_main.cc` found   | `ControlSurface::Type`; the config string  |
| `SurfaceNotifier` | Makes REAPER's calls back to the surface          | `ControlSurfaceListener`, which hears them |
| `SurfaceTest`     | The fixture: fake, X-Touches, plugin, and surface | `TestControlSurface`, which it holds       |

`SurfaceNotifier` says what it does, plainly. The alternatives were
`FakeCallbacks` ("callback" already names the scene's `CallbackToggleProperty`
and the surface type's `create_listener`) and `ReaperEcho` (cute, and vague).
`SurfaceTest` is the fixture the design doc's example already uses; a fixture
is what the harness is, so it needs no other name.

### What REAPER calls back (common/testing)

The fake never calls the surface by itself, so a test makes the calls REAPER
would. For the whole plugin that is too many to make by hand, and they have to
be made at the right moment: REAPER makes most of them from inside the function
JPRSurf called, in the middle of a run. A `SurfaceNotifier` hooks the fake's
functions with `gb::FunctionHook`, and makes those calls on the open surface,
as the 2026-09-29 smoke trace showed (see To confirm):

```
// Makes the calls REAPER makes on the open control surface from inside its own
// functions, as traces showed (see "Seen in traces" in
// docs/testing_and_profiling.md), for as long as it exists. A test of a whole
// surface creates one before the plugin loads; a test of ControlSurface itself
// makes the calls it needs by hand, and has none.
class SurfaceNotifier final {
 public:
  explicit SurfaceNotifier(FakeReaper* reaper);
  ~SurfaceNotifier();
};
```

| JPRSurf calls                       | REAPER calls back                                  |
| ----------------------------------- | -------------------------------------------------- |
| `SetOnlyTrackSelected`              | `SetSurfaceSelected()` for each track that changed |
| `SetTrackSelected`                  | `SetSurfaceSelected()` for the track               |
| `SetTrackUIMute`, `SetTrackUISolo`  | The track's mute and solo, and the master's solo   |
| `SetTrackUIRecArm`                  | `SetTrackListChange()`, then every track's state   |
| `CSurf_OnVolumeChangeEx`            | The last touched track (`Extended()`)              |
| `CSurf_OnPanChangeEx`               | The last touched track (`Extended()`)              |
| `SetGlobalAutomationOverride`       | Every track's volume, pan, and selection           |
| Automation modes (40400-40404)      | `SetAutoMode()`, then as the override does         |
| Undo and redo (40029, 40030)        | Everything (see "Undo resends everything")         |

- **When.** The track setters' calls come at the end of the batch
  (`PreventUIRefresh(-1)`), or in the call when there is none. The rest come in
  the call: for an action, after its handler has made its change.
- **Everything seen, not just what JPRSurf hears.** `ControlSurface` only acts
  on four of these today (`SetTrackListChange()`, `SetSurfaceSelected()`,
  `SetAutoMode()`, and the last touched track), but the notifier makes every
  call the trace showed, so a surface that starts listening to another one is
  already tested against what REAPER sends.
- **Every track's state** is as "Seen in traces" lists it: master first, then
  each track in order.
- **Nested calls.** The notifier makes its calls on `FakeReaper::GetSurface()`,
  the surface the `TestControlSurface` passes calls to, rather than on the
  `TestControlSurface`. So they are part of the entry point that called the
  function, whether a run or the test's own calls, and the fake's checks still
  see one entry point per call REAPER makes.
- **Order of hooks.** The notifier is created before the plugin loads and
  destroyed after it unloads, so its hooks are under the profiler's and the
  trace's, as REAPER's own functions are.

**Brittleness:** the notifier knows only what traces showed. A function JPRSurf
starts calling that makes REAPER call back needs a new trace and a new row
here, or the tests silently get no callback. The design doc's "Seen in traces"
lists what has been traced, which is the place to check when a function is
added to the API list.

### The plugin, split (plugin)

```
// What the plugin reads from its environment when REAPER loads it (see
// dll_main.cc). Empty paths turn each off, as tests load it.
struct Plugin::Options {
  std::filesystem::path trace_path;    // Set by JPRSURF_TRACE.
  std::filesystem::path profile_path;  // Beside the log.
};

static bool Plugin::Load(HINSTANCE hinstance, reaper_plugin_info_t& info,
                         const Options& options);
```

- `jpr_plugin` is a static library with `plugin.cc` and `plugin_surface.cc`.
  `reaper_jprsurf` is the DLL: `dll_main.cc`, with `DllMain`, and
  `REAPER_PLUGIN_ENTRYPOINT`, which moves there from `plugin.cc` (a static
  library would drop an export nothing references).
- `dll_main.cc` reads `JPRSURF_TRACE` and finds the log directory, and passes
  them to `Load()` in `Options`. `PluginSurface::Register()` takes the profile
  path rather than finding it. So `dll_main.cc` is the only code that reads
  the environment, and the only code no test runs, and no test can write the
  user's trace or profile, whatever `JPRSURF_TRACE` is set to.
- `Plugin`'s instance, and the trace it owns, get a `TestReset` in `plugin.cc`,
  so a test that fails before unloading doesn't leave it for the next.

### SurfaceTest (plugin/testing)

A gtest fixture in a test-only `jpr_plugin_testing` library, which the plugin's
tests derive from:

```
class SurfaceTest : public testing::Test {
 protected:
  // With an extender to the left of the X-Touch, as JPRSurf expects, unless a
  // test asks for the X-Touch alone.
  explicit SurfaceTest(bool extender = true);
  ~SurfaceTest() override;  // Checks that nothing logged an error.

  // Loads the plugin, and adds the surface, as REAPER does at startup. Then
  // calls SetTrackListChange() and runs once, as REAPER does when the project
  // loads.
  void AddSurface();

  // Builds the SurfaceTest project the smoke scenario runs on (see
  // profiles/README.md), as the 2026-09-29 trace lists it: T1 to T25, T2's,
  // T3's, and T5's nested folders, T5's 32 children, and T4's children with
  // their sends: T4.1-T4.4 to T4.7, T4.5 and T4.6 to T4.8, and T4.7 and T4.8
  // to T4.9.
  void AddSmokeProject();
  FakeTrack* GetTrack(std::string_view name);

  // Presses that need the clock moved: a double press (two presses within
  // the double press time), and a long press (held past the long press time).
  // Each ends released, after a run.
  void DoublePress(FakeXTouch& xtouch, FakeXTouch::StripButton button,
                   int strip);
  void LongPress(FakeXTouch& xtouch, FakeXTouch::StripButton button,
                 int strip);
  ...

  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::optional<FakeXTouch> xtouch_ext_;  // Strips 1-8, if there is one.
  FakeXTouch xtouch_;                     // Strips 9-16, or 1-8 alone.
  std::unique_ptr<TestControlSurface> surface_;
};
```

Tests use the fake REAPER, the fake X-Touches, and the surface directly: press
on an X-Touch, `surface_->Run()`, then check the fake's tracks and the
X-Touch's lights. The fixture only holds what every test needs, and the few
helpers that save getting the clock right.

- **Loading:** `Plugin::Load()` with the fake's plugin info and no options, then
  `AddSurface()`. The destructor removes the surface, unloads the plugin, and
  then the X-Touches and the fake go, in that order.
- **Errors fail the test.** A log sink records anything logged at `ERROR` or
  above, and the fixture fails the test with it, as `jprsurf.log` with no new
  errors is the first check in REAPER.
- **REAPER's actions:** the fake records every action `Main_OnCommand()` runs
  (`GetCommandsRun()`), which is what most tests check. A few actions must
  also exist in the fake (`AddCommand()`), which the fixture adds:
  - Those whose toggle state a light shows: Cycle, Click, Solo in front, and
    the ruler modes.
  - Those whose effect a test needs, with a handler that makes it, as REAPER
    would: the ruler modes as a radio group (as `TimelineTest::AddModes()`
    does), so the timecode button steps through them, and the automation mode
    actions, which set the selected tracks' modes (the master's too).

  These handlers model what REAPER's actions do, which the backlog's *Check the
  fakes in REAPER* checks.
- Its own tests, in `jpr_plugin_testing_test`, check the fixture: the surface
  loads with both models and with the X-Touch alone, the smoke project's tracks
  and routes, and that an error logged fails the test
  (`EXPECT_NONFATAL_FAILURE`).

### Undo

The fake records the undo points JPRSurf adds (`GetUndoPoints()`), and what
Edit: Redo would redo, but it doesn't undo: REAPER's undo restores project
state that JPRSurf never sees, including its own points for track volume and
pan. So the tests check JPRSurf's part: each gesture adds one undo point with
its name (a range is one, route moves merge into one after 500ms), Undo and
Redo run 40029 and 40030, the Undo light follows `Undo_CanRedo2()`, and the
surface follows the state an undo leaves (the test's handler for Undo sets it,
and the notifier sends what REAPER sends). That the faders and pans come back
(smoke step 14) is REAPER's, and stays a check in REAPER until *Check the fakes
in REAPER* shows what undo restores, and whether the fake should model it.

### Tests

By area, one file each, following the smoke scenario's steps where it has them:
- **`track_strip_test.cc`** (smoke steps 1-3, 5): faders, pots, pot buttons,
  mute, solo, and rec arm, each way between the surface and REAPER; meters;
  scribble names, volumes, and colors; the ring for a folder and an empty
  strip; the master fader; and ranges of mute, solo, and rec arm.
- **`track_list_test.cc`** (4, 6, 7, 20-25): select's press, double press into
  a folder, and long press and range; Global, pressed and held, and its light;
  Bank and Channel left and right, at the ends too; and the X-Touch alone.
- **`send_mode_test.cc`** (8-19): the Track and Send lights; entering by
  tapping Send, and by holding Send and pressing select; route strips; the Info
  strip; switching between sends and receives; walking the routes with select;
  route undo points; and following a track touched in REAPER.
- **`global_test.cc`** (26-40): the transport, with Rewind and Forward by
  measure, beat, and marker; Cycle, Click, and Solo; the timecode and ruler
  modes; the utility buttons; and automation modes and the global override,
  each with its lights.

Modifiers on track controls, grouping, and the detail of ranges are left to
*Scene tests*, which test them on the scene directly.

### To confirm

The 2026-09-29 trace of the smoke scenario (`jprsurf_trace.txt`, still on disk)
already answers what the design doc's To confirm asked of this item, for every
call the surface makes:
- **Selection** notifies each track whose selection changed:
  `SetOnlyTrackSelected()` during the call, and `SetTrackSelected()` in a
  batch at `PreventUIRefresh(-1)`.
- **Solo** notifies as mute does.
- **Rec arm** is different: REAPER calls `SetTrackListChange()` and resends
  every track's state, from the setter, or at the end of the batch. So every
  rec arm refreshes the track list.
- **Sends** notify nothing: `SetTrackSendUIVol()`, `SetTrackSendUIPan()`, and
  `ToggleTrackSendUIMute()` called nothing back.
- **The automation mode actions** call `SetAutoMode()` and then resend every
  track's volume, pan, and selection, and so does
  `SetGlobalAutomationOverride()`.

What the trace doesn't have is changes made in REAPER's own UI, which a few
tests make by hand. Volume and pan changes in REAPER's UI don't matter, as
JPRSurf polls them. What does is selecting a track in REAPER, which Send mode
follows (CL7):
- **Fact:** what clicking a track in REAPER's track panel calls on the surface,
  and in what order. Expected: `Extended(CSURF_EXT_SETLASTTOUCHEDTRACK)`, then
  `SetSurfaceSelected()` for each track whose selection changed.
- **Check:** with `JPRSURF_TRACE=1`, click a track's name in the track panel,
  then Ctrl+click another, then set the global automation override to Latch
  Preview in REAPER, and exit.

No temporary code is needed: the trace is enough. Its findings, and the
2026-09-29 trace's, go into "Seen in traces" in the design doc (CL1), and the
design doc's To confirm table goes, answered.

## CLs

### CL1 [x] common/testing: SurfaceNotifier

Depends on: nothing. The UI check in To confirm only before CL7.

- `FakeReaper::GetSurface()`, whose calls are part of the entry point in
  progress. The state the notifier reads without calling the API:
  `FakeReaper::IsUIRefreshPrevented()`, `GetToggleState()`, and
  `kBeatsPerMinute`, and `FakeProject::AnyTrackSolo()`, which the fake's own
  functions now use too.
- `SurfaceNotifier` (`surface_notifier.cc`/`.h`), and
  `surface_notifier_test.cc`: each row of the table, including the batch rule
  (nothing sent until `PreventUIRefresh(-1)`).
- The tests are written so they can later run in REAPER as they are (see
  *Check the fakes in REAPER* in the backlog): each acts only through REAPER's
  API, on a surface that records every call it gets, registered as REAPER's
  plugin info registers one. Only building the project they start from uses
  the fake (`FakeProject`), in one function per project, which an RPP file
  would replace. What `ControlSurface` does with the calls is already tested
  in `common`.
- `testing_and_profiling.md`: the 2026-09-29 trace's findings in "Seen in
  traces", the To confirm table answered and removed, and the notifier in the
  pieces at a glance and Behavior.
- Unused, so no visible change.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL2 [ ] plugin: Split the plugin into a library and a DLL

Depends on: nothing.

- `jpr_plugin` and `reaper_jprsurf` in `src/jpr/plugin/CMakeLists.txt`, with
  deployment staying on the DLL.
- The entry point in `dll_main.cc`, reading the environment into
  `Plugin::Options`. `Plugin::Load()` takes them, and
  `PluginSurface::Register()` takes the profile path.
- `Plugin`'s `TestReset`.
- `plugin_test.cc`: loading through the fake registers the surface type, and
  adding a surface opens the X-Touch's ports; loading twice fails, as does a
  version mismatch or no `GetFunc`; unloading forgets the instance; and a
  test's leftover instance is reset.

**Verify**
- Standard checks.
- The plugin still loads in REAPER (done with the end of the feature): no
  profile or trace is written by `ctest`, and REAPER still writes both.

### CL3 [ ] plugin/testing: SurfaceTest

Depends on: CL1, CL2.

- The `jpr_plugin_testing` library, with `SurfaceTest`, and its own tests (see
  SurfaceTest).
- `jpr_plugin_TEST_DEPS` links it.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL4 [ ] plugin: Track strip tests

Depends on: CL3.

- `track_strip_test.cc` (see Tests).
- Anything the tests find wrong is fixed in its own follow-up CL, after this
  one, not here. The same goes for each test CL below.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL5 [ ] plugin: Track list tests

Depends on: CL3.

- `track_list_test.cc` (see Tests).

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL6 [ ] plugin: Global control tests

Depends on: CL3.

- `global_test.cc` (see Tests).

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL7 [ ] plugin: Send mode tests

Depends on: CL3, and the UI check in To confirm.

- `send_mode_test.cc` (see Tests).

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL8 [ ] docs: The smoke test shrinks

Depends on: CL4-CL7.

- CLAUDE.md's smoke test becomes what the fakes can't show: REAPER loads the
  extension (`dll_main.cc`), changes made in REAPER's own UI that no trace has
  covered, that the fake X-Touch agrees with the hardware and how it feels,
  and undo restoring REAPER's state. New features add surface tests of their
  own behavior.
- `testing_and_profiling.md`: the surface harness as built, and the Tests
  table.

**Verify**
- None beyond reading it: docs only.

## Checks in REAPER

- REAPER loads the extension, `jprsurf.log` has no new errors, and closing
  REAPER still writes `jprsurf_profile.txt`.
- With `JPRSURF_TRACE=1`, REAPER still writes `jprsurf_trace.txt`.
- The smoke test, as CL8 leaves it. There is no idle or smoke profile: nothing
  on the realtime path changes.
