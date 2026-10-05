# Surface tests

The smoke test as unit tests. The plugin is loaded into the fake REAPER exactly
as REAPER loads it, a fake X-Touch and extender are connected, and the tests
press buttons, move faders, and turn pots, then check what REAPER was asked to
do and what the hardware shows. They cover what the smoke scenario in
`profiles/README.md` does, and the rest of CLAUDE.md's smoke test list:
faders, pots, meters, scribble strips, the master fader, and the timecode
display. Each test builds just the project the behavior it checks needs,
rather than the SurfaceTest project the smoke scenario runs on, which stays
for profiling in REAPER.

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
- **Nested calls.** The notifier makes its calls on the surface the
  `TestControlSurface` wraps (`TestControlSurface::GetWrapped()`), rather than
  on the `TestControlSurface`. So they are part of the entry point that called
  the function, whether a run or the test's own calls, and the fake's checks
  still see one entry point per call REAPER makes. Its calls for a change in
  REAPER's own UI (CL7) are made on the `TestControlSurface`
  (`FakeReaper::GetSurface()`), as they come between runs, each an entry point
  of its own.
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

### SurfaceTest (plugin/testing) and DefaultConfigTest (plugin/default_config)

`SurfaceTest` is a gtest fixture in a test-only `jpr_plugin_testing` library,
for tests of the whole surface, whatever its config: the fake REAPER, the
plugin loaded into it, and helpers for the project and for pressing fake
devices. Each config's tests derive a fixture of their own from it, with that
config's fake devices. Today the only config is the one `PluginSurface`
builds, so the only one is `DefaultConfigTest`, in a test-only
`plugin/default_config` (see CL5a):

```
class SurfaceTest : public testing::Test {
 protected:
  SurfaceTest();
  // Calls RemoveSurface(), before any fixture's members go, so the surface
  // goes before the fake devices a derived fixture holds.
  void TearDown() override;

  // Loads the plugin, and adds the surface, as REAPER does at startup. Then
  // calls SetTrackListChange(), as REAPER does when the project loads, and
  // runs until the devices show the project.
  void AddSurface();

  // Removes the surface, if there is one, and unloads the plugin, as REAPER
  // does at exit.
  void RemoveSurface();

  // Runs the surface until the devices show the project as it is now: two
  // runs, as each run reads the project before it acts (see CL4b).
  void RunUntilShown();

  // Adds `count` tracks to the end of `folder`, or of the current project if
  // it is null, and returns them. Each is named for where it is: T1, T2, and
  // so on at the top level, and T2.1, T2.2, and so on in T2.
  std::vector<FakeTrack*> AddTracks(int count, FakeTrack* folder = nullptr);

  // Presses that need the clock moved, each ending released, once whatever
  // it started is done and shown (a press held back in case it is a double
  // press, and then RunUntilShown()): a tap, a double press (two presses
  // within the double press time), and a long press (held past the long
  // press time). Overloads are added as tests need them.
  void Tap(FakeXTouch& xtouch, FakeXTouch::Button button);
  void Tap(FakeXTouch& xtouch, FakeXTouch::StripButton button, int strip);
  void DoublePress(FakeXTouch& xtouch, FakeXTouch::StripButton button,
                   int strip);
  void LongPress(FakeXTouch& xtouch, FakeXTouch::Button button);

  // Pressed, and held past the long press time, but not released: the test
  // releases it, after pressing whatever it holds the button for.
  void Hold(FakeXTouch& xtouch, FakeXTouch::StripButton button, int strip);

  // Touches the fader, moves it, and lets go, as a hand does.
  void MoveFader(FakeXTouch& xtouch, int fader, int position);

  gb::LogErrorGuard log_error_guard_;
  FakeReaper reaper_;
  SurfaceNotifier notifier_{&reaper_};
  std::unique_ptr<TestControlSurface> surface_;
};

// The X-Touch and its extender that PluginSurface expects.
class DefaultConfigTest : public SurfaceTest {
 protected:
  // With an extender to the left of the X-Touch, unless `extender` is false,
  // for the X-Touch alone.
  explicit DefaultConfigTest(bool extender = true);

  // Strips are numbered across the surface: 0-7 on the extender, and 8-15 on
  // the X-Touch, or 0-7 on the X-Touch alone.
  FakeXTouch& GetXTouch(int strip);
  static int GetXTouchStrip(int strip);
  std::string GetName(int strip);  // Without the spaces after it.
  std::string GetBottomLine(int strip);  // The same, for its bottom line.
  FakeXTouch::Light GetLight(FakeXTouch::StripButton button, int strip);

  // Taps select on `strip`, or holds it on `first` and taps it on `last`.
  void TapSelect(int strip);
  void SelectRange(int first, int last);
  void EnterSendMode(int strip);  // Selects only its track, and taps Send.

  std::optional<FakeXTouch> xtouch_ext_;  // Strips 0-7, if there is one.
  FakeXTouch xtouch_;                     // Strips 8-15, or 0-7 alone.
};
```

