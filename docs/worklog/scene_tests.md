# Scene tests

Tests of `scene` on its own, for the detail the surface tests don't reach:
properties against REAPER's state (track, route, state, command, and timeline
properties), views (conditions, subjects, lists, references, and anchors),
mappings (each property type each way, modifiers, taps, picks, and
conditions), and `TrackActions` (every modifier, ranges, anchors, grouping,
and batching). Each file in `scene` gets its own test, against the fake REAPER
and a device of fake controls. The design is in
[testing_and_profiling.md](../testing_and_profiling.md) (Tests).

There is no change in behavior. A bug a test finds is fixed in its own
follow-up CL, with the test disabled until then, as in *Surface tests*.

## Design

### Names

| Name                          | What                                       | Might be confused with             |
| ----------------------------- | ------------------------------------------ | ---------------------------------- |
| `FakeProject::AddTracks()`    | Adds tracks named for where they are       | `FakeProject::AddTrack()`          |
| `FakeDevice`                  | A `Device` of fake controls, with no MIDI  | `FakeXTouch`; `TestDevice`         |
| `jpr_device_fakes`            | The test-only library holding `FakeDevice` | `jpr_device_testing`               |
| `SceneTest`                   | The fixture for scene tests                | `SurfaceTest`; `scene_test.cc`     |
| `GetCachedTrack()`            | The track cache's track for a fake track   | `TrackCache::GetTrack()`           |
| `FakeReaper::EndEntryPoint()` | Ends a call REAPER would have made         | `TestControlSurface`'s `EndCall()` |
| `TestProperty`                | A property of any type holding a value     | `CreateConstProperty()`            |

- `AddTracks()` names tracks T2 and T2.1, and moves from `SurfaceTest`.
  `AddTrack()` takes the name.
- `FakeXTouch` is the hardware end of an X-Touch's ports. `TestDevice`, in
  `device_test.cc`, is replaced by `FakeDevice`.
- `jpr_device_fakes` holds `fake_control_io.h` and `FakeDevice`, and links
  `jpr_device`. `jpr_device_testing` holds `FakeXTouch`, and deliberately
  doesn't link `jpr_device`.
- `SceneTest` is to `scene` what `SurfaceTest` is to the plugin. Its header is
  `scene/testing/scene_test.h`, while `scene_test.cc` holds `Scene`'s own
  tests, which use it.

### Tests on fake controls, not a fake X-Touch

The design doc planned scene tests on fake X-Touches. They test `scene`
through `DeviceXTouch` and its protocol, though, where `scene` only uses
`Device` and `Control`. Fake controls (`fake_control_io.h`) keep each test to
the file it tests:
- **Any control a mapping can meet.** A test builds the inputs and outputs it
  needs (a press without release, a value and a delta together, outputs with
  several modes), where the X-Touch has a fixed set.
- **Exact values.** A fader's value is the value the property gets, with no
  MCU fader curve, 7 character scribble strip, or ring positions between.
- `DeviceXTouch` is already tested on the fake X-Touch, and surface tests test
  the whole chain on it.

### device: FakeDevice (device/testing/fake_device.h)

```
// A device of fake controls, which a test adds by name, then drives and reads
// through their fake inputs and outputs, with no MIDI or hardware.
class FakeDevice final : public Device {
 public:
  // The fakes a control is made of, as Control::Options takes them. The
  // control has no input or output for any that is null.
  struct ControlOptions {
    std::string_view name;
    std::unique_ptr<FakeValueInput> value_input;
    ...  // Each input, the binding, and each output.
  };

  // A control added to the device, and its fakes, which are null for any it
  // doesn't have.
  struct FakeControl {
    Control* control = nullptr;
    FakeValueInput* value_input = nullptr;
    ...  // Each input and output.
  };

  explicit FakeDevice(RunRegistry& run_registry);

  // Adds a control made of the fakes, and returns it. If the name is taken,
  // every pointer returned is null.
  FakeControl AddControl(ControlOptions options);

  // Each adds a control of a common kind:
  // - A button: a press input with release, and a light (a DValue output).
  // - A fader: a value input, a touch (a press input), and a CValue output,
  //   motorized.
  // - A pot: a delta input, and a CValue output (its ring). A pot that can be
  //   pushed has a button of its own, as Control::Options recommends.
  // - A display: text and color outputs.
  FakeControl AddButton(std::string_view name);
  FakeControl AddFader(std::string_view name);
  FakeControl AddPot(std::string_view name);
  FakeControl AddDisplay(std::string_view name);
};
```

