# Backlog

Ideas for future work, collected from the feature worklogs in `docs/worklog/`
and from using the surface. The worklogs describe what was built; this is what
has not been.

The list is a rough stack rank: the order the items look worth doing, as a best
guess rather than a commitment. Re-order it freely. The items near the bottom
are either large and open ended, or only worth doing if a problem actually
turns up.

Each item carries:

- **Layers**: the JPRSurf libraries it touches (`common`, `device`, `scene`,
  `plugin`), in dependency order. More than one usually means more than one CL.
- **Size**: a guess. *Small* is a single CL. *Medium* is a few. *Large* is
  many, usually after a design.
- **Feature workflow**: whether the item follows the feature workflow, with a
  design and a `docs/worklog/` plan of CLs. Anything that comes down to one or
  two simple CLs doesn't, and is done as an ordinary change.
- **Depends on**: other items that should come first, or "nothing". Items in
  another project's backlog are named with the project, such as Game Bits
  *Function hooks*.
- **Background**: the worklog holding the context, where there is one.

When an item that follows the feature workflow is picked up, it moves into its
own `docs/worklog/<feature>.md` plan and comes out of this list. Any other item
comes out of this list in the commit that does it. The workflow (imported by
CLAUDE.md) has the rest.

## Scene tests

- **Layers:** scene
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Fake X-Touch and device tests*
- **Background:** [testing_and_profiling.md](testing_and_profiling.md) (Tests)

Tests of `scene` against the fake and fake X-Touches, for the detail surface
tests don't reach: properties against REAPER's state (track, route, state,
command, and timeline properties), views (conditions, subjects, lists,
references, and repeated views), mappings (modifiers, taps, and picks), and
`TrackActions` (ranges, anchors, grouping, and batching). The fake's reset of
process state extends to `scene`'s globals, such as `g_last_auto_override` in
`state_properties.cc`.

## Check the fakes in REAPER

- **Layers:** common
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Surface tests*
- **Background:** [surface_tests.md](worklog/surface_tests.md) (What REAPER
  calls back), [testing_and_profiling.md](testing_and_profiling.md) (Tests)

Surface tests trust two fakes: the fake REAPER's model of REAPER's state, and
`SurfaceNotifier`'s model of what REAPER calls on the surface. This checks
both against REAPER, with contract tests: tests that act only through REAPER's
API, so they run unchanged against the fake (in `ctest`) and inside REAPER.
The fake loads itself as REAPER's API, so only building the project a test
starts from differs: `FakeProject` under the fake, an RPP file in REAPER. A
test that passes under the fake but fails in REAPER is a gap in the fake, which
is fixed, with a test, as Checking in REAPER in CLAUDE.md says.

So the surface is tested in three separate parts:
- **The surface's behavior**, and its call counts: surface tests and *REAPER
  call count tests*, under the fake only.
- **The fake REAPER:** what each function on the API list does to REAPER's
  state, as the fake models it (setters read back, GUIDs, route indexing,
  text formats, selection), and the effects of the actions the surface tests
  give handlers (the ruler modes, the automation mode actions). What Undo
  restores settles whether the fake should model undo.
- **`SurfaceNotifier`:** what REAPER calls on a surface during and after each
  function. Its own tests are already contract tests
  (`surface_notifier_test.cc`, from *Surface tests*).

Every behavior the fake models, and every call the notifier makes, has a
contract test. The API list is finite, so a review can check it.

Running them in REAPER:
- **Isolation:** a portable REAPER install, only for tests, with its own
  `UserPlugins` (the test DLL, and not the plugin) and `reaper.ini`, which adds
  the test DLL's surface. The user's setup is untouched.
- **The test DLL** registers a surface that records every call it gets, and
  runs the tests from inside one of its `Run()`s, once the project has loaded.
  Each test finishes within the call, as it does under the fake, so gtest runs
  them unchanged. A check of what REAPER calls later, between runs, needs a
  test across runs, and is left out unless one is needed.
- **Ending:** gtest writes its results to a file, and the DLL quits REAPER
  (File: Quit REAPER, 40004). A script launches REAPER with the project,
  waits, and reads the results, as a ctest test with a label a plain `ctest`
  skips.
- **To confirm:** how to quit without REAPER asking to save the changed
  project, and how each test starts from the same project (undoing its changes,
  or reverting to the saved project, within the run).

