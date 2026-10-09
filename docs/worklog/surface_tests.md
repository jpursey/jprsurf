# Surface tests

The smoke test as unit tests. The plugin is loaded into the fake REAPER exactly
as REAPER loads it, a fake X-Touch and extender are connected, and the tests
press buttons, move faders, and turn pots, then check what REAPER was asked to
do and what the hardware shows. They cover everything the default config does:
every step of the smoke scenario in `profiles/README.md`, and what it never
reached (paging routes, the Info strip, deleted and hidden tracks, removing the
surface). CLAUDE.md's smoke test is now only what the fakes can't show, and
each feature adds surface tests of its own behavior. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Running the plugin,
Surface harness, Tests).

## Behavior

The tests found three things the surface showed wrong, all text sent to an
empty strip's scribble strip, which are fixed:
- **An empty track strip's** bottom line showed "-inf dB", the stub track's
  volume.
- **An empty route strip's** bottom line showed "-inf dB" too.
- **The Info strip,** once its track was deleted, still showed "Send".

Each mapping now only acts while its track or route exists (a condition on
`TrackProperties::kTrackExists` or `RouteProperties::kExists`), so the line has
no writer, and is cleared. An empty strip is black, and a black scribble strip
shows no text, so none of them could be seen on the hardware. The fixes stop
the surface sending text nobody can read, and show if a config ever colors
empty strips.

Nothing else changes for the user. The plugin's entry point moves to
`dll_main.cc`, which is the only code that reads the environment (whether to
trace, and where the profile goes), so no test can write the user's trace or
profile.

## Names

| Name                | What                                                         | Might be confused with                         |
| ------------------- | ------------------------------------------------------------ | ---------------------------------------------- |
| `jpr_plugin`        | The plugin as a static library, which tests link             | `reaper_jprsurf`, now just the DLL             |
| `Plugin::Options`   | The trace and profile paths `dll_main.cc` found              | `ControlSurface::Type`; the config string      |
| `SurfaceNotifier`   | Makes REAPER's calls back to the surface                     | `ControlSurfaceListener`, which hears them     |
| `SurfaceTest`       | The fixture for any config: fake, plugin, and surface        | `TestControlSurface`, which it holds           |
| `DefaultConfigTest` | The fixture for `PluginSurface`'s config, with its X-Touches | `SurfaceTest`, which it derives from           |

## Structure

### plugin: The plugin, split (plugin.h, dll_main.cc)

- `jpr_plugin` is a static library with `plugin.cc` and `plugin_surface.cc`.
  `reaper_jprsurf` is the DLL: `dll_main.cc`, with `DllMain` and
  `REAPER_PLUGIN_ENTRYPOINT` (a static library would drop an export nothing
  references). Deployment stays on the DLL.
- `dll_main.cc` reads `JPRSURF_TRACE` and finds the log directory, and passes
  them to `Plugin::Load(hinstance, info, options)` in `Plugin::Options`.
  `PluginSurface::Register()` takes the profile path rather than finding it.
  Empty paths turn each off, as tests load it.
- `Plugin`'s instance, and the trace it owns, have a `TestReset`, so a test
  that fails before unloading doesn't leave it for the next.
- The fake's plugin info `GetFunc` returns each function as it is loaded at the
  time, so `Plugin::Load()` loading the API again keeps hooks already over the
  fake, such as a `SurfaceNotifier`'s.

### common/testing: What REAPER calls back (surface_notifier.h)

