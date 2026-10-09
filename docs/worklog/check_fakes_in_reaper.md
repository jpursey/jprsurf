# Check the fakes in REAPER

Surface tests trust two fakes: the fake REAPER's model of REAPER's state, and
`SurfaceNotifier`'s model of what REAPER calls on the surface. Contract tests
check both against REAPER: tests that act only through REAPER's API, so the same
source runs under the fake, in `ctest`, and inside a test install of REAPER,
with `check_in_reaper`. A test that passes under the fake but fails in REAPER is
a gap in the fake, which is fixed, with that test.

So the surface is tested in three separate parts:
- **The surface's behavior:** surface tests, under the fake only.
- **The fake REAPER:** what each function on the API list does to REAPER's
  state, and the effects of the actions `AddReaperActions()` gives handlers,
  undo included.
- **`SurfaceNotifier`:** what REAPER calls on a surface from inside each
  function.

Every function on the API list has a contract test, or is fake only for a
reason (see Coverage). Running them in REAPER found gaps in nearly every area of
the fakes, each now fixed and tested (see REAPER facts). CLAUDE.md's Checking in
REAPER now checks a fact about REAPER's API with a contract test in the test
install, which Claude runs, rather than with temporary code in the user's
REAPER. The design of the runner is in "Contract tests in REAPER" in
[testing_and_profiling.md](../testing_and_profiling.md).

## Behavior

One change for the user: **a route's volume, then pan, now get an undo point
each.** `Track` told `ContinuousUndo` of a route change after making it, so the
pending point of the other kind (a send's volume, then its pan) held the change
that added it, and the pan never got a point of its own. It now tells it first.
The fake's undo showed it, as
`SendModeTest.EachKindOfRouteMoveHasItsOwnUndoPoint` failed once the fake
modeled it.

The rest is in the tests: the fakes now do what REAPER does, and the surface
tests that relied on the old behavior follow REAPER's (0dB shows as `0.00dB`,
not `+0.00dB`; sends are in the order of the tracks they go to; tests that
needed a redo undo a change).

## Names

| Name                    | What                                                                       | Might be confused with                                                  |
| ----------------------- | -------------------------------------------------------------------------- | ----------------------------------------------------------------------- |
| Contract test           | A test that acts only through REAPER's API, and runs under both            | Surface tests, which run under the fake only                            |
| `ContractTest`          | The gtest fixture contract tests derive from                               | `SurfaceTest`, the fixture for a whole surface                          |
| `RecordingSurface`      | A control surface that records every call it gets, as text                 | `TestControlSurface`, the fake's wrapper of the surface under test      |
| `WriteProjectFile()`    | Writes a `FakeProject` as an RPP file REAPER opens                         | REAPER's own `Main_SaveProject()`                                       |
| Test install            | A portable REAPER only for contract tests, with its own `UserPlugins`      | The user's REAPER, which the plugin deploys to                          |
| `reaper_jprsurf_check`  | The DLL that runs the contract tests inside the test install               | `reaper_jprsurf`, the plugin                                            |
| `check_in_reaper`       | The build target that runs `reaper_jprsurf_check` in the test install      | `ctest`, which runs the same tests under the fake                       |

## Structure

### common/testing: The contract library (jpr_common_contract)

What contract tests need, without the fake, so the DLL that runs them in REAPER
doesn't link the fake, and a contract test that reaches for it
(`FakeReaper`, `GetProject()`) doesn't build there:
- `contract_test.h`, `FakeProject` and `FakeTrack`, `WriteProjectFile()`, and
  `RecordingSurface`.
- `reaper_action_list.h`: every action JPRSurf uses, with REAPER's text and
  default toggle state, which a contract test checks against REAPER, and the
  IDs tests use, named in `action_ids.h`.
- `jpr_common_testing` (the fake) depends on it. The contract tests
  (`jpr_common_contract_TESTS`) build into both `jpr_common_testing_test` and
  `reaper_jprsurf_check`.

### common/testing: ContractTest (contract_test.h)

- `OpenProject(build)` opens the project `build` fills in, in place of the one
  open, and forgets the calls opening it made. Each test opens its own, so
  nothing carries from one test to the next. `TakeCalls()` returns what the
  `RecordingSurface` got since the last time. `EndEntryPoint()` splits the
  test's calls, as one of REAPER's calls would end, so the fake's checks see
  each part on its own.