It isn't for performance. The tests' own calls go through the hooks the
profiler times, so a profile of them isn't the user's. Performance stays with
profiles of real sessions, and *REAPER call count tests* under the fake.

**Risks**, accepted: contract tests only check what someone thought to check,
where running whole surface tests in REAPER would also find what nobody
expected; the test install's preferences aren't the user's; the fake X-Touch
against the hardware stays a check by hand; and what REAPER calls while
creating the surface and loading the project is only seen in traces.

It needs REAPER, so it runs in the main checkout, never a side session.

## REAPER call count tests

- **Layers:** plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Profiler*, *Surface tests*
- **Background:** [testing_and_profiling.md](testing_and_profiling.md) (Tests)

Performance checks that are deterministic. JPRSurf controls only two things
about its cost: which calls it makes to REAPER and how many, and its own code.
This is the first. What a surface costs depends on its config, so these are
tests of JPRSurf's own surface, in `plugin/default_config` beside its surface
tests. A generated project (100+ tracks, with sends and receives) runs in the
surface harness, with the profiler over the fake:
- **Steady state:** an idle run makes no expensive calls, and its other calls
  are bounded: what the surface polls, and nothing more.
- **Each gesture** makes the fewest expensive calls that do what it asks. It
  pays one UI refresh at most, however many tracks it changes (one setter, or
  one `PreventUIRefresh()` batch), adds one undo point at most, and makes no
  call twice (the same setter on the same track). These hold for every gesture
  the surface tests make, so a new behavior that misses a batch, or repeats a
  call, fails without a bound of its own.
- **Bounds** on the calls in each gesture, and in a track list refresh, a bank
  change, and a mode change. Each starts at the count when the test is
  written, so a regression fails, and an improvement lowers it in the same
  change. A feature that adds a gesture adds its bounds, as it adds its surface
  tests.

The calls are weighed by cost, in a few coarse categories, so a bound says
what matters ("two UI refreshes") rather than a raw count. A checked-in table
gives each function on the API list its category, from the per-call times in
the `profiles/` snapshots of a real project, until *Measure REAPER's costs*
measures them:
- **Cheap reads**, such as `GetTrackState()` and `CountTracks()`.
- **Moderate**, such as text formatting and route info.
- **Expensive:** setters that refresh REAPER's UI (about 2-17ms each), the end
  of a `PreventUIRefresh()` batch, and undo points. Their category carries the
  work REAPER puts off until later, which no call's time shows.
- **`Main_OnCommand()`**, whose cost is the action's, by action.

A call whose cost grows with the project is weighed at the generated project's
size. The table is refreshed occasionally, from a new snapshot, rather than
with each change.

With *Benchmarks under the fake*, this replaces the idle profile in REAPER that
CLAUDE.md's Performance section asks of a CL that changes per-run work: call
count bounds in `ctest` for every change, a benchmark when a change touches
JPRSurf's own per-run work, and profiles in REAPER only occasionally, and at
the end of a feature, with the log's "Slow run" warnings from the user's test.
CLAUDE.md changes when both are done. *Measure REAPER's costs* then retires the
smoke profile.

## Batch each run

- **Layers:** common
- **Size:** small
- **Feature workflow:** yes
- **Depends on:** *REAPER call count tests*
- **Background:** [testing_and_profiling.md](testing_and_profiling.md) (Seen
  in traces)

Every track setter already goes through a `TrackBatch`, even for one track.
Wrapping each run in one `PreventUIRefresh()` pair, in `ControlSurface`, would
also batch what isn't batched today: `CSurf_OnVolumeChangeEx()` and
`CSurf_OnPanChangeEx()`, the send setters, and `Main_OnCommand()`, if they
refresh REAPER's UI, and more than one gesture in a run, such as two faders
moved at once. A new behavior would then need no rule to remember. It is a
plain pair, not a `TrackBatch`, which also makes its gesture's undo point, so
undo points stay one a gesture. The call count tests show what it saves.

To confirm, in REAPER:
- **What an empty pair costs.** It runs every run, idle or not. If REAPER
  refreshes whenever the count goes back to zero, even with nothing changed,
  the batch opens at the run's first write instead.