The press helpers stay in `SurfaceTest`: they take the fake X-Touch they
press, and hold no config's state, so any config with an X-Touch uses them.

Tests use the fake REAPER, the fake X-Touches, and the surface directly: press
on an X-Touch, `surface_->Run()`, then check the fake's tracks and the
X-Touch's lights. The fixture only holds what every test needs, and the few
helpers that save getting the clock right, or naming tracks by hand.

- **Projects:** each test, or each file's fixture, builds the project the
  behavior it checks needs, with `AddTracks()` and the fake's own methods
  (`AddSend()`, and setting a track's values), and nothing more. So a test
  shows what it depends on, and doesn't break when another test needs a
  different project. Names come from positions, so what a scribble strip
  shows says where the track is.

- **Loading:** `Plugin::Load()` with the fake's plugin info and no options, then
  `AddSurface()`. The destructor removes the surface, unloads the plugin, and
  then the X-Touches and the fake go, in that order.
- **Errors fail the test.** The fixture holds a `gb::LogErrorGuard` (Game
  Bits' *Fail tests on logged errors*), first, so it outlives everything else,
  and fails the test with anything logged at `ERROR` or above, from setup to
  teardown, as `jprsurf.log` with no new errors is the first check in REAPER.
  A test that means to log an error takes it from the guard and checks it.
- **REAPER's actions:** the fake records every action `Main_OnCommand()` runs
  (`GetCommandsRun()`), which is what most tests check. Every action the
  surface maps must also exist in the fake (`AddCommand()`), or its mapping
  fails, and logs an error: a command property is only made for an action
  REAPER has text for. So the fixture adds REAPER's actions from
  `common/testing` (`AddReaperActions()`, CL2a): every action JPRSurf uses (28,
  with the ruler modes besides), with the text and toggle state REAPER
  reported in the 2026-09-29 trace. Those whose effect a test needs have a
  handler that makes it, as REAPER would: the ruler modes as radio groups, so
  the timecode button steps through them, and the automation mode actions,
  which set the selected tracks' modes (the master's too).

  These handlers model what REAPER's actions do, which the backlog's *Check the
  fakes in REAPER* checks. A test gives another action, such as Undo, a
  handler with `FakeReaper::SetCommandHandler()`.
- **Press timing:** the press helpers hold a button for exactly a long press
  (and a run), and settle for exactly the double press window, and then until
  what the press changed is shown, from the times `Control` makes public
  (CL2a), so they follow any change to them.
- Their own tests check the fixtures: `jpr_plugin_testing_test` that an
  error logged is kept, and `AddTracks()`' names and folders, and
  `jpr_default_config_test` that the surface loads with both models and with
  the X-Touch alone, and that each press helper does what the gesture does on
  the surface, on a few tracks and a folder.

### Undo

The fake records the undo points JPRSurf adds (`GetUndoPoints()`), and what
Edit: Redo would redo, but it doesn't undo: REAPER's undo restores project
state that JPRSurf never sees, including its own points for track volume and
pan. So the tests check JPRSurf's part: each gesture adds one undo point with
its name (a range is one, route moves merge into one after 500ms), Undo and
Redo run 40029 and 40030, the Undo light follows `Undo_CanRedo2()`, and the
surface follows the state an undo leaves (the test's handler for Undo sets it,
and the notifier sends what REAPER sends). That the faders and pans come back
(as in smoke step 14) is REAPER's, and stays a check in REAPER until *Check the fakes
in REAPER* shows what undo restores, and whether the fake should model it.

### Tests

By area, one file each in `plugin/default_config` (from CL5a), grouped by
behavior. The smoke scenario's steps each area covers are in parentheses, so
CL8 knows what leaves the smoke test. Each file's project is the least that
shows its behavior:
- **`track_strip_test.cc`** (smoke steps 1-3, 5): faders, pots, pot buttons,
  mute, solo, and rec arm, each way between the surface and REAPER; meters;
  scribble names, volumes, and colors; the ring for a folder and an empty
  strip; the master fader; and ranges of mute, solo, and rec arm. Project:
  fewer tracks than the 16 strips, so some strips are empty, one of them a
  folder, with volumes, pans, and colors set, and ranges that cross from the
  extender to the X-Touch.
- **`track_list_test.cc`** (4, 6, 7, 20-25): select's press, double press into
  a folder, and long press and range; Global, pressed and held, and its light;
  Bank and Channel left and right, at the ends too; and the X-Touch alone.
  Project: top level tracks two banks past the strips, a folder two levels
  deep, and a folder with more children than strips.
- **`send_mode_test.cc`** (8-19): the Track and Send lights; entering by
  tapping Send, and by holding Send and pressing select; route strips; the Info
  strip; switching between sends and receives; walking the routes with select;
  route undo points; and following a track touched in REAPER. Project: a bus
  with several receives, a track sending to two buses, a chain (one track with
  both sends and receives), and a track with only receives.
- **`global_test.cc`** (26-40): the transport, with Rewind and Forward by
  measure, beat, and marker; Cycle, Click, and Solo; the timecode and ruler
  modes; the utility buttons; and automation modes and the global override,
  each with its lights. Project: a few tracks, some selected, for the
  automation modes.

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

What that trace doesn't have is changes made in REAPER's own UI, which a few
tests make. Volume and pan changes in REAPER's UI don't matter, as JPRSurf
polls them. What does is selecting a track in REAPER, which Send mode follows
(CL7). Checked on 2026-10-05, with `JPRSURF_TRACE=1`, by clicking a track
in the track panel, clicking another, Ctrl+clicking a third, and
setting the global automation override to Latch Preview in REAPER:
- **A click** calls `SetSurfaceSelected()` for each track whose selection
  changed, in track order (the track unselected too), then
  `OnTrackSelection()` and `Extended(CSURF_EXT_SETLASTTOUCHEDTRACK)` with the
  track clicked. The expected order was the other way round.
- **A Ctrl+click** adding a track calls the same, with only its
  `SetSurfaceSelected()`.
- **The override** set in REAPER calls what `SetGlobalAutomationOverride()`
  does: every track's volume, pan, and selection, and no `SetAutoMode()`.

These are in "Seen in traces" in the design doc, with the 2026-09-29 trace's
findings (CL1), and `SurfaceNotifier` makes them (CL7).

CL10 found one more. The fake and `TrackCache` hold each track's mixer
visibility (`B_SHOWINMIXER`) on its own, so hiding only a folder still shows its
tracks on the surface, and the `kFolder` comment in `plugin_surface.cc` says a
hidden folder's "strips go blank". Checked on 2026-10-05: showing or hiding a
folder in the mixer (or the track control panel) sets every track in it to the
same, at every depth, whatever each was before, so hiding and showing it again doesn't bring
back a mix of shown and hidden tracks. Each track's own `B_SHOWINMIXER` is
set, which the surface already follows, so only the fake changes:
`FakeProject::ShowInMixer()` does the same (CL10).

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

### CL2 [x] plugin: Split the plugin into a library and a DLL

Depends on: nothing.

- `jpr_plugin` and `reaper_jprsurf` in `src/jpr/plugin/CMakeLists.txt`, with
  deployment staying on the DLL.
- The entry point in `dll_main.cc`, reading the environment into
  `Plugin::Options`. `Plugin::Load()` takes them, and
  `PluginSurface::Register()` takes the profile path.
- `Plugin`'s `TestReset`.
- The fake's plugin info `GetFunc` returns each function as it is loaded at the
  time, rather than the fake's own, so `Plugin::Load()` loading the API again
  keeps the `SurfaceNotifier`'s hooks (which it would otherwise remove,
  failing their reverse order check). The constructor loads the fake's own
  through `GetFakeFunc`. With a test in `fake_reaper_test.cc`.