- `ControlOptions` holds the fake types, so a control can only be made of
  fakes, with no cast. It mirrors `Control::Options`, so a new input or output
  type in `Control` is added to it too.
- `Device::AddControl()` returns the control it added, or null if the name is
  taken, which `FakeDevice` returns. `DeviceXTouch` ignores it.
- `fake_control_io.h` moves from `jpr_device`'s test sources into
  `jpr_device_fakes`, beside `FakeDevice`, in `jpr/device/testing`.
  `jpr_device_testing` stays as it is, so `FakeXTouch` still can't reach
  `jpr_device`.
- `device_test.cc`'s `TestDevice` becomes `FakeDevice`.

### scene: SceneTest (scene/testing/scene_test.h)

A fixture in a test-only `jpr_scene_testing` library, as `SurfaceTest` is in
`jpr_plugin_testing`:
- The fake REAPER, with REAPER's actions (`AddReaperActions()`), a
  `SurfaceNotifier`, a `Scene`, and a `FakeDevice` in it, which a test adds
  its controls to. `Scene::GetControl()` now asks the device for a control,
  rather than keeping its own copy of each device's controls from when the
  device was added, so a control can be added at any time.
- **The scene runs from a control surface,** as in the plugin. The scene reads
  REAPER through `TrackCache` and `ContinuousUndo`, which only a
  `ControlSurface` keeps current, so `Scene`'s comment now says it must run
  from a `ControlSurfaceListener`'s `OnRun()`. The fixture registers a surface
  type whose listener activates the scene, and runs the devices, then the
  input, then the scene, as `PluginSurface` does. `AddSurface()` and
  `RemoveSurface()` add and remove it, as REAPER does at startup and exit. So
  the notifier makes REAPER's calls back (selection, rec arm, the last touched
  track), each run is an entry point the fake checks, and the notifier's UI
  changes (`ClickTrack()`) work.
- **Input is queued** (`Press()` and `Release()`), and arrives in the next run
  between the controls and the scene, as MIDI input does. Each run, the
  controls clear the input that arrived since they last ran.
- `RunUntilShown()` is two runs, as for the surface. Presses that get the
  clock right: `Tap()`, `DoublePress()`, `LongPress()`, and `Hold()`, waiting
  for `Control::kLongPressDurationSecs` and `kDoublePressWindowSecs`, as
  `SurfaceTest`'s do.
- **A test of a property alone doesn't need the fixture.** It uses a plain
  fixture (the fake, and a scene for the property), refreshes the track cache
  or calls `UpdateState()` itself, and ends each call with
  `EndEntryPoint()` where it needs to. The scene's polling of what is watched
  is tested with the scene. Tests that need REAPER's calls back, such as for
  the selected tracks' automation modes, use `SceneTest`.
- Anything logged at `ERROR` or above fails the test (`gb::LogErrorGuard`),
  unless the test takes it, as a mapping that fails to be added logs why.

**Brittleness:** a test must give input through the fixture, never on a
control's fakes, which would never reach the scene. The header says so.

### scene: Process state

`scene`'s globals get a `TestReset`, so no test sees another's:
`g_last_auto_override` (`state_properties.cc`), the override that turning
`state:auto_override_active` on restores, and `g_next_const_id`
(`const_property.cc`), so const: names are the same in every test. The
process state table in the design doc gains them.

### Tests

One file per source file, each against the fake, and fake controls where it
needs them:
- **`scene_test.cc`:** controls by device and name; property lookup in each
  namespace; user, const, and modifier properties and their name rules; the
  built in references; track references' fallback and follow; activation; and
  conditional views applied between runs.
