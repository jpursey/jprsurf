# Scene tests

Tests of `scene` on its own, for the detail the surface tests don't reach:
properties against REAPER's state (track, route, state, command, and timeline
properties), views (conditions, subjects, lists, references, and anchors),
mappings (each property type each way, modifiers, presses, and conditions),
and `TrackActions` (every modifier, ranges, anchors, grouping, and batching).
Each file in `scene` has its own test, against the fake REAPER and a device of
fake controls. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Tests).

## Behavior

The tests found five bugs in `ViewMapping`, each fixed in a CL of its own
after the tests. None of them reached JPRSurf's own surface, whose mappings
don't use what was broken, so the surface is unchanged:
- **Pans written to an even number of steps** (an output whose highest value
  is odd, such as the X-Touch ring's spread mode) showed every pan between
  hard left and hard right as hard left: `MapPanToEvenRange()` truncated the
  pan to an int before scaling it.
- **Steps in an overridden mode:** how a pan, volume, normalized, or color
  value is spread over a DValue output's steps was picked once, from the
  mapping's own mode, so a mode override to a mode with a different highest
  value spread it wrongly. It is now picked from the mode each write resolves
  to, as the toggle and enumerated writes already did.
- **The configured input type** (`ReadConfig::input_type`) was never read, so
  a mapping always read the input its property type prefers.
  `InitReadControl()` now narrows the control's inputs to it, and each type
  picks from what is left in its usual order, reading nothing if the control
  lacks the input or the type can't read it. A `press_release` mapping reads
  nothing with any input type but a press.
- **A press between a range's ends** went to the farther end, where the
  comments said the nearer. `ToggleDouble()` now sends a value at (or beyond)
  an end to the other end, and one between them to the nearer end, or the max
  from the middle. This is the binary toggle of a pan whose range is on one
  side of the center, a volume, and a normalized value.
- **A press of a pan whose range spans the center** went from between min and
  the center to max, skipping the center. `StepPan()` now steps it to the next
  of min, the center, and max. Presses in one run were also counted modulo 3
  (or 2, for the binary toggles), which assumed the value started at a stop or
  an end. Now each press steps the value once (`Press()`).

The rest of the feature is tests and test support, plus two changes to
non-test code that tests needed:
- `Scene::GetControl()` asks the device for the control, rather than keeping
  its own copy of each device's controls from when the device was added, so a
  device's controls can be added at any time.
- `Device::AddControl()` returns the control it added, or null if the name is
  taken.

## Names

| Name                          | What                                       | Might be confused with             |
| ----------------------------- | ------------------------------------------ | ---------------------------------- |
| `FakeProject::AddTracks()`    | Adds tracks named for where they are       | `FakeProject::AddTrack()`          |
| `FakeDevice`                  | A `Device` of fake controls, with no MIDI  | `FakeXTouch`                       |
| `jpr_device_fakes`            | The test-only library holding `FakeDevice` | `jpr_device_testing`               |
| `SceneTest`                   | The fixture for scene tests                | `SurfaceTest`; `scene_test.cc`     |
| `GetCachedTrack()`            | The track cache's track for a fake track   | `TrackCache::GetTrack()`           |
| `FakeReaper::EndEntryPoint()` | Ends a call REAPER would have made         | `TestControlSurface`'s `EndCall()` |
| `TestProperty`                | A property of any type holding a value     | `CreateConstProperty()`            |

## Structure

### Tests on fake controls, not a fake X-Touch

The design doc planned scene tests on fake X-Touches. They would test `scene`
through `DeviceXTouch` and its protocol, though, where `scene` only uses
`Device` and `Control`. Fake controls keep each test to the file it tests:
- **Any control a mapping can meet.** A test builds the inputs and outputs it
  needs (a press without release, every input at once, outputs with several
  modes), where the X-Touch has a fixed set.
- **Exact values.** A fader's value is the value the property gets, with no
  MCU fader curve, 7 character scribble strip, or ring positions between.
- `DeviceXTouch` is tested on the fake X-Touch, and the surface tests test the
  whole chain on it.

### common/testing: The fake

- **`FakeProject::AddTracks(count, folder)`** names each track for where it is
  (T2, T2.1), moved from `SurfaceTest`, which calls it.
- **`FakeReaper::EndEntryPoint()`:** a test that calls JPRSurf's code directly,
  outside a surface's run, ends each call REAPER would have made, so the fake
  checks each on its own (such as the batching rule) rather than all of the
  test's calls as one. `TestControlSurface` ends its calls with it.
- **`GetCachedTrack(FakeTrack*)`** (`cached_track.h`, so the fake doesn't
  depend on the cache) returns the track cache's track for a fake track, and
  the stub track for null.
- `reaper_trace_test.cc` runs on `FakeReaper`, with a `SurfaceNotifier`, in
  place of its own small fake. The fake reads `IP_TRACKNUMBER`, and `P_NAME`
  for every track but the master, which REAPER's documentation says reads as
  null, and nobody has checked.

### device/testing: FakeDevice (fake_device.h)

A `Device` of fake controls (`fake_control_io.h`), in the test-only
`jpr_device_fakes` library. `jpr_device_testing` holds `FakeXTouch`, and
deliberately doesn't link `jpr_device`.
- `AddControl(ControlOptions)` adds a control made of fakes, and returns a
  `FakeControl`: the control and its fakes, null for any it doesn't have.
  `ControlOptions` mirrors `Control::Options` with the fake types, so a
  control can only be made of fakes, with no cast. A new input or output type
  in `Control` is added to it too.
- `AddButton()`, `AddFader()`, `AddPot()`, and `AddDisplay()` add the common
  kinds of control, as the hardware has them: a button has a light, a fader a
  touch and a motor, a pot a ring.
- `device_test.cc` uses it in place of its own `TestDevice`.

### scene/testing: SceneTest (scene_test.h)

The fixture for scene tests, in the test-only `jpr_scene_testing` library, as
`SurfaceTest` is to the plugin. `scene_test.cc` holds `Scene`'s own tests,
which use it.
- The fake REAPER, with REAPER's actions, a `SurfaceNotifier`, a `Scene`, and
  a `FakeDevice` in it (`device_`), which a test adds its controls to.
  `GetControlName("X")` is the scene's name for one ("Device/X").
- **The scene runs from a control surface,** as in the plugin. The scene reads
  REAPER through `TrackCache` and `ContinuousUndo`, which only a
  `ControlSurface` keeps current, so `Scene`'s comment says it must run from a
  `ControlSurfaceListener`'s `OnRun()`. The fixture registers a surface type
  whose listener activates the scene, and runs the devices, then the input,
  then the scene, as `PluginSurface` does. `AddSurface()` and
  `RemoveSurface()` add and remove it, as REAPER does at startup and exit. So
  REAPER's calls back work, each run is an entry point the fake checks, and
  the notifier's UI changes (`ClickTrack()`) work.
- **Input is queued** (`Press()`, `Release()`, `Move()` for a value input, and
  `Turn()` for a delta input), and arrives in the next run between the
  controls and the scene, as MIDI input does. Each run, the controls clear
  the input that arrived since they last ran.
- `RunUntilShown()` is two runs, as for the surface. `Tap()`,
  `DoublePress()`, `LongPress()`, and `Hold()` wait for
  `Control::kLongPressDurationSecs` and `kDoublePressWindowSecs`.
- **A test of a property alone doesn't use the fixture.** It uses a plain
  fixture (the fake, and a scene for the property), refreshes the track cache
  or calls `UpdateState()` itself, and ends each call with `EndEntryPoint()`
  where it needs to. The scene's polling of what is watched is tested with the
  scene.
- Anything logged at `ERROR` or above fails the test (`gb::LogErrorGuard`),
  unless the test takes it, as a mapping that fails to be added logs why.

**Brittleness:** a test must give input through the fixture, never on a
control's fakes, which would never reach the scene. The header says so.

### scene/testing: TestProperty (test_property.h)

A property of any type (`ViewProperty::Type`) that holds a value of that
type, which its setters change, notifying only when it changes, with an
enumerated property's highest value. It tests what reads and writes
properties without depending on any particular one, from `ViewProperty`'s
conversions to every mapping.

### scene: Process state

`scene`'s globals have a `TestReset`, so no test sees another's:
`g_last_auto_override` (`state_properties.cc`), the override that turning
`state:auto_override_active` on restores, and `g_next_const_id`
(`const_property.cc`), so const: names are the same in every test. The
process state table in the design doc has them.

## Tests

One file per source file, each against the fake, and fake controls where it
needs them:
- **`track_test.cc`** (`common`): `TrackRange` on its own, which ends make a
  range, either order, the same parent rule, and the filter.
- **`fake_device_test.cc`:** each kind of control has the inputs and outputs
  it says, and a control keeps its fakes.
- **`scene_test.cc`:** controls by device and name; property lookup in each
  namespace; user, const, and modifier properties and their name rules; the
  built in references; track references' fallback and follow; activation; and
  conditional views applied between runs.
- **`track_properties_test.cc`, `route_properties_test.cc`:** each property
  read and written, plain and ui_ properties, the meter, folders, the stub and
  deleted tracks, routes past the end, and the routes changing.
- **`track_reference_test.cc`, `route_reference_test.cc`:** setting, versions,
  fields following the reference, and `Update()` refreshing only what is
  watched.
- **`state_properties_test.cc`, `polled_toggle_property_test.cc`,
  `command_properties_test.cc`, `timeline_property_test.cc`,
  `value_property_test.cc`:** each polled toggle and polling only while
  watched, the selected tracks' automation modes, the override and what
  turning it on restores, commands by number and name, each timeline position
  source and ruler mode, and the enumerated and callback properties.
- **`track_actions_test.cc`, `track_anchor_property_test.cc`,
  `track_pick_property_test.cc`:** every modifier for select, the toggles,
  volume, and pan; grouping; ranges; anchors, which take precedence over every
  modifier; picks; and one batch for every change of more than one track.
- **`view_test.cc`:** activity and conditions; subjects; user properties and
  their scope; the anchor released on a new subject or deactivation; and
  mappings that fail.
- **`view_list_test.cc`:** child tracks (scrolling, banks, navigating, and
  reveal), routes (type rules, the toggle and its name, and crossing to the
  other track), actions seeing the subject, and the track list and routes
  changing.
- **`view_mapping_test.cc`:**
  - Writing: the output each type suits best, each type to each output, steps
    in a mode and an overridden mode, mode overrides, conditions holding and
    releasing the output, and writing only on a change.
  - Reading: the input each type suits best and a configured input type, each
    type from each input, ranges, `press_toggles`, `press_release`, a press
    read when pressed rather than released, presses between a range's ends
    and between a pan's stops, several presses in one run, required modifiers
    and how mappings take turns by them, taps, double and long presses,
    conditions, and which reads watch their property.

Mutation checks backed the mapping tests: every change tried to the read and
write sides of `view_mapping.cc` (well over a hundred, such as a flipped
comparison, a dropped clamp, or a changed default range) fails a test.

## Checks in REAPER

None. Outside the tests, the code that changed is in `scene` and `device`: two
`TestReset` registrations, `Scene::GetControl()`, `Device::AddControl()`'s
return value, and the `ViewMapping` fixes, none of which changes what
JPRSurf's own mappings do. So there was no smoke test or profile.