- `plugin_test.cc`: loading through the fake registers the surface type, and
  adding a surface opens the X-Touch's ports; loading twice fails, as does a
  version mismatch or no `GetFunc`; unloading forgets the instance; the trace
  and profile go to the paths in `Options`; and a test's leftover instance is
  reset.

**Verify**
- Standard checks.
- The plugin still loads in REAPER (done with the end of the feature): no
  profile or trace is written by `ctest`, and REAPER still writes both.

### CL2a [x] common/testing, device: What SurfaceTest shares

Depends on: nothing.

What a fixture of the whole surface needs that isn't about the plugin, so
`TimelineTest`, the coming *Scene tests*, and `SurfaceTest` share it rather
than each copying REAPER's facts:
- **REAPER's actions** (`reaper_actions.h`): `AddReaperActions()` adds every
  action JPRSurf uses to a fake, with the text and toggle state the 2026-09-29
  trace reported, the automation mode actions with a handler that sets the
  selected tracks' modes (the master's too), and the ruler's time unit
  actions, and its secondary ones, each as a radio group (a run turns it on
  and the rest of its group off), as the trace had them (Measure.Beats, and no
  secondary). The automation mode actions' IDs move here from
  `SurfaceNotifier`, checked against `AutoMode` once. `TimelineTest` uses it
  rather than its own `AddModes()`.