- **Callbacks come later.** REAPER calls a batched setter's callbacks (such
  as `SetSurfaceSelected()`) at the end of the batch, so they would all come
  after the run's work, rather than after each gesture. `TrackBatch` keeps the
  cache in line with JPRSurf's own changes, but what only a callback refreshes
  (today, the selected tracks' automation modes) would show a run later. The
  fake already defers these callbacks, so the surface tests show the effect.
- **Actions inside a batch:** whether every action JPRSurf runs behaves the
  same inside `PreventUIRefresh()`, such as Undo (which resends everything),
  the ruler modes, and anything that scrolls or redraws.

## Benchmarks under the fake

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** *REAPER call count tests*
- **Background:** [testing_and_profiling.md](testing_and_profiling.md)
  (Profiler)

The second part of JPRSurf's cost: its own code, without REAPER. The surface
tests' scenarios (idle runs, the smoke scenario's actions) run under the fake
with the profiler on, and report JPRSurf's self time per run and per action.
Self time already leaves out every timed call inside a point, and every call
into the fake and every MIDI port call is one, so neither the fake nor the fake
X-Touches' decoding is counted.

Times vary with the machine and its load, so these aren't pass/fail tests in
`ctest`. They run on demand, under a label a plain `ctest` skips, and compare
against a snapshot, as the idle profile does today, but without REAPER, and
with the same scenario every time. They miss REAPER's effect on JPRSurf's own
code (the cache, and its UI work on the same thread), which an occasional
profile in REAPER still shows.

## Measure REAPER's costs

- **Layers:** common
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Check the fakes in REAPER*, *REAPER call count tests*
- **Background:** [testing_and_profiling.md](testing_and_profiling.md)
  (Profiler), [profiles/README.md](../profiles/README.md)

What each function on the API list costs in REAPER, measured on its own, under
the conditions its cost depends on, rather than through the default config's
gestures: inside a `PreventUIRefresh()` batch or not, and as it scales with the
project (tracks, selected tracks, and routes). The end of a batch, an undo
point, and each action JPRSurf runs are measured the same way. No device or
config is involved: the measurements call REAPER's API directly, on generated
projects, in the test install and runner *Check the fakes in REAPER* builds.
- **Deferred work:** some of a call's cost comes after it returns, in REAPER's
  UI refresh. A measurement times the runs after the call too, against runs
  with no call, so what REAPER puts off is still counted.
- **The result** is the cost table *REAPER call count tests* weighs calls with:
  each function's category, and how it scales. It is measured again when the
  API list grows, or REAPER updates, rather than with each change.

The smoke scenario and its profile (`profiles/smoke.txt`) then go: a gesture's
cost is its calls, bounded by the call count tests and weighed by this table,
and JPRSurf's own time, from *Benchmarks under the fake*. CLAUDE.md's smoke
test, checked by hand at the end of a feature, no longer rides along with a
profiled run. What isolated measurements miss, REAPER's effect on JPRSurf's own
code, the log's "Slow run" warnings show in everyday use.

## Record the profile bar on performance cores

- **Layers:** none (docs and profiles)
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [profiles/README.md](../profiles/README.md)

Profiles of the same scenario varied by about 30% between sessions. Game Bits'
profiler now reports the tick rate and the kind of core each frame ran on, and
profiles from four sessions showed the cause is the core: frames on efficiency
cores run about 25-30% slower, and Windows moves REAPER onto them when it is in
the background. `profiles/README.md` now says to profile with REAPER in the
foreground, and how to read the core table, which brings the variation down to
about 10%. The profiler's own cost didn't change enough to touch its budget
(1.48us a run, against 1.51us before).

`profiles/idle.txt` has been recorded again under those conditions, with every
frame on performance cores. What is left is `smoke.txt`, unless *Measure
REAPER's costs* retires it first, recorded the same way at the end of the next
feature that profiles the smoke test.

## Keep common free of the plugin's name