- **`track_properties_test.cc`:** each property read from and written to the
  fake, the plain and ui_ ones, the meter, and the stub and deleted tracks.
- **`route_properties_test.cc`:** each property of sends and receives, a route
  past the end, the other track's fields, and the routes changing.
- **`track_reference_test.cc`, `route_reference_test.cc`:** setting, versions,
  fields following the reference, and `Update()` refreshing only what is
  watched.
- **`state_properties_test.cc`:** each polled toggle, the selected tracks'
  automation modes, the override (and what turning it on restores), and the
  timeline and ruler names.
- **`polled_toggle_property_test.cc`:** polling only while watched, and
  writing.
- **`command_properties_test.cc`:** numeric and named commands, toggles and
  actions, and an unknown command.
- **`timeline_property_test.cc`:** each position source, playing and stopped,
  and each ruler mode property, primary and secondary.
- **`value_property_test.cc`** (adding to the existing tests): enumerated
  values, and the callback properties.
- **`track_actions_test.cc`:** select, toggles, volume, and pan with every
  modifier; ranges by parent and filter; anchors; grouping; and one batch for
  every change of more than one track.
- **`track_anchor_property_test.cc`, `track_pick_property_test.cc`:** holding
  and releasing an anchor, and picking from a source or the view.
- **`view_test.cc`:** enabling, activity, and conditions; each subject; user
  properties and their scope; the anchor released on a new subject or
  deactivation; and mappings that fail.
- **`view_list_test.cc`:** child tracks (scrolling, banks, navigating in, up,
  and to the root, and reveal), routes (type rules, the toggle and its name,
  and crossing to the other track), and the track list changing.
- **`view_mapping_test.cc`:** writing each property type, with modes and mode
  overrides; reading each type from each input, with ranges, press_toggles,
  and press_release; modifiers; taps, double and long presses; and
  conditions.

## CLs

### CL1 [x] common: AddTracks() on the fake project

Depends on: nothing.

- `FakeProject::AddTracks(count, folder)`, moved from `SurfaceTest`, which
  calls it.
- Test only, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `fake_reaper_test.cc` checks the names and folders, with the tests moved
  from `surface_test_test.cc`.

### CL2 [x] device: FakeDevice

Depends on: nothing.

- `jpr_device_fakes` in `jpr/device/testing`: `fake_control_io.h`, moved, and
  `FakeDevice`.
- `device_test.cc` uses `FakeDevice` for its `TestDevice`, and every device
  test that included `fake_control_io.h` links `jpr_device_fakes`.
- `Device::AddControl()` returns the control it added, or null if the name is
  taken.

**Verify**
- Standard checks.
- `fake_device_test.cc`: each kind of control has the inputs and outputs it
  says, and a control added from options keeps its fakes.

### CL3 [x] scene: SceneTest, process state, and Scene's tests

Depends on: CL1, CL2.

- `TestReset`s for `g_last_auto_override` and `g_next_const_id`, and the
  design doc's process state table.
- `jpr_scene_testing` with `SceneTest`, and `scene_test_test.cc` for its
  presses and `RunUntilShown()`.
- `Scene`'s comment says it runs from a `ControlSurfaceListener`.
- `Scene::GetControl()` asks the device for the control, and the scene no
  longer keeps its own copy of the controls.
- `scene_test.cc`.

**Verify**
- Standard checks.
- A test that turning the override on restores Bypass in a new fake, after
  an earlier fake saw Write.

### CL4 [x] scene: Track and route properties, and references

Depends on: CL3.

- `track_properties_test.cc`, `route_properties_test.cc`,
  `track_reference_test.cc`, and `route_reference_test.cc`.

**Verify**
- Standard checks.

### CL5 [x] common: Entry points and cached tracks in the fake

Depends on: nothing.

- `FakeReaper::EndEntryPoint()`: a test that calls JPRSurf's code directly,
  outside a surface's run, ends each call REAPER would have made, so the fake
  checks each on its own (such as the batching rule) rather than all of the
  test's calls as one. `TestControlSurface` ends its calls with it.