- `FakeReaper::SetCommandHandler()`, to give an action already added a handler
  (such as Undo, which the fixture adds).
- `FakeProject::GetSelectedTracks(include_master)`, the selected tracks, master
  first, which the fake's API and the automation handler share, and
  `FakeProject::FindTrackByName()`.
- **device:** `Control`'s long press and double press times become public, so
  the press helpers wait for exactly them, named for their unit
  (`kLongPressDurationSecs`, `kDoublePressWindowSecs`) now that they are.

The ruler's action IDs are `timeline.cc`'s. The trace read the toggle states
of only 41916, 40367, 43205, 43204, and 40365, so the rest are taken on trust
until *Check the fakes in REAPER*.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL3 [x] plugin/testing: SurfaceTest

Depends on: CL1, CL2, CL2a, and Game Bits *Fail tests on logged errors*
(done).

- The `jpr_plugin_testing` library, with `SurfaceTest`, and its own tests (see
  SurfaceTest), including that the fixture's guard records an error logged.
- `jpr_plugin_TEST_DEPS` links it.
- This was CL3 and CL3a, the errors failing the test, which waited on Game
  Bits. Game Bits' item was done first, so they are one CL.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- The fixture's tests log no errors.

### CL4 [x] plugin: Track strip tests

Depends on: CL3.

- `track_strip_test.cc` (see Tests).
- `SurfaceTest` gains `RunUntilShown()`, which `AddSurface()` now ends with,
  and `MoveFader()`. A fader's position is sent a run after the surface reads
  it (see CL4b).
- Anything the tests find wrong is fixed in its own follow-up CL, after this
  one, not here, with its test disabled until then. The same goes for each
  test CL below.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL4a [x] plugin: Empty strips show no volume

Depends on: CL4.

- Found by CL4: an empty strip's bottom scribble line shows "-inf dB", as the
  stub track's volume is 0. It should be blank, as the top line is.
- The volume line's mapping in `PluginSurface` only acts while its track
  exists (a condition on `TrackProperties::kTrackExists`), so an empty strip
  has no writer, and is cleared. The ring does the same with a mode override,
  but the scribble strip's text has no modes. `config_model.md` says how a
  list's empty instances show nothing: by these, not by the runtime.
- `track_strip_test.cc`: the empty strip's bottom line is blank (its disabled
  test folded into `EmptyStripScribbleIsBlank`), a track added to an empty
  strip shows its volume, and a deleted track's strip goes blank.

**Verify**
- Standard checks.
- An empty strip's bottom line is blank in REAPER (with the end of the
  feature).

### CL4b: Faders move on the run that reads the change (dropped)

Found by CL4: every change the scene makes to a fader reaches the X-Touch a
run (1/30s) late, touched or not. A fader's output is bound to its input, so
`Control` holds it as pending, and sends it when the device next runs, and
`PluginSurface::OnRun()` runs the devices before the scene, so input has no
latency.

Not fixed: a run is never noticeable, and a motor fader moves slower than
that anyway. Sending a bound output at once when nothing holds it would give
`Control` two paths for an output where it has one, and the tests would still
need the run after letting go of a fader, which is kept on purpose (the
finger is likely still near it). So `RunUntilShown()` stays.

CL5 found the same run elsewhere: the scene reads the tracks it shows before
it runs its actions, so what an action changes on other tracks (such as the
tracks select unselects) is read, and shown, on the next run. It is the same
run, and the same decision.

### CL5 [x] plugin: Track list tests