The fake never calls the surface by itself, and for the whole plugin there are
too many calls to make by hand, at the right moment. A `SurfaceNotifier`, for
as long as it exists, hooks the fake's functions with `gb::FunctionHook`, and
makes the calls REAPER makes from inside them, as traces showed (see "Seen in
traces" in the design doc, and REAPER facts below):

| JPRSurf calls                       | REAPER calls back                                  |
| ----------------------------------- | -------------------------------------------------- |
| `SetOnlyTrackSelected`              | `SetSurfaceSelected()` for each track that changed |
| `SetTrackSelected`                  | `SetSurfaceSelected()` for the track               |
| `SetTrackUIMute`, `SetTrackUISolo`  | The track's mute and solo, and the master's solo   |
| `SetTrackUIRecArm`                  | `SetTrackListChange()`, then every track's state   |
| `CSurf_OnVolumeChangeEx`            | The last touched track (`Extended()`)              |
| `CSurf_OnPanChangeEx`               | The last touched track (`Extended()`)              |
| `SetGlobalAutomationOverride`       | Every track's volume, pan, and selection           |
| Automation modes (40400-40404, 42023) | `SetAutoMode()`, then as the override does       |
| Undo and redo (40029, 40030)        | Everything                                         |

- **When:** a track setter's calls come at the end of its batch
  (`PreventUIRefresh(-1)`), or in the call when there is none. The rest come in
  the call, after the action's handler has made its change. Every track's
  state is master first, then each track in order.
- **Everything seen,** not just what `ControlSurface` acts on, so a surface
  that starts listening to another call is already tested against what REAPER
  sends.
- **Nested calls** are made on the surface the `TestControlSurface` wraps
  (`TestControlSurface::GetWrapped()`), so they are part of the entry point that
  called the function, and the fake's checks still see one entry point per
  call REAPER makes.
- **Changes made in REAPER's own UI:** `ClickTrack()`, `CtrlClickTrack()`, and
  `ClickMute()` set the fake and make the calls traces showed. They come
  between runs, so they are made on the `TestControlSurface`
  (`FakeReaper::GetSurface()`), each an entry point of its own. A change no
  trace has shown (one to the master, a Ctrl+click that unselects, or a mute of
  one of several selected tracks), or one made inside a batch, fails the test.
- **Order of hooks:** the notifier is created before the plugin loads and
  destroyed after it unloads, so its hooks are under the profiler's and the
  trace's, as REAPER's own functions are.
- Its tests act only through REAPER's API, on a surface that records every call
  it gets, so they can later run inside REAPER (*Check the fakes in REAPER*).

**Brittleness:** the notifier knows only what traces showed. A function JPRSurf
starts calling that makes REAPER call back needs a new trace and a new row, or
the tests silently get no callback. "Seen in traces" is the place to check
when a function is added to the API list.

### common/testing: The rest of the fake

- **REAPER's actions** (`reaper_actions.h`): `AddReaperActions()` adds every
  action JPRSurf uses, with the text and toggle state REAPER reported. The
  automation mode actions have a handler that sets the selected tracks' modes
  (the master's too), and the ruler's unit actions are radio groups, so the
  timecode button steps through them. `FakeReaper::SetCommandHandler()` gives
  an action already added a handler, such as Undo.
- **FakeProject:** `GetSelectedTracks(include_master)`, `FindTrackByName()`,
  and `ShowInMixer(track, shown)`, which shows or hides a track as the user
  does, setting every track in its folder, at every depth, the same.
- **FakeReaper::GetSurface()** returns the `TestControlSurface`, whose calls are
  entry points of their own.

### common, device: Times tests wait for

`ControlSurface::kVisibilityInterval` (how often visibility is polled) and
`Control::kLongPressDurationSecs` and `kDoublePressWindowSecs` are public, so
the fixtures wait for exactly them, and follow any change.

### plugin/testing: SurfaceTest (surface_test.h)

A gtest fixture in the test-only `jpr_plugin_testing` library, for any config:
the fake, a `SurfaceNotifier`, and the surface.
- `AddSurface()` loads the plugin and adds the surface, as REAPER does at
  startup, then calls `SetTrackListChange()`, as REAPER does when a project
  loads, and runs until it is shown. `RemoveSurface()` removes it and unloads
  the plugin, as REAPER does at exit; `TearDown()` calls it, before a derived
  fixture's fake devices go.
- `RunUntilShown()` is two runs: each run reads the project before it acts, so
  what a run changes on other tracks, or sends to a fader, is shown on the
  next.
- `AddTracks(count, folder)` names each track for where it is (T2, T2.1).
- Presses and moves that get the clock right: `Tap()`, `DoublePress()`,
  `LongPress()`, `Hold()`, and `MoveFader()`. Each settles for the double press
  window, in case a press is held back as the first of two, and then until it
  is shown.
- Anything logged at `ERROR` or above fails the test, from the fixture's
  construction to its destruction (Game Bits' `gb::LogErrorGuard`), unless the
  test takes it from `log_error_guard_`.

### plugin/default_config: DefaultConfigTest (default_config_test.h)

The tests of `PluginSurface`'s one config, in a test-only folder of their own:
when configs come from files, each will have its own, and a config's mappings
are data. `DefaultConfigTest` connects an X-Touch with an extender to its left,
or the X-Touch alone.
- Strips are numbered across the surface (0-7 on the extender, 8-15 on the
  X-Touch): `GetXTouch()`, `GetXTouchStrip()`, `GetName()`, `GetBottomLine()`,
  and `GetLight()`.
- How the X-Touch shows 0dB and a pan: `kFader0dB`, `kPanRing`, and the ring
  positions.
- Gestures its tests share: `TapSelect()`, `SelectRange()`, and
  `EnterSendMode()`.

## Tests

| File                                  | What                                                                                                                                                                  |
| ------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `surface_notifier_test.cc`            | Each row of the notifier's table, the batch rule, and each change in REAPER's own UI, with the ones no trace has shown failing                                        |
| `plugin_test.cc`                      | Loading registers the surface type, and adding one opens the X-Touch's ports; loading twice, a version mismatch, and no `GetFunc` fail; the paths in `Options`        |
| `surface_test_test.cc`                | The error guard, and `AddTracks()`' names and folders                                                                                                                 |
| `default_config_test_test.cc`         | The X-Touches' order, the X-Touch alone, each press helper, and removing the surface clearing the hardware                                                            |
| `track_strip_test.cc`                 | Faders, pots, pot buttons, mute, solo, and rec arm each way; meters; scribble names, volumes, and colors; folder and empty strips; the master fader; ranges           |
| `track_list_test.cc`                  | Select's press, double press, long press, and range; Global; Bank and Channel at the ends; following the current track; a deleted and a hidden folder; X-Touch alone  |
| `send_mode_test.cc`                   | Entering and leaving, and the lights; route strips; paging; the Info strip; sends and receives; walking routes; undo points; following REAPER; a deleted track        |
| `global_test.cc`                      | Transport, Rewind and Forward by measure, beat, and marker; timecode and ruler modes; utility buttons; modifiers; automation modes and the override, with lights      |

Each file builds the least project that shows its behavior, and acts as the
user does: through the fake X-Touches, REAPER's API, or the notifier's UI
changes. What the surface polls (track mute, solo, volume, pan, color, and
selection, toggle states, the override, the cursor, routes) a test may set on
the fake directly. What it caches until REAPER calls back (the selected tracks'
automation modes) a test changes through the surface or REAPER's API.

A bug a test finds is fixed in its own follow-up CL, with the test disabled
until then. Modifiers on track controls, grouping, and the detail of ranges are
left to *Scene tests*.

## REAPER facts

Checked in REAPER, and held by the fake and the notifier:
- **The 2026-09-29 smoke trace:** selection notifies each track whose
  selection changed (`SetOnlyTrackSelected()` during the call,
  `SetTrackSelected()` at the end of the batch); solo notifies as mute does;
  rec arm calls `SetTrackListChange()` and resends every track's state; sends
  notify nothing; the automation mode actions call `SetAutoMode()`, then resend
  every track's volume, pan, and selection, as `SetGlobalAutomationOverride()`
  does.
- **Clicking a track** in the track panel (2026-10-05) calls
  `SetSurfaceSelected()` for each track whose selection changed, in track order,
  then `OnTrackSelection()` and `Extended(CSURF_EXT_SETLASTTOUCHEDTRACK)` with
  the track clicked. A Ctrl+click adding a track calls the same, with only its
  `SetSurfaceSelected()`.
- **The override** set in REAPER's UI calls what
  `SetGlobalAutomationOverride()` does, and no `SetAutoMode()`.
- **Hiding or showing a folder** in the mixer or track control panel
  (2026-10-05) sets every track in it, at every depth, the same, whatever each
  was before, so a mix of shown and hidden tracks isn't brought back. Each
  track's own `B_SHOWINMIXER` changes, which the surface already follows.
- **The fake doesn't undo.** REAPER's undo restores project state JPRSurf never
  sees, so the tests check JPRSurf's part (its undo points, Undo and Redo, the
  Undo light, and following the state an undo leaves), and what undo restores
  stays a check in REAPER until *Check the fakes in REAPER*.

## Building blocks

- **A surface test** derives a fixture from `DefaultConfigTest` (or, for
  another config, from `SurfaceTest`), builds its project, calls
  `AddSurface()`, and uses the press helpers and `RunUntilShown()`, never a
  hand-counted number of runs.
- **A delay** a test waits for is a public constant of the code that has it
  (`ContinuousUndo::kDelay`, `ControlSurface::kVisibilityInterval`), plus a run.
- **A change in REAPER's own UI** is a notifier function, made only for what a
  trace has shown, with its own test.
- **An empty strip shows nothing** by a condition on `exists` on each mapping
  that would still show a value, as `config_model.md` describes.

## Decisions

- **A run of latency stays.** Everything the scene changes on a fader, or on
  tracks other than the one acted on, reaches the X-Touch a run (1/30s) later.
  It is never noticeable, a motor fader is slower anyway, and removing it would
  give `Control` two paths for an output, so `RunUntilShown()` is two runs.

## Performance

Nothing on the realtime path changes. The smoke profile at the end of the
feature matched `profiles/smoke.txt`: per run, counts per action, and the
profiler's cost.