- `GetCachedTrack(FakeTrack*)`, in its own `cached_track.h` so the fake
  doesn't depend on the cache: the track cache's track for a fake track,
  replacing the copies in each test that needed one.
- `reaper_trace_test.cc` moves onto `FakeReaper`, with a `SurfaceNotifier`
  for the calls back it checks, in place of its own small fake. The fake reads
  `IP_TRACKNUMBER`, and reads `P_NAME` for every track but the master, which
  REAPER's documentation says reads as null, and nobody has checked.

**Verify**
- Standard checks.
- `fake_reaper_test.cc`: two changes to several tracks, each ended, pass, and
  without the end fail.
- `reaper_trace_test.cc` checks what it did before.

### CL6 [x] scene: State, command, and timeline properties

Depends on: CL3.

- `state_properties_test.cc`, `polled_toggle_property_test.cc`,
  `command_properties_test.cc`, `timeline_property_test.cc`, and additions to
  `value_property_test.cc`.

**Verify**
- Standard checks.

### CL7 [x] scene: Track actions, anchors, and picks

Depends on: CL3, CL5.

- `track_actions_test.cc`, `track_anchor_property_test.cc`, and
  `track_pick_property_test.cc`. The fake fails any change of more than one
  track outside one batch, so every multi-track action is checked for it.

**Verify**
- Standard checks.

### CL8 [x] common: Tests of TrackRange

Depends on: nothing.

- `track_test.cc` tests `TrackRange` on its own: which ends make a range (an
  end that is null, or isn't in the filter, makes none), either order, the
  same parent rule, and tracks off the surface left out of `Contains()`. Its
  rules were only tested through `TrackActions` (CL7).

**Verify**
- Standard checks.

### CL9 [x] scene: Views

Depends on: CL3.

- `view_test.cc`.

**Verify**
- Standard checks.

### CL10 [x] scene: View lists

Depends on: CL9.

- `view_list_test.cc`.

**Verify**
- Standard checks.

### CL11 [x] scene: Mappings that write

Depends on: CL3.

- `view_mapping_test.cc`: each property type written to its output, modes and
  mode overrides, conditions holding and releasing the output, and writes
  when the property, its condition, or an override changes.
- `TestProperty` (`scene/testing/test_property.h`), a property of any type
  that holds a value its setters change, replaces `view_property_test.cc`'s
  own copy, so mapping tests can write and read any type.

**Verify**
- Standard checks.

### CL12 [ ] scene: Fix steps written in some modes

Depends on: CL11.

CL11 found two bugs in writing a pan, volume, normalized, or color property
to a DValue output, neither of which the X-Touch mappings reach today:
- A pan written to an output whose highest value is odd (an even number of
  steps, such as the X-Touch ring's spread mode) shows every pan between hard
  left and hard right as hard left: `MapPanToEvenRange()` truncates the pan to
  an int before scaling it.
- How the value is spread over the steps is picked once, from the highest
  value of the mapping's own mode, so a mode override to a mode with a
  different highest value spreads it wrongly (a pan in a mode with 2 steps
  overridden to one with 11 only ever lights the first 2).

The fixes:
- Fix `MapPanToEvenRange()` in `view_mapping.cc`.
- Pick the spread from the highest value of the mode each write resolves to,
  as the toggle and enumerated writes already do.
- `view_mapping_test.cc`: the pan test gains a DValue output with four steps,
  and a pan written with a mode override to a mode with a different highest
  value.

**Verify**
- Standard checks.

### CL13 [ ] scene: Mappings that read

Depends on: CL11.

- `view_mapping_test.cc`: each property type read from each input type,
  input type choice, property ranges, press_toggles, press_release, required
  modifiers and their mutual exclusion, taps, double and long presses, and
  read conditions.

**Verify**
- Standard checks.

## Checks in REAPER

None for the tests. The only plugin code that changes is two `TestReset`
registrations, so REAPER loads the extension with no new errors in the log,
and no profile is taken. A follow-up CL that fixes a bug a test found has its
own checks.