Depends on: CL3.

- `track_list_test.cc` (see Tests). The big folder is T20, with 20 tracks,
  so that leaving it shows Global centering it, and a bank in it stops short
  at its last track.
- `SurfaceTest` gains `Hold()`, for a select range, and strips numbered
  across the surface (`GetXTouch()`, `GetXTouchStrip()`, and `GetName()`),
  which its own tests now use too. The press helpers settle for the double
  press window, in whose last run a held back press is acted on, and then
  `RunUntilShown()`, as what the press changes on other tracks is read on the
  run after (see CL4b).

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL5a [x] plugin: The default config's tests

Depends on: CL5.

The surface tests test `PluginSurface`'s one hard-coded config, not the
plugin, and when configs come from files (see *Getting there* in
`config_model.md`), each config will have its own. So they move to a folder of
their own, which will only ever hold tests: a config's mappings are data.
- `plugin/default_config`, with a test-only `jpr_default_config` library (its
  tests are `jpr_default_config_test`): `DefaultConfigTest`, and the track
  strip and track list tests, moved there.
- `SurfaceTest` loses what is the default config's: the fake X-Touches, and
  strips numbered across them. It gains `RemoveSurface()`, which its
  `TearDown()` calls, before any fixture's members go, so the surface goes
  before a derived fixture's fake devices, whatever it holds. Its own tests of
  the presses, and of the X-Touches' order, move to `DefaultConfigTest`'s.
- `jpr_plugin_test` no longer links `jpr_plugin_testing`, which none of its
  tests use.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.
- The same tests run, and pass, in their new places.

### CL6 [x] plugin: Global control tests

Depends on: CL5a.

- `global_test.cc` (see Tests).
- `DefaultConfigTest` gains `TapSelect()` and `SelectRange()`, from the track
  list tests, which use them too.
- Its tests select tracks, and set their automation modes, with the surface's
  buttons, and select the master with `SetTrackSelected()`, so the calls REAPER
  makes back are the ones traces showed (see "Seen in traces"). The surface
  reads the selected tracks' modes again only when REAPER says the selection,
  or a mode, changed, so setting the fake's tracks directly shows nothing. What
  REAPER calls for a change in its own UI is To confirm, for CL7.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL7 [x] plugin: Send mode tests

Depends on: CL5a, and the UI check in To confirm.

- `send_mode_test.cc` (see Tests).
- `DefaultConfigTest` gains how the X-Touch shows 0dB and a pan (`kFader0dB`,
  `kPanRing`, and the ring positions), and `GetLight()` for a strip's button,
  from the track strip and track list tests, which use them too.
- An empty route strip's bottom line shows "-inf dB", as track strips did
  before CL4a. Its test is disabled until CL7a.
- **common/testing:** `SurfaceNotifier` gains a function for each change made
  in REAPER's own UI that a test needs: clicking a track, Ctrl+clicking it,
  and clicking its mute button. Each sets the fake and makes the calls
  traces showed (see To confirm), with its own tests, and a change no trace
  has shown (one to the master, or a mute of one of several selected tracks)
  fails the test. Following a track touched in REAPER is tested with them.
- **common/testing:** those calls come between runs, so each is an entry point
  of its own, made on the `TestControlSurface`, which
  `FakeReaper::GetSurface()` now returns. The surface it wraps, which the
  notifier's other calls are made on, is `TestControlSurface::GetWrapped()`.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL7a [x] plugin: Empty route strips show no volume

Depends on: CL7.

- Found by CL7: an empty route strip's bottom scribble line shows "-inf dB".
  It should be blank, as the top line is.
- CL4a's fix: the volume line's mapping in the route view only acts while its
  route exists (a condition on `RouteProperties::kExists`).
- `send_mode_test.cc`: its disabled test folded into `EmptyRouteStripIsBlank`.

**Verify**
- Standard checks.
- An empty route strip's bottom line is blank in REAPER (with the end of the
  feature).

### CL8 [x] docs: The smoke test shrinks

Depends on: CL4-CL7a.

- CLAUDE.md's smoke test becomes what the fakes can't show: REAPER loads the
  extension (`dll_main.cc`), changes made in REAPER's own UI that no trace has
  covered, that the fake X-Touch agrees with the hardware and how it feels,
  and undo restoring REAPER's state. New features add surface tests of their
  own behavior. The hand checks are made during the smoke scenario the end of
  a feature profiles anyway, so there is one pass in REAPER, not two.