- **Layers:** common, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [fake_reaper.md](worklog/fake_reaper.md) (The profile is
  the plugin's)

`common`, `device`, and `scene` shouldn't know which plugin they are in. After
*Fake REAPER* moves the profile path into the plugin, `common` still names
JPRSurf in the profile's header (`JPRSurf profile`) and run split (`JPRSurf
15us, REAPER 12us`), the trace's header, and some comments (`log_file.h`,
`reaper_api.h`, `reaper_profiler.h`, `reaper_trace.h`, and in `testing/`
`fake_midi`, `fake_reaper`, and `surface_notifier.h`), and generates the
build info (the plugin's git commit) itself. The split's label becomes the
extension's, or a neutral word, the headers take a name the plugin gives, and
the build info moves to `plugin`, which passes it in. `device` and `scene`
already don't name it.

## Units in the names of plain times

- **Layers:** common, device, scene, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** none

A time held as a plain number, rather than an `absl::Duration` or
`absl::Time`, says its unit in its name, as `Control::kLongPressDurationSecs`
does. Many don't yet: `kDependentBindingDelay` and `kMotorizedBindingDelay` in
`control.cc`, and the run times kept as seconds, such as `last_run_time_`,
`state_start_time`, `last_change_time_`, and `last_visibility_time_`. Scrub
the code for them, and either add the unit (`Secs`, `Ms`) or, where it reads
better and costs nothing on the realtime path, use the Abseil type.

## Undo points in the project the changes were made in

- **Layers:** common
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [fake_reaper.md](worklog/fake_reaper.md) (CL8: undo and the
  transport are per project)

Undo is per project in REAPER, but `ContinuousUndo` adds its pending undo point
up to `kDelay` (500ms) after the last change, with `Undo_OnStateChangeEx()`,
which takes no project, so it goes to whichever project is current then. Move a
send fader and switch project tabs within half a second, and the undo point
probably lands in the other tab, undoing nothing there, while the project that
changed loses it. The same goes for a `Flush()` after a tab switch. This is read
from the code, not yet seen in REAPER.

Remember the project the pending changes were made in (`EnumProjects(-1, ...)`
when the first change is recorded), and add the undo point to it with
`Undo_OnStateChangeEx2()`, wherever and whenever it is flushed. A change in
another project first adds the pending undo point to its own project, as a
change with a different description does. Flushing when the project changes
instead isn't enough: `SetTrackListChange()` comes with a tab switch, but
doesn't mean the project changed, and REAPER has no other notice of it.

`EnumProjects` and `Undo_OnStateChangeEx2` join the API list, so the fake fakes
both (the current project, and an undo point in the given one). `undo_test.cc`
then tests a change made in one tab, a switch to another, and the undo point
landing in the first. **Verify** in REAPER: move a send fader, switch tabs at
once, and undo in each tab.

## Report touch to REAPER for automation

- **Layers:** common, scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [automation_modes.md](worklog/automation_modes.md),
  [fake_xtouch.md](worklog/fake_xtouch.md)

REAPER asks a control surface whether a track's volume or pan is touched
(`IReaperControlSurface::GetTouchState(track, isPan)`), to know when Touch and
Latch automation should record. JPRSurf never answers, so REAPER takes each
move as a brief touch: a fader recording in Touch mode keeps dropping back to
the envelope between moves, as the user found at the end of *Fake X-Touch and
device tests*. Klinke answers it (`CSurf_MCU::GetTouchState()` in
`csurf_mcu.cpp`), with the faders' touch, and for the pans, which have none,
touched for 3 seconds after the last turn.

- `ControlSurfaceListener` answers `GetTouchState()`.
- A mapping that writes a track's volume or pan reports its control's touch:
  its press input for a fader with touch, and for one without, a short time
  after its last input. The scene answers for each track from the mappings
  active on it.
- To confirm first, in REAPER: how often it asks, for which tracks, and what it
  does with the answer in each automation mode.

## Widgets

- **Layers:** device
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Widgets)

The names mappings use for hardware, independent of which device a control is
on. A widget is a control, a struct of named widgets, or an array of widgets
of the same shape:
- **Shapes:** a control's shape is its inputs, outputs, and output mode names,
  and struct and array shapes follow from their fields and elements. Device
  catalogs (*Device types and catalogs*) are struct shapes too.
- **Definitions:** a path to a device control or struct, a new struct (with
  includes and exclusions), and joined arrays. Checking catches unknown paths,
  fields defined twice, and array elements whose shapes don't match.
- **Resolving:** paths such as `strip[3].fader` resolve to the `Control`s of
  the devices that are present. A joined array drops the elements of an absent
  device.

Zipped arrays are part of the design, but nothing needs them yet, so they wait
for a device that does. It is pure `device` code with no REAPER dependency, so
all of it is unit tested against hand-built shapes, before any real device
publishes one.

## Device types and catalogs

- **Layers:** device, scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** *Widgets*
- **Background:** [config_model.md](config_model.md) (Devices)

A registry of device types by name, each with:
- A **catalog**: its controls as a struct widget, with arrays of structs for
  the controls that repeat (the X-Touch's `strip` array), and named output
  modes (solid and blink for lights, the ring styles and off for encoders). It
  is plain data, without ports or REAPER, so it is unit tested.
- A **factory** that creates the `Device` from its ports and **control
  overrides**: inputs or outputs of particular controls that a unit doesn't use.
  `Device` drops them before creating each `Control`, so the extender's fader
  without touch sensing stops being hard coded.

`DeviceXTouch` takes its control names from its catalog, and `Device` checks
once, when it is created, that the controls it built match the catalog,
logging an error if they don't. A unit test checks it too, for each device
type, creating the device on the fake hardware's ports (as
`device_xtouch_test.cc` does).

The last CL moves the plugin onto device types and widgets. It creates its
devices through the registry, with the X-Touch required and the extender
optional (so the extender alone no longer loads, deliberately). It defines
JPRSurf's widgets in C++, and the scene maps through widget paths and output
mode names rather than `"XTouch/..."` control names and mode numbers. That
removes the device prefixes and the device checks in the strip loops. Apart
from the extender alone, behavior doesn't change, so the smoke test verifies
it.

## Build the scene from a SurfaceSpec

- **Layers:** a new spec library, scene, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Device types and catalogs*
- **Background:** [config_model.md](config_model.md)

The plugin builds its scene from a C++ data structure, the `SurfaceSpec`,
instead of imperative calls: devices (by type, required or optional), widgets,
views, templates, mappings, declared components, and settings (the track
filter). JPRSurf's own surface becomes a spec defined in code. Behavior doesn't
change, so the smoke test verifies it, and later reading a config file is just
filling in the same struct.

Checking a spec and building a scene from it are separate:
- **Checking** has no REAPER dependency, so it is unit tested. It checks
  widgets against the device catalogs, and everything that names a property
  against a **property catalog**: plain data giving each built in property's
  namespace, name, type, and whether it reads, writes, or triggers. `scene`
  checks once, when the scene is built, that each catalog entry resolves to a
  property of the same type, logging an error if not. `cmd:` properties are
  only checked for syntax, as only REAPER knows which commands exist. A
  mapping that writes a fixed value holds the type and value, and building
  maps it to the name of a `const:` property added for it
  (`Scene::AddConstProperty()`).
- **Building** needs REAPER, or the fake REAPER and fake hardware, so it is
  unit tested too. It creates the devices, stops if a required device is
  absent or a command isn't installed, expands templates and repeated views,
  and creates the scene. Surface tests (see *Surface tests*) check that the
  scene built from JPRSurf's spec behaves as the C++ surface did.

This settles where the spec library sits: checking depends on `device` (for
the catalogs) but not on `scene`, and building depends on `scene`. No state in
the plugin outlives the scene, which is what makes *Reload the config without
restarting REAPER* cheap. If anything still isn't a component by then, the
plugin can register it as a named component the spec refers to, as a stopgap.

## Config file language and loader

- **Layers:** spec library, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Build the scene from a SurfaceSpec*
- **Background:** none

A bespoke language, parsed with `gb/parse` into a `SurfaceSpec`. It is a DSL
rather than `gb::ReadConfigFromText()` for three reasons: maps in `gb::Config`
are unordered, it keeps no source locations for errors, and mappings are too
dense to be one object each.

The config is its own file (or set of files). REAPER's config string only
refers to it, and `ShowConfig()` grows a file selector. The design settles how
errors are reported (log, REAPER console, partial or total failure), and
includes a schema version. A parity test checks that JPRSurf's own config,
parsed, equals the in-code spec, so the translation is verified without REAPER.

## Public config spec and user guide split

- **Layers:** none (docs)
- **Size:** medium
- **Feature workflow:** no
- **Depends on:** *Config file language and loader*
- **Background:** [config_model.md](config_model.md)

The config model doc becomes a public specification of the config language. The
user guide becomes the guide to JPRSurf's own config, and the reference for how
it uses the spec. This comes after the loader, so the spec has been proven by
writing a real config in it.

## Rec and Solo on route strips

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

In Send/Receive mode the route strips leave Rec and Solo unmapped, so they
clear and sit dark. A route strip's `route:other_track` fields already cover
the solo and rec arm of the track at the other end of the route
(`route:other_track.ui_solo` and so on), so the work is mostly deciding what
the buttons should mean on a route strip.

## Info strip select behavior

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

The Info strip shows the Send/Receive track with the same controls as a Track
mode strip, except that select is unmapped. Decide what select should do there.
It is the one strip whose track is the mode's own subject, so the Track mode
select behavior is not obviously the right answer.

## Ranges in Send/Receive mode

- **Layers:** common, scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** nothing for route mute; selecting routes depends on *Local
  surface-only selection within routes*
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md),
  [track_actions.md](worklog/track_actions.md)

Anchors are Track mode only today. `Anchor<T>`, `AnchorHold`, and the view's
anchor are all generic, but the ranged behavior lives in the scene's
`TrackActions`, and has no route equivalent. Ranged route mute is a `scene`
change, with route range primitives in `common` beside `TrackRange` and
`TrackBatch`.

## Local surface-only selection within routes

- **Layers:** scene, plugin
- **Size:** medium
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [surface_modes.md](worklog/surface_modes.md)

A selection of route strips that lives on the surface only, for grouped volume,
pan, and mute changes across several routes at once. REAPER has no notion of a
selected send or receive, so this is entirely a scene and plugin concept, and
needs its own lights and clearing rules.

## More polled lights

- **Layers:** scene, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [utility_buttons.md](worklog/utility_buttons.md)

Anything REAPER can answer cheaply is a polled toggle row in the
`kStateProperties` table and a write mapping to a button. Each one costs a poll
every run, so weigh it against *Cheaper polling for the polled toggles* below.
A row can also have a write function, so the button can change the state too.

## Modes for the other assign buttons

- **Layers:** scene, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Modes),
  [surface_modes.md](worklog/surface_modes.md),
  [enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)

Track and Send/Receive use two of the X-Touch assign buttons. Modes are an
enumerated value, so another mode is another value of `user:surface_mode`, a
view enabled by it, and perhaps a pick and a new kind of subject (a track's FX,
for the Plugin button). What is missing is what the other modes should do, which
is the design work.

## Reload the config without restarting REAPER

- **Layers:** plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** *Config file language and loader*
- **Background:** none

Tear down the scene and build a new one from the config file, perhaps from a
REAPER action. This makes iterating on a config much faster. It should be cheap
if nothing in the plugin outlives the scene.

## Generic MIDI devices defined in config

- **Layers:** device, spec library
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Config file language and loader*
- **Background:** none

Devices are C++ today, which suits the X-Touch (scribble strip sysex, meters,
timecode). A generic device whose controls are defined in the config (MIDI
message to control, with input and output types) would support simple
controllers without any code, much like CSI's surface files. These define
device controls, not widgets (see [config_model.md](config_model.md)).

## Choose a config by the devices present

- **Layers:** spec library, plugin
- **Size:** large
- **Feature workflow:** yes
- **Depends on:** *Config file language and loader*
- **Background:** [config_model.md](config_model.md) (Devices)

A direction rather than a plan. Each device in a config is required or
optional, so one config covers a set of hardware with some units missing. A
config set would go further: several configs, with the one that best fits the
devices actually connected chosen at startup. With only the X-Touch, for
instance, a different config could drop the Info strip so all 8 strips show
routes, rather than just losing the extender's strips. It needs a definition of
"best fits" (such as the config with the most devices present whose required
devices are all present), and a way to say which config was chosen.

## Modifiers a mapping ignores

- **Layers:** device, scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Modifier sets)

A read mapping fires only when exactly its required modifiers are on, among all
the modifiers the read mappings on its control mention. That keeps Undo and
Redo, or Save and Save New Version, unambiguous without priorities. But a
mapping meant to fire with a modifier held "whatever else is held" needs a copy
for every combination of the other modifiers on the control. Today that is the
Send/Receive pick on a strip's select button, and the mapping beside it that
enters the mode, which each need a second copy for Send held along with a
select anchor (`InitViews()` in `plugin_surface.cc`).

A mapping could also list modifiers it ignores, so the pick is one mapping that
requires `mod:send_hold` and ignores `mod:select_anchor`. It is purely
additive: existing mappings and configs mean the same thing either way, so it
can be done at any time, such as when a second case turns up.

## Conditions that compare values

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [config_model.md](config_model.md) (Mappings)

A `ViewCondition` is met while its property, read as its value's type, equals
it (`ViewProperty::Equals()`, see
[enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)). It
could also compare in other ways (not equal, less, greater, and so on), so a
condition could test a level directly, rather than needing a toggle made for
it. Less and greater need an order for text and colors. Mode overrides are a
value to mode map today, and could become conditions with it. The config model
keeps a condition to one property equal to a value, with combining states left
to components, so it needs updating too.

## Share a condition across mappings

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:**
  [enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)

When one press does two things, each is its own mapping with the same
condition, such as the pick and the mapping that sets `user:surface_mode` to
enter Send/Receive mode, both requiring `has_routes`. The condition is written, and
watched, once per mapping, and the copies must be kept the same by hand. A
mapping could instead name a condition declared once on its view, shared by
every mapping that uses it and watched once, for less to write (ergonomics)
and fewer watches (performance). Worth doing once a config has several such
groups, or a measurement shows the extra watches matter.

## Value names for every enumerated property

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:**
  [enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)

`EnumeratedValueProperty` has a name for each value: `GetText()` returns it,
and `SetText()` sets the value by name. Other enumerated properties don't. The
ruler mode properties spell out their names in `GetText()` switches, and
`SetText()` reads a number. Names could be part of `ViewProperty` for every
enumerated property instead (a names list, or a virtual that names a value),
with the base `GetText()` and `SetText()` mapping between names and values. A
config could then refer to any enumerated value by name, rather than by an
index that has to match the order of the names. Worth doing when a config
names enumerated values (see *Build the scene from a SurfaceSpec*).

## Friendly names for common commands

- **Layers:** spec library
- **Size:** small
- **Feature workflow:** no
- **Depends on:** *Config file language and loader*
- **Background:** [config_model.md](config_model.md) (The escape hatch)

REAPER's built in actions only have numbers, so a config that maps Undo says
`cmd:40029`. A config could name the common commands instead, starting with
the ones JPRSurf's own config uses (the `kCmd*` constants in
`command_properties.h`): undo, save, play, stop, the automation modes, and so
on. As with any shorthand, the config expands a name to its `cmd:<id>` form,
so `scene` doesn't change. The names are part of the public spec, so the list
is chosen with *Public config spec and user guide split* in mind, and numbers
and named command IDs keep working for everything else.

## Read-only toggle mappings register for changes they ignore

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [ranged_track_actions.md](worklog/ranged_track_actions.md)

A read-only toggle mapping still registers for property change notices, so it
calls `WriteControl()`, which does nothing, on every change. Registering only
when the mapping actually writes the control would skip that. No user-visible
effect, so it needs temporary logging or a performance measurement to show the
difference.

## Refresh a reference's track only for polled fields

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:**
  [enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)
  (Performance)

Only worth doing if it shows up in the `Run()` average. While anything watches
a field of a track reference, `TrackReference::Update()` calls
`Track::Refresh()` every run (three REAPER reads), even when the watched fields
never read polled state. Track mode's Send light watches
`state:selected_track.has_routes`, which only changes through
`OnTrackRoutesChanged()`, so the selected track is refreshed every run for
nothing. A track property could say whether it reads polled state (name,
color, mute, volume, and so on) or only notified state (`has_routes`,
`exists`, `is_folder`, `has_parent`), and the reference refresh only while a
polled one is watched, as `IsMeterWatched()` already gates the meter.
`TrackProperties::OnTrackChanged()` could also skip `has_routes`, which it
notifies on every change to the track.

## Cheaper polling for the polled toggles

- **Layers:** scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [utility_buttons.md](worklog/utility_buttons.md)

Only worth doing if a polled read becomes a problem. `kStateAnyItemSelected`
costs a steady ~10us of the `Run()` average today, which was accepted. In
order:

1. **Throttle.** Give each polled toggle a poll interval in runs, and
   have `PolledToggleProperty::UpdateState()` read only every Nth run. Polling
   `kStateAnyItemSelected` every 4th run would cut it to ~2-3us, with the light
   lagging by up to ~130ms.
2. **Gate on `GetProjectStateChangeCount(nullptr)`**, which REAPER increments
   whenever the project changes, and re-read only when it moves. This could be
   one switch for every polled state and command toggle state that can use it.
   It is the second choice because whether a selection change bumps the count
   depends on the user's undo preferences.

## Poll only the routes a routes list shows

- **Layers:** common, scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [view_subjects.md](worklog/view_subjects.md) (Lists)

Only worth doing if route polling shows up in the `Run()` average. REAPER
doesn't reliably report route volume, pan, and mute changes, so a routes list
calls `Track::RefreshRoutes()` every run while it is active, which reads every
send and every receive of its track (two REAPER calls each). Only the routes
of the list's route type are shown, one per item from its scroll position, so
a track with 40 receives shown as sends costs 80+ calls a run where at most 2
per strip are needed. A `Track::RefreshRoutes(type, first, count)`
would bound it to what the strips show.

## Act on REAPER re-reporting the last touched track

- **Layers:** common, scene
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [view_subjects.md](worklog/view_subjects.md) (Behavior)

Only worth doing if it turns out to be annoying. REAPER reports the last
touched track again when the same track is touched again, but references only
act on a change, so touching it again moves nothing. Track mode no longer
scrolls back to a track the user banked away from, and Send/Receive mode no
longer returns to it after crossing a route. Before references, the plugin
acted on every report.

A re-report can't simply count as a change: re-touching the track Send/Receive
mode already shows would then reset its routes list and release its anchor, as
views bound to `user:current_track` reset on its version. It needs a second
signal on track references, apart from their version:
- `TrackCache` counts REAPER's reports (not the surface's own touches, which
  `TrackActions` sets directly), and `state:last_touched_track` is "renewed" on
  each one, even for the same track.
- A follower passes a renewal on as a renewal, and a list's reveal watches
  renewals as well as changes, while bound views keep watching only the
  version.

## Reset the Send/Receive routes on entering the mode

- **Layers:** scene, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [view_subjects.md](worklog/view_subjects.md) (Behavior)

Only worth doing if the current behavior is unwelcome. Re-entering Send/Receive
mode for the track it last showed keeps the route type and scroll position it
had, as a list only resets when its track changes. Before references, entering
always showed sends (or receives, if the track only has receives) from the
first route. Entering the mode could reset the list explicitly, such as with a
`view:` action on a routes list that re-applies its route type rule and scrolls
to the start.

## Separate undo points for track faders

- **Layers:** common
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [track_actions.md](worklog/track_actions.md) (REAPER facts)

Only worth doing if it gets in the way. REAPER creates the undo point for a
track volume or pan change made from the surface itself, and holds it open,
folding it into the next undo point it is given. So a track fader or pan move
followed within about half a second by anything with its own undo point (a
mute, or a send fader's undo point from `ContinuousUndo`) is one undo step.
REAPER's `CSurf_FlushUndo(true)`, called before JPRSurf adds an undo point (in
`ContinuousUndo::Flush()` and `TrackBatch`), might close REAPER's one first.
That is untested: it didn't make route changes create undo points of their own
(see [surface_modes.md](worklog/surface_modes.md), Route undo), which is a
different question. It would be added to the API list.

## Short MIDI messages at their length on the fake output

- **Layers:** common
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [fake_xtouch.md](worklog/fake_xtouch.md)

Only worth doing if a test needs it. `FakeMidiOutput::Send()` passes on and
records all three bytes it is given, so channel pressure and program change,
which have one data byte, arrive with a stray third byte. The fake X-Touch
ignores it, and the tests match it with `_`. Passing on only the bytes the
status has would make the raw messages exact, as the hardware receives them,
once it is checked what REAPER sends for them.

## Profile snapshots on demand

- **Layers:** common, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** *Profiler*
- **Background:** [testing_and_profiling.md](testing_and_profiling.md)
  (Output)

Only worth doing if a whole session's snapshot mixes too much together
(startup, idle, and activity). REAPER actions to reset the profile and to save
a snapshot would measure one scenario on its own: reset, do the thing, save.
These would be the plugin's first REAPER actions.

The workload values need setting before such a snapshot: a reset clears them,
and the scene sets its own only when it is activated or destroyed
(`Scene::SetWorkloadValues()`), and `TrackCache` on its next refresh. The
snapshot would have them set when it is taken, which adds `scene` to the
layers.

## Verbose logging

- **Layers:** common, plugin
- **Size:** small
- **Feature workflow:** no
- **Depends on:** nothing
- **Background:** [profiler.md](worklog/profiler.md)

The log is quiet unless something is wrong: errors, slow runs, and the
profile's summary on exit. A `JPRSURF_VERBOSE` environment variable, set for
REAPER's process as `JPRSURF_TRACE` is, would log events that help while
debugging, such as track list refreshes and view activations (the profiler
replaced their timing lines). `ControlSurface` already logs every REAPER
callback with Abseil's `VLOG(1)`, so the variable could set the `VLOG` level.
Only worth doing once a debugging session needs it.