- **Two implementations,** chosen by what links it. `contract_test_fake.cc`
  gives each test its own `FakeReaper`, with `AddReaperActions()`, a
  `SurfaceNotifier`, and a `RecordingSurface` added, and writes the project
  file too, so a project REAPER couldn't open fails in `ctest`.
  `contract_test_reaper.cc` writes the project file and opens it with
  `Main_openProject("noprompt:...")`, after `CSurf_FlushUndo(true)`.
- **`RecordingSurface`** records each call as text, with tracks by name
  (`SetSurfaceMute(Drums, true)`), and route calls named (`SETSENDVOLUME`). It
  leaves out its runs, and the `Extended()` calls about state the fake doesn't
  hold (the mixer's scroll, input monitoring). Its type is registered as
  `RECORDING`.

### common/testing: Project files (project_file.h)

`WriteProjectFile(project)` writes a `FakeProject` as the text of an RPP file,
which opens in REAPER as the same project: the tracks in order, with their
folders, names, GUIDs, colors, volume and pan, mute and solo (in place too),
rec arm, selection (the master's too), automation mode, group, and visibility,
with input monitoring off and `PANMODE 3`; each send and hardware output; the
selected media items, as empty items; the cursor; the automation override; and
the master's visibility in the track panel (`MASTERTRACKVIEW`). It fails the
test, naming it, for state a project can't open with: playing, a peak, a dirty
flag, or undo points. `FakeProject::Create()` makes a project outside the fake,
for the REAPER side.

### common/testing: Running them in REAPER (reaper_jprsurf_check)

- **The DLL** loads the API from REAPER's `GetFunc`, and registers
  `RecordingSurface`'s type. With `JPRSURF_CHECK_DIR` set, the surface's first
  run runs every test (`RUN_ALL_TESTS()`), each finishing within the call, then
  opens an empty project. The next posts File: Quit REAPER to the main window,
  as quitting from inside a run destroys the surface while it runs. It writes
  `output.txt` (gtest's output), `result.txt`, `log.txt` (how far it got), and
  `project.rpp` (the last project opened).
- `Main_openProject()`, `GetMainHwnd()`, and `CSurf_FlushUndo()` are loaded by
  name in `contract_test_reaper.cc`, not put on the API list, as JPRSurf never
  calls them and the fake must never fake them.
- **`check_in_reaper`** (`check_in_reaper.cmake`, run with `cmake -P`) builds
  the DLL, copies it into the test install's `UserPlugins`, writes
  `csurf_0=RECORDING null` as its only control surface, runs REAPER with
  `-newinst` and a time limit (`JPR_REAPER_CHECK_TIMEOUT`), prints gtest's
  output, and fails if a test failed, REAPER didn't quit, or there is no
  result. `GTEST_FILTER` picks the tests. It runs in the main checkout only,
  one run at a time, and never touches the user's REAPER.

### common/testing: The fake's undo (fake_project.h)

- **An undo point holds the project's state,** as REAPER's does. Each track's
  undoable values are in `FakeTrackUndoValues`, a base of `FakeTrack`, and each
  route's in `FakeRouteUndoValues`, a base of `FakeRoute`, so a field is undone
  by where it is declared. A point copies those bases for every track and route
  (`UndoState`), and Undo or Redo copies them back. Tracks and routes are never
  freed, so a deleted one stays deleted.
- **It starts when the API first changes something** (`BeforeChange()`), so a
  test's own setup isn't undone. A test's direct changes after that are part of
  the next point.
- `AddUndoPoint()`, `Undo()`, `Redo()`, `GetUndoPoints()`, `GetUndoCount()`,
  and `GetRedo()`. `AddReaperActions()` gives Edit: Undo and Redo handlers, and
  the setters REAPER adds a point for add it (see REAPER facts).
  `CSurf_On*ChangeEx()` hold their point as REAPER does
  (`HoldSurfaceChange()`). `SetRedo()` is gone.
- The project itself has no undoable values yet. The first would go in a
  `FakeProjectUndoValues`, which `UndoState` would hold a copy of.

### common/testing: The rest of the fake, and SurfaceNotifier

Each gap REAPER showed is fixed in the fake or the notifier, with its test (see
REAPER facts): `GetTrackState()`'s flags, the master's name, color, and
visibility, solo in place, the setters' return values, selection ganging, how
grouping changes each property, the order of sends, ending a route edit, the
text formats, `NamedCommandLookup()` of a number, and undo. `SurfaceNotifier`
follows REAPER's calls, which wait for the refresh (see REAPER facts), and its
comments and "Seen in traces" in `testing_and_profiling.md` say so.

### common: Shared constants

REAPER's values that `Track`, the fake, and the contract tests each had their
own copy of are in one place: `GetTrackState()`'s flags, the setters' group
flags, and the route categories in `track_state.h`; `format_timestr_pos()`'s
modes in `timeline.h`; and `kEndEdit` in `fake_project.h`.

## Tests

| File                                | What                                                                                                                                         |
| ----------------------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `fake_reaper_contract_test.cc`      | Tracks, track changes (grouping, ganging, batches), selection, routes, the project, undo, text formats, and GUID text, through the API only  |
| `reaper_actions_contract_test.cc`   | Each action's text and toggle state, the automation mode actions, `NamedCommandLookup()`, and the ruler's time units                          |
| `surface_notifier_contract_test.cc` | What REAPER calls on a surface during each setter, in and out of a batch, for routes, groups, the override, actions, and Undo and Redo      |
| `project_file_test.cc`              | Each field's text, and the state a project can't open with failing the test                                                                 |
| `recording_surface_test.cc`         | Only one exists at a time, and its run can be replaced from inside a run                                                                    |
| `fake_reaper_test.cc`               | What needs the fake: its checks, `FakeProject`'s own methods, undo points as the fake lists them, and what it doesn't model failing the test |
| `surface_notifier_test.cc`          | What needs the fake: changes in REAPER's own UI, and runs forgetting a refresh                                                               |

## Coverage

Each function on the API list is checked by a contract test, or is listed here
as fake only, with why, in the order of `JPR_REAPER_API`. Contract tests are
named by their fixture, without `ContractTest`: `ReaperActions` and `RulerMode`
are in `reaper_actions_contract_test.cc`, those starting `SurfaceNotifier` in
`surface_notifier_contract_test.cc`, and the rest in
`fake_reaper_contract_test.cc`. "and most others" is for a function nearly
every test reads. A function added to the API list gets a row.

| Function                      | Contract tests                                           | Fake only, because                                                |
| ----------------------------- | -------------------------------------------------------- | ----------------------------------------------------------------- |
| `AnyTrackSolo`                | TrackChange, SurfaceNotifier                             |                                                                   |
| `CountSelectedMediaItems`     | Project                                                  |                                                                   |
| `CountSelectedTracks`         | Selection                                                |                                                                   |
| `CountSelectedTracks2`        | Selection                                                |                                                                   |
| `CountTracks`                 | Track                                                    |                                                                   |
| `CreateMIDIInput`             |                                                          | The machine's ports                                               |
| `CreateMIDIOutput`            |                                                          | The machine's ports                                               |
| `CSurf_OnPanChangeEx`         | TrackChange, Undo, SurfaceNotifier                       |                                                                   |
| `CSurf_OnVolumeChangeEx`      | TrackChange, Undo, SurfaceNotifier                       |                                                                   |
| `format_timestr_pos`          | Text                                                     |                                                                   |
| `GetCursorPosition`           | Project                                                  | Moving it: tests set it, as REAPER's UI moves it                  |
| `GetGlobalAutomationOverride` | Project, Undo                                            |                                                                   |
| `GetMasterTrack`              | Track, and most others                                   |                                                                   |
| `GetMediaTrackInfo_Value`     | Track, ReaperActions, Undo                               |                                                                   |
| `GetMIDIInputName`            |                                                          | The machine's ports                                               |
| `GetMIDIOutputName`           |                                                          | The machine's ports                                               |
| `GetNumMIDIInputs`            |                                                          | The machine's ports                                               |
| `GetNumMIDIOutputs`           |                                                          | The machine's ports                                               |
| `GetParentTrack`              | Track                                                    |                                                                   |
| `GetPlayPosition`             | Project                                                  | Playing: tests set it, as the transport actions have no handlers  |
| `GetPlayState`                | Project                                                  | Playing: tests set it, as the transport actions have no handlers  |
| `GetSelectedTrack`            | Selection                                                |                                                                   |
| `GetSelectedTrack2`           | Selection                                                |                                                                   |
| `GetSetMediaTrackInfo_String` | Track, Undo                                              |                                                                   |
| `GetSetTrackSendInfo`         | Route                                                    |                                                                   |
| `GetToggleCommandState`       | ReaperActions, RulerMode                                 |                                                                   |
| `GetTrack`                    | Track, and most others                                   |                                                                   |
| `GetTrackColor`               | Track                                                    |                                                                   |
| `GetTrackGUID`                | Track                                                    | Its pointer lasting as tracks are added, which the API can't show |
| `GetTrackNumSends`            | Route                                                    |                                                                   |
| `GetTrackReceiveUIMute`       | Route                                                    |                                                                   |
| `GetTrackReceiveUIVolPan`     | Route                                                    |                                                                   |
| `GetTrackSendUIMute`          | Route                                                    |                                                                   |
| `GetTrackSendUIVolPan`        | Route, Undo                                              |                                                                   |
| `GetTrackState`               | Track, and most others                                   |                                                                   |
| `GetTrackUIVolPan`            | Track, and most others                                   |                                                                   |
| `guidToString`                | Track, Guid                                              |                                                                   |
| `IsProjectDirty`              | Project, Undo                                            |                                                                   |
| `kbd_getTextFromCmd`          | ReaperActions, RulerMode                                 |                                                                   |
| `Main_OnCommand`              | ReaperActions, RulerMode, Undo, SurfaceNotifier          |                                                                   |
| `mkpanstr`                    | Text                                                     |                                                                   |
| `mkvolstr`                    | Text                                                     |                                                                   |
| `NamedCommandLookup`          | ReaperActions                                            |                                                                   |
| `PreventUIRefresh`            | TrackChange, SurfaceNotifier                             |                                                                   |
| `SetGlobalAutomationOverride` | Project, Undo, SurfaceNotifier                           |                                                                   |
| `SetOnlyTrackSelected`        | Selection, SurfaceNotifier                               |                                                                   |
| `SetTrackSelected`            | Selection, TrackChange, Undo, SurfaceNotifier            |                                                                   |
| `SetTrackSendUIPan`           | Route, Undo, SurfaceNotifierRoute                        |                                                                   |
| `SetTrackSendUIVol`           | Route, Undo, SurfaceNotifierRoute                        |                                                                   |
| `SetTrackUIMute`              | TrackChange, Undo, SurfaceNotifier                       |                                                                   |
| `SetTrackUIRecArm`            | TrackChange, Undo, SurfaceNotifier, SurfaceNotifierGroup |                                                                   |
| `SetTrackUISolo`              | TrackChange, Undo, SurfaceNotifier                       |                                                                   |
| `ShowConsoleMsg`              |                                                          | Opens a window                                                    |
| `stringToGuid`                | Guid                                                     |                                                                   |
| `time_precise`                |                                                          | The real clock                                                    |
| `ToggleTrackSendUIMute`       | Route, Undo, SurfaceNotifierRoute                        |                                                                   |
| `Track_GetPeakInfo`           |                                                          | Peaks need audio playing, and a project opens stopped             |
| `Undo_CanRedo2`               | Project, Undo                                            |                                                                   |
| `Undo_OnStateChangeEx`        | Undo                                                     |                                                                   |

What the API can't reach stays fake only too: changes made in REAPER's own UI,
which tests make by setting the fake, and what REAPER calls on a surface
between runs (see "Seen in traces" in `testing_and_profiling.md`).

## REAPER facts

Checked in the test install (2026-10-08 to 2026-10-09), and held by the fake and
the notifier.

**The test install:**
- Registered with the user's license and set to the dummy audio device, it
  starts with no dialogs, and with `-newinst` runs beside the user's REAPER.
  The transport plays and stops on the dummy device.
- REAPER only creates a control surface whose line in `reaper.ini` has a
  config string after its type.
- `Main_openProject("noprompt:...")` from inside a run finishes within the call,
  in about 0.55 seconds. A project closed while REAPER holds a surface change's
  undo point open (see Undo) crashes REAPER after the run, so the DLL calls
  `CSurf_FlushUndo(true)` before opening each project.
- Opening an empty project with `noprompt:`, then posting File: Quit REAPER,
  exits with no prompt. Quitting from inside a run crashes REAPER.

**What REAPER calls on a surface:**
- **A track's mute and solo wait for the refresh:** the end of the outermost
  batch, or outside a batch, before the next run, when REAPER sends those that
  differ from what it last sent. Until then, every track's state leaves them
  out. Outside a batch, a mute or solo sends only the master's solo during the
  call.
- **Rec arm** sends the track's change around every track's state outside a
  batch, and in a batch sends each changed track's change in every track's
  state at its end. A rec arm that changes nothing sends nothing. One that
  ganging or grouping could take to other tracks is sent as a change to every
  track.
- **The automation mode actions** resend only if a selected track's mode
  changed.
- **Undo and Redo** with nothing to undo or redo call nothing.
- **Send volume and pan changes notify:** `Extended(CSURF_EXT_SETSENDVOLUME)`
  and `SETSENDPAN` for the source track, and `SETRECVVOLUME` and `SETRECVPAN`
  for the destination. Mute sends nothing. Ending an edit sends nothing.
- **`Extended(CSURF_EXT_SETPAN_EX)`** sends the track's pan mode: `PANMODE 3`
  in the project file sets the tracks' mode, and `MASTER_PANMODE 3` the
  master's.

**Tracks:**
- **`GetTrackState()`** sets &1 for a folder, &16 for solo (&16 and &32 in
  place), &128 for input monitoring (which REAPER's new tracks have), and &512
  and &1024 for a track hidden in the track panel or the mixer.
- **The master** is shown in the track panel by the project
  (`MASTERTRACKVIEW`'s first field), which View: Toggle master track visible
  (40075) toggles, and whose toggle state it is. Hidden, it has &512, though
  its `B_SHOWINTCP` reads 1 either way. Its `GetTrackColor()` reads 0 even with
  a color. `P_NAME` can't read or set its name, and `GetTrackState()` names it
  `MASTER`. It can't be rec armed (`SetTrackUIRecArm()` returns -1).
- **Solo** is in place by default: `SetTrackUISolo()` with 1, or a toggle, sets
  &16 and &32 and returns 2. 2 solos not in place and returns 1, and 4 solos in
  place. The master's solo returns 2 but sets only &16, and `AnyTrackSolo()`
  leaves it out.
- **Selection ganging:** with the setters' group flags without `&2`, a change to
  a selected track changes every selected track, the master too, and then
  grouping changes each of their groups, but a group's selected tracks aren't
  ganged in turn. `CSurf_On*ChangeEx()`'s `allowGang` allows both.
- **Grouped and ganged changes:** mute and solo set the other tracks to the
  track's new value, even when the track's didn't change. Rec arm changes
  nothing if the track's doesn't change; ganged tracks are set to its new
  value, but grouped tracks are toggled. Volume and pan move the other tracks
  by the same change, volume by its ratio and pan by its difference, measured
  from where each was when the gesture began, so a track clamped at an end of
  pan, or a volume through -inf, comes back (the fake fails the test there). A
  track's pan past an end is clamped.

**Routes:**
- **A track's sends are in the order of the tracks they go to,** then of their
  receives there, as REAPER keeps each route at its destination (`AUXRECV`).
- **Ending an edit** (`SetTrackSendUIVol()` or `SetTrackSendUIPan()` with
  isend 1) sets nothing. An instant edit (-1) sets the value.
- The getters index receives below zero, as the setters are documented to. A
  hardware output's `P_SRCTRACK` is its track, and its `P_DESTTRACK` null. A
  send's pan past an end isn't clamped.

**Actions and text:**
- **The ruler's time unit actions** have text (`View: Time unit for ruler:
  Seconds`). Which of each group is on is a preference: REAPER's default
  secondary unit is Minutes:Seconds. Running the mode that is on leaves it on.
- **`mkvolstr()`** writes 0dB as `0.00dB`, and just under or over it as
  `-0.00dB` or `+0.00dB`. It writes one decimal from 10dB up (`-140.0dB`).
  Below 2^-25 (about -150.5dB) it writes `-inf dB`.
- **`mkpanstr()`** writes `center` only for exactly 0. Otherwise it truncates
  the percent (`12%R` for 0.125), or under 1% writes it to a tenth (`0.4%R`),
  unless that is 0 (`0%R`).
- **`format_timestr_pos()`** truncates the time mode's milliseconds and the
  frames. Before the start, beats count measures down from 0 (`0.4.50` is half
  a beat before), and frames count hours down from 0 (`-1:59:59:28`). Beats'
  hundredths and samples are rounded.
- **`NamedCommandLookup()`** reads a name that doesn't start with `_` as a
  number, as far as its digits go (`40029x` is 40029).
- An action REAPER doesn't have has no text and no toggle. A project opens
  stopped, with the play position at 0 rather than the cursor, clean, and with
  nothing to redo. The automation override is the project's.

**Undo:**
- **An undo point holds the project's state,** not a change.
  `Undo_OnStateChangeEx()` adds one holding every change since the last point,
  and none if nothing changed, or if its flags don't have
  `UNDO_STATE_TRACKCFG`. Edit: Undo restores the state of the point before, and
  Edit: Redo the state of the point after, each dropping any change since the
  last point. A new point drops the redo. Each point sets the dirty flag.
- **What is undone:** each track's name, volume, pan, mute, solo, rec arm,
  automation mode, color, group, and visibility, the master's too, and each
  route's volume, pan, and mute. Selection and the automation override aren't.
  (Color, group, and visibility were checked in REAPER's UI, as no function on
  the API list changes them.)
- **Which setters add their own point:** the track setters, selection, names,
  the override, and the route setters with isend 0 add none.
  `ToggleTrackSendUIMute()` adds `Toggle send mute`, and `SetTrackSendUIVol()`
  and `SetTrackSendUIPan()` add `Adjust send volume` and `Adjust send pan` for
  an instant edit (-1), or one that ends an edit (1), from either end of the
  route. The automation mode actions add `Change track envelope automation
  mode` if a mode changed, and Track: Unsolo all tracks adds `Clear all track
  solos` if a track was soloed.
- **`CSurf_On*ChangeEx()` hold their undo point:** REAPER adds it, as `Adjust
  track volume (via surface)` or `Adjust track pan (via surface)`, once a
  change of the other kind is made, so it holds that change too. Changes of the
  same kind, to any track, add nothing. Other points, and Undo, leave it held.
  REAPER also adds it between runs.

**What the fake leaves out**, as no test needs it yet: the held surface point
REAPER adds between runs; the point a volume change from a surface adds on a
track in touch mode (`Create volume envelope: ...`), as the fake has no
envelopes; the points changes in REAPER's own UI add (`ClickMute()` and the
like); and a handler for Track: Unsolo all tracks. `GetTrackGUID()` keeping the
same pointer as tracks are added is fake only (see Coverage).

## Building blocks

- **A contract test** derives from `ContractTest` (or a fixture of its own that
  does), opens its project with `OpenProject()`, filled in as a `FakeProject`,
  and checks only what REAPER's API returns and `TakeCalls()`. It goes in the
  `*_contract_test.cc` beside the code it checks, which is in
  `jpr_common_contract_TESTS`. A test that needs the fake stays in the plain
  `*_test.cc`.
- **Exploring REAPER:** a temporary contract test writes what REAPER returns
  into a string and expects it to be empty, so `check_in_reaper` (with
  `GTEST_FILTER`) prints it. The finding then becomes a real contract test the
  fake passes, and the exploration goes.
- **A field the project file can't write** fails the test, naming it, so a new
  `FakeProject` or `FakeTrack` field is either written or refused.
- **An undoable value** of a track or route goes in `FakeTrackUndoValues` or
  `FakeRouteUndoValues`, and is undone with no other change.
- **A value REAPER defines** (a flag, a mode, an action ID) is named once, in
  `common` or the contract library, for the plugin, the fake, and the tests.

## Decisions

- **One project builder for both,** written as an RPP file, rather than built
  with REAPER's own calls, which would put more functions on the API list, each
  needing a fake and a contract test. Its cost is a format REAPER doesn't
  document, which every contract test checks by reading its fields back.
- **The contract tests finish within one run,** so what REAPER calls between
  runs is left to traces.
- **The fake models undo** as snapshots of the undoable values, as REAPER's undo
  was simple enough once explored, rather than leaving undo a check by hand.
- **`check_in_reaper` is a build target,** not a `ctest` test, as a plain
  `ctest` can't skip a test by label, and it runs REAPER, so it is only run on
  purpose.

## Performance

Nothing on the realtime path changes; the one change for the user, the route
undo fix, reorders two calls. `check_in_reaper` isn't for performance: the
tests' calls aren't JPRSurf's, and the DLL has no profiler.