- `testing_and_profiling.md`: a Surface harness section, as built (the
  fixtures, REAPER's actions and calls back, timing, and when a test sets the
  fake or acts through REAPER), in place of the Fake X-Touch section's
  example, which predated it. The Tests table's plugin row lists the files'
  areas, and what still needs REAPER adds what Undo restores.

**Verify**
- None beyond reading it: docs only.

### CL9 [x] plugin: More send mode tests

Depends on: CL8.

What the default config does in Send/Receive mode that neither the smoke
scenario nor CL7 covered, in `send_mode_test.cc`:
- Bank and Channel left and right page through the route strips, on a track
  with more routes than the 15 strips, stopping at each end.
- The Info strip controls the track it shows, as a track strip does: fader,
  pot, pot button, mute, solo, rec arm, color, and meter.
- Deleting the shown track leaves the strips blank until another track is
  touched.
- With the X-Touch alone, 7 route strips and the Info strip.
- Holding Send while a select is held as a range's anchor picks the track
  pressed, rather than selecting the range.
- An empty Info strip's bottom line shows "Send", as route strips did before
  CL7a. Its test is disabled until CL9a. The strip is black, so the hardware
  shows nothing either way.
- `DefaultConfigTest` gains `GetBottomLine()`, from the send mode tests, for
  the X-Touch alone's.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL9a [x] plugin: An empty Info strip shows no route kind

Depends on: CL9.

- Found by CL9: once the Send/Receive mode track is deleted, the Info strip's
  bottom line shows "Send". It should be blank, as its top line is. The line
  is the routes list's own setting (`View::kChildRouteTypeName`), not a value
  of its track, and the list sets it to sends when its subject changes, so it
  has no empty form: only a condition can blank it.
- CL4a's fix: the route kind line's mapping only acts while the track exists
  (a condition on `TrackProperties::kTrackExists`).
- `send_mode_test.cc`: its disabled test folded into
  `DeletedTrackShowsNothingUntilAnotherIsTouched`.

**Verify**
- Standard checks. Nothing to see in REAPER: the strip is black.

### CL10 [x] plugin: More track list tests

Depends on: CL8.

In `track_list_test.cc`:
- The track list scrolls to show the current track: one touched in REAPER, off
  the strips shown, and one reached across a route in Send/Receive mode, on
  returning to Track mode.
- Deleting the shown folder goes back to the top level. Hiding it keeps it,
  with its strips blank, until it is shown again.
- The fixture gains a send from T1 to T20.19, for Send/Receive mode to go to,
  and T20's tracks. `ControlSurface::kVisibilityInterval` is public, so a
  test waits for the visibility poll by it.
- `DefaultConfigTest` gains `EnterSendMode()`, from the send mode tests, which
  the track list's and the X-Touch alone's send mode tests use too.
- **common/testing:** `FakeProject::ShowInMixer()` shows or hides a track as
  the user does in REAPER, setting every track in its folder the same (see To
  confirm), with its own test. The hidden folder test uses it, and the
  `kFolder` comment in `plugin_surface.cc` says why its strips go blank.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

### CL11 [ ] plugin: Whole surface tests

Depends on: CL8.

- `default_config_test_test.cc`: removing the surface, as REAPER does at exit,
  clears the hardware: lights off, faders down, scribble strips blank, and the
  timecode display empty.
- `global_test.cc`: Shift, Control, Alt, and Option are lit while held.

**Verify**
- Standard checks, apart from REAPER: the plugin doesn't change.

## Checks in REAPER

- REAPER loads the extension, `jprsurf.log` has no new errors, and closing
  REAPER still writes `jprsurf_profile.txt`.
- With `JPRSURF_TRACE=1`, REAPER still writes `jprsurf_trace.txt`.
- The smoke test, as CL8 leaves it. There is no idle profile: nothing on the
  realtime path changes. The smoke run is profiled, as the hand checks ride
  along with it.

Checked on 2026-10-05: the extension loads with no new errors, and writes its
profile and trace. The smoke test passed, and its profile matches
`profiles/smoke.txt` (per run, counts per action, and the profiler's cost).
The blank volume line of an empty strip (CL4a and CL7a) can't be seen on the
hardware: an empty strip is black, which shows no text (see X-Touch facts in
[fake_xtouch.md](fake_xtouch.md)). The fixes still stop the surface sending
text nobody can read, and show if a config ever colors empty strips.
