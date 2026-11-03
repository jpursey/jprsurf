# Device types and catalogs

Each kind of hardware JPRSurf supports becomes a **device type**, by name: a
**catalog** of its controls, as a struct widget shape, and a factory that
creates the device from its ports (see [Device types](../config_model.md#device-types)
in the config model). The surface's devices become an explicit list, each with
its name, type, ports, whether it is required, and its **control overrides**.
The plugin creates its devices from that list, defines JPRSurf's widgets over
them, and maps through widget paths and output mode names, rather than
`"XTouch/Fader3"` and mode numbers. That removes the device prefixes and the
device checks in the strip loops, and leaves *Build the scene from a
SurfaceSpec* only the plugin's own code to replace.

## Behavior

With both units, or with the X-Touch alone, nothing changes, and the surface
tests verify it.
- **The extender alone no longer loads.** The X-Touch is required, so without
  it the surface does nothing, and logs an error naming it. Today the extender
  alone gives 8 strips with no transport, modes, or navigation, which was
  never a useful surface. This is the config model's rule that a config
  written for some hardware never runs half working on other hardware.
- **The extender's third fader** still falls back to delayed feedback, as its
  touch sensing is broken. It is now a control override in JPRSurf's device
  list, rather than code in `DeviceXTouch`.

## Design

Most of it is in `device`, with no REAPER dependency apart from opening ports.
The scene then maps through widgets, and the plugin defines its devices and
widgets in C++ until the config fills in the same data.

Everything here runs once, when the surface is created, and nothing per run, so
there is no benchmark, and the call count bounds don't move.

### Names

| New name                                                  | Alternatives                                | Existing names it might be confused with                                |
| --------------------------------------------------------- | ------------------------------------------- | ----------------------------------------------------------------------- |
| `DeviceType`, `FindDeviceType()`                          | `DeviceKind`                                | `DeviceXTouch::Type` (full or extender), which becomes two device types |
| `ControlOverride`                                         | `UnusedIO`, `ControlRemoval`                | `ViewMapping`'s mode overrides                                          |
| `DeviceSpec`                                              | `DeviceEntry`, `DeviceConfig`               | `SurfaceSpec`, which will hold a list of them                           |
| `CreateDevices()`                                         | `OpenDevices()`                             | `CreateDevice()`, which it calls for each device present                |
| `WidgetRoot`                                              | `ShapedWidget`, `TopLevelWidget`            | `Scene::GetRootView()`, the views' equivalent                           |
| `ValidateControlOverrides()`, `ValidateDeviceControls()`  | `Check*`, which may not report a problem    | `ValidateWidgets()`, which they match                                   |
| `WidgetDoc`, `CatalogDoc`, `ValidateCatalogDoc()`         | `*Annotation`, `*Info`                      | `WidgetStructDefinition`, a config's widgets, not a catalog's           |
| `GenerateDeviceTypePage()`                                | `WriteDeviceTypeDoc()`                      | `CatalogDoc`, the data a page is generated from                         |

"Catalog", "device type", "control override", "required", and "optional" are
the config model's own words.

**Validate, not check:** a function that reports what is wrong is
`Validate*()`, as "check" doesn't say whether it judges anything. CL1 renames
`CheckWidgets()` to `ValidateWidgets()` to match (and `WidgetChecker` to
`WidgetValidator`).

**Device type names:** `XTouch` and `XTouchExt`. By convention type names are
capitalized, so they read differently from the names JPRSurf gives its
devices, which stay `xtouch` and `xtouch_ext`, as in the config model. Like
every name, neither is enforced beyond being a C identifier.

### Catalogs

```
class DeviceXTouch final : public Device {
 public:
  enum class Type { kFull, kExtender };

  // Returns the catalog of the X-Touch or its extender: a struct of its
  // controls, with its channel strips in the array `strip`. Both have the same
  // strips, so one join can take strips from each.
  static const WidgetShape& GetCatalog(Type type);
  ...
};
```

Both catalogs have a `strip` array of 8, of the same struct:

| Field                             | Inputs        | Outputs          | Modes                 |
| --------------------------------- | ------------- | ---------------- | --------------------- |
| `rec`, `solo`, `mute`, `select`   | press         | discrete value   | `solid`, `blink`      |
| `fader`                           | value, press  | continuous value |                       |
| `pot`                             | delta         | discrete value   | the ring's (below)    |
| `pot_button`                      | press         |                  |                       |
| `meter`                           |               | continuous value |                       |
| `scribble_top`, `scribble_bottom` |               | text             |                       |
| `scribble_color`                  |               | color            |                       |

The X-Touch's catalog adds, before `strip`:
- Every button but the strips', each with a press input and a light (`solid`,
  `blink`), named by its label in lower case: `assign_track`, `bank_left`,
  `show_time_beats`, `auto_read`, `play`, and so on. The function keys are an
  array, `function[0]` to `function[7]`, as an index is never part of a name.
- `master_fader`, as a strip's `fader`.
- `timecode`, a text output.
- `smpte_light`, `beats_light`, and `solo_light`, each a light (`solid`,
  `blink`).

The ring's modes, by today's mode numbers 0 to 8: `dot`, `center`, `fill`,
`spread`, `dot_edges`, `center_edges`, `fill_edges`, `spread_edges`, and `off`.
The `_edges` modes are the first four with the ring's outermost LEDs always
lit.

A control with one mode effectively has no modes, so it has no mode names, and
a mapping to it names no mode (mode 0). That is every output but the lights and
the rings.

`DeviceXTouch` names each control by its path in the catalog, such as
`strip[2].fader` or `play`, which is how `ResolveWidgets()` finds it. The names
are defined once, in the tables the catalog and the constructor are both built
from. Until the plugin maps through widgets, `DeviceXTouch` keeps the name
constants and functions the plugin uses (`kPlay`, `Fader()`), returning the new
names, so the plugin's `"XTouch/..."` names still work. The plugin's last use
of them goes in CL6, and they go with it. The plugin then names its widgets by
strings, as a config will, with no constants of its own.

### Catalog documentation

Someone writing a config needs every device type's controls, their inputs and
outputs, and their mode names, and the catalog code is no place to find them.
So each device type has a page generated from its catalog, and from
documentation kept beside the catalog for what the shape can't say, such as a
sentence on the hardware or what each ring mode looks like. The documentation
is new classes in `device`, so `WidgetShape` and `Widget` hold only what
behavior needs, and it is linked into the plugin like the catalogs, so a GUI
that builds configs for the devices present could use it at runtime.

```
// Documentation of one widget in a catalog.
struct WidgetDoc {
  struct Mode {
    std::string_view name;         // One of the control's mode names.
    std::string_view description;  // Such as "One LED lit at the value."
  };

  // Its path in the catalog by field names only, such as "strip.pot", as
  // every element of an array has the same shape, and so the same doc. An
  // array's own doc is at its path, such as "strip", which also documents its
  // element, if that is a control.
  std::string_view path;
  std::string_view description;  // Such as "The rotary encoder above the fader."

  // Every mode, for a control with modes. Controls with the same modes share
  // one array of them, which their page lists once.
  absl::Span<const Mode> modes;
};

// Documentation of a catalog: the hardware, and each widget.
struct CatalogDoc {
  std::string_view description;  // The hardware, and the ports it shows as.
  std::vector<WidgetDoc> widgets;
};

// Returns true if the doc has a description, and documents every widget in the
// catalog and every mode of each control with modes, each with a description,
// adding an error for each widget or mode that isn't documented, or has no
// description, and for each doc of a path or mode that names nothing or is
// repeated.
bool ValidateCatalogDoc(const WidgetShape& catalog, const CatalogDoc& doc,
                        std::vector<std::string>* errors);
```

- **In `device`, beside the catalog:** `widget_doc.h` has the classes and
  `ValidateCatalogDoc()`. `DeviceXTouch::GetCatalogDoc(Type)`, beside
  `GetCatalog(Type)`, returns each model's, from `device_xtouch_doc.cc`, so its
  descriptions don't bury the catalog code. `DeviceType` gets a `catalog_doc`
  beside its `catalog`, so every type has one.
- **Every widget is documented:** `device_types_test.cc` validates each type's
  doc beside its controls, so adding a control without documenting it fails
  `device`'s own tests, and no name is left only in the catalog code.
- **Pages are generated in `src/jpr/doc/` (`jpr_doc`):** a library that may use
  any other, and that nothing uses, so it is outside the order of the
  libraries, as the `testing/` libraries are, and later pages (such as the
  scene's properties) can go there too. It has `GenerateDeviceTypePage(const
  DeviceType&)`, which returns the type's page in markdown, the executable the
  `update_device_docs` target runs to write every type's page, and the test of
  the pages.
- **Where the pages live:** the user docs move apart from the developer docs,
  into `docs/user/`: `user_guide.md` and its screenshots (`docs/user/images/`),
  with the README's link to match, and a page for each device type in
  `docs/user/devices/`, named by the type (`XTouch.md`, `XTouchExt.md`). The
  developer docs stay where they are, as CLAUDE.md, the worklogs, and each
  other link to them.
- **A page:** the type's description, then a table of its catalog in order,
  with each widget's path, inputs, outputs, mode names, and description. An
  array is listed once, as `strip[0..7]`, with its element's fields below it as
  `strip[i].fader`. Then a table of each set of modes, with their
  descriptions, headed by the mode names and listing the controls that share
  its array, as every light on the X-Touch has the same two modes.
- **Published as checked-in markdown,** so the pages read on GitHub without a
  build. Generating them in every build would either leave them out of the
  repository or dirty the tree.
- **The test** compares each type's checked-in page with what
  `GenerateDeviceTypePage()` returns, ignoring line endings. It finds the pages
  by a compile definition of `docs/user/devices`, and a page that differs fails
  with the target to run.

### Control overrides

```
// Inputs and outputs of one control that a unit doesn't use, such as a fader
// whose touch sensing is broken.
struct ControlOverride {
  std::string control;      // Its path in the catalog, such as "strip[2].fader".
  Control::Inputs inputs;   // The inputs it doesn't use.
  Control::Outputs outputs; // The outputs it doesn't use.
};

// Returns true if every override names a control in the catalog, and only
// inputs and outputs that control has, adding an error for each that doesn't.
bool ValidateControlOverrides(const WidgetShape& catalog,
                           absl::Span<const ControlOverride> overrides,
                           std::vector<std::string>* errors);
```

- `Device` takes the overrides when it is constructed, and a derived device
  asks `Device::IsUsed()` before it builds each input and output, so nothing
  the unit doesn't use is built (a MIDI input subscribes to its port as it is
  built). `DeviceXTouch` passes them through, and stops leaving out the
  extender's third fader's touch. A device type that ignores an override
  builds a control that doesn't match its catalog less the override, which
  `CreateDevice()` logs, and its type's test fails.
- A motorized fader without a press input already falls back to delayed
  feedback, so nothing else changes.

### Device types

```
// A kind of hardware JPRSurf supports, by name.
struct DeviceType {
  std::string_view name;  // Such as "XTouch".

  // The type's controls: a struct shape, which never changes.
  const WidgetShape* catalog;

  // The catalog's documentation (from CL3), which behavior never reads.
  const CatalogDoc* catalog_doc;

  // Creates the device on its ports, with its controls named by their paths
  // in the catalog, apart from the overrides' inputs and outputs.
  std::unique_ptr<Device> (*create)(RunRegistry& run_registry, MidiIn* midi_in,
                                    MidiOut* midi_out,
                                    absl::Span<const ControlOverride> overrides);
};

// Returns every device type, or the one with the name (or null).
absl::Span<const DeviceType> GetDeviceTypes();
const DeviceType* FindDeviceType(std::string_view name);

// Creates a device of the type, and checks once that the controls it built
// match the catalog exactly, apart from the overrides, logging an error for
// each that doesn't.
std::unique_ptr<Device> CreateDevice(const DeviceType& type, ...);

// Returns true if the device's controls match the catalog, apart from the
// overrides: every control in the catalog, and nothing else, each with the
// catalog's inputs and outputs less its override, and as many modes as the
// catalog names (or one, if it names none).
bool ValidateDeviceControls(const Device& device, const WidgetShape& catalog,
                            absl::Span<const ControlOverride> overrides,
                            std::vector<std::string>* errors);
```

- The registry is a fixed list in `device_types.cc`, which knows every type.
  Nothing else registers types, so there is no registration to get wrong.
- A catalog can't be validated by a device that is never created, so a unit
  test for each type creates it on ports of the fake REAPER's, with nothing
  behind them, checks `ValidateDeviceControls()` finds no errors, and checks
  `ValidateWidgets()` finds none in its catalog (building a shape never fails,
  so a bad or repeated name in a catalog only shows up there).
- An override that drops an output can lower that control's mode count, so a
  control with an output override may have fewer modes than its catalog names.

### The device list

```
// One device of the surface. Nothing is inferred from its type or from the
// ports that exist, so several units of one type are several entries.
struct DeviceSpec {
  std::string name;  // As widget paths start with it, such as "xtouch_ext".
  std::string type;  // A device type, such as "XTouchExt".
  std::string input_port;   // As REAPER lists it, such as "X-Touch-Ext".
  std::string output_port;
  bool required = true;
  std::vector<ControlOverride> overrides;
};

// The devices created from a device list.
struct DeviceList {
  std::vector<std::unique_ptr<Device>> devices;  // Those present, in order.

  // A device for each spec, in order, to resolve widgets on: its name, its
  // type's catalog, and its device, or null if it is absent.
  std::vector<WidgetDevice> widget_devices;
};

// Creates each device whose ports are both found, and open. Returns nullopt,
// opening no ports, with an error for each problem, if a type or an override
// is invalid, a port is also another spec's, a name isn't a widget name or is
// repeated, or a required device is absent, and nullopt if a required
// device's ports fail to open.
std::optional<DeviceList> CreateDevices(absl::Span<const DeviceSpec> specs,
                                        MidiPorts& ports,
                                        RunRegistry& run_registry,
                                        std::vector<std::string>* errors);
```

- They are in a new `device_spec.h`.
- Devices are found once, when the surface is created, as they are today.
- `MidiPorts::HasInput()` and `HasOutput()` find a port without opening it, so
  a list that fails opens no ports, and a device with only one of its ports
  found leaves that one closed, as every open port is polled each run.
- A port that is found can still fail to open, as when another program has it,
  which the fake's ports can be set to do (`SetOpenFails()`). Until the ports
  are handles (see below), the other port of that device, and the ports a list
  that fails opened, stay open.
- Each port is one device's, so a port in two specs is an error.
- The list gives the `WidgetDevice`s to resolve widgets on, so the caller
  needn't look up the types again, or pair specs with devices. Their names are
  validated by `ValidateWidgets()` on them, the one place the rule is.
- `CreateDevice()` takes valid overrides, as `CreateDevices()` validates them.
  Until CL8, `PluginSurface` validates its own.
- It is unit tested on the fake REAPER's MIDI ports and the fake X-Touch: both
  present, an optional device absent, a required device absent, a port that
  fails to open on an optional and a required device, and each invalid spec.

### MIDI port handles

`MidiPorts::OpenInput()` and `OpenOutput()` return a move-only handle, which
closes the port when it goes, so nothing can close a port out from under its
other users, or forget to close one:

```cpp
// An open MIDI input or output port, which stays open while any handle to it
// lives. Null if the port isn't found, or fails to open.
class MidiInHandle;
class MidiOutHandle;
```

- `MidiPorts` counts the handles on each port, and closes it as the last one
  goes. Opening a port that is already open gives another handle to the same
  port. There is no other way to close a port.
- The device holds its handles: `DeviceType::create`, `CreateDevice()` and
  `DeviceXTouch` take them, and `DeviceXTouch` keeps them. Not every device is
  MIDI, so the `Device` base has none. The ports close before `Device` destroys
  the controls, which only unsubscribe from the input as they go, as a closed
  port allows. Controls still borrow the raw `MidiIn*` and `MidiOut*`.
- `CreateDevices()` then closes a port whose device is absent, and every port
  of a list that fails, by dropping their handles, with no code of its own.
- An output port closes only once it is also done sending: nothing queued,
  and nothing sent within the flush wait (100ms), so what it sent reaches the
  hardware. Until then its runs send what is queued, and close it once the
  wait has passed, and destroying `MidiPorts` sends the rest and waits, as
  today. So the surface still clears on shutdown, though the scene's devices
  go before the ports do. An input closes as its last handle goes.
- A handle must not outlive its `MidiPorts`: destroying `MidiPorts` with a
  handle left logs an error, which fails a test under the fake.

### The widget root

`ResolveWidgets()` returns the top-level struct with its shape, as
`ValidateWidgets()` returns the shape, so the scene can tell a path that names
nothing from one whose device is absent, and look up mode names on an absent
control:

```
// The config's widgets: the top-level struct, and its shape, which has every
// widget the catalogs give, whether or not its device is present.
struct WidgetRoot {
  WidgetShape shape;
  std::unique_ptr<const Widget> widget;
};
std::optional<WidgetRoot> ResolveWidgets(...);
```

The shape can't live on each `Widget` instead: a widget of an absent device's
control has a null control, and no catalog to give its mode names, and a join
leaves out the elements of absent devices, which the shape keeps.

### The scene maps through widgets

```
class Scene {
 public:
  // Creates a scene on the devices, which it keeps for its lifetime, and the
  // widgets on them, which mappings name controls by.
  Scene(std::string_view name, std::vector<std::unique_ptr<Device>> devices,
        WidgetRoot widgets, TrackFilter track_filter = TrackFilter::kMcp);
  ...
};

// View::AddMapping() takes a widget path, such as "strip[3].fader", instead of
// "<device>/<control>".
```

- The scene's hardware is given when it is created, so no mapping can be added
  before its widgets exist, and nothing replaces them after. A
  `Scene::SetWidgets()` that may only be called once would do as well, but
  `SceneTest` would still have to set them between a test's controls and its
  mappings, the same work as creating the scene then. `Scene::AddDevice()`
  goes. The scene's other `Add*()` functions stay until *Build the scene from
  a SurfaceSpec*. The devices and widgets default to none (a `WidgetRoot`
  defaults to an empty struct), for the property tests that need only a
  scene.
- `WidgetRoot::FindControl()` finds the control at a path's text: its shape,
  and the device's control, or null if its device is absent. The scene maps
  through it, and CL7 looks up mode names in the shape it gives. It and
  `ValidateControlOverrides()` share `FindControlShape()`, so a path's errors
  read the same in a mapping and an override.
- A path the shape has, but whose control is absent, adds a mapping that does
  nothing, and returns true: the model's rule that a mapping to an absent
  widget does nothing. It is validated as any mapping is (its properties and
  its condition), and then not added, as it would never read or write. A path
  the shape doesn't have, or that isn't a control, returns false and logs why,
  as a missing control does today.
- `Scene::GetControl()` takes a widget path.
- `SceneTest` creates its scene the first time a test calls `GetScene()`,
  which replaces `scene_`, on its fake device and a struct of the device's
  controls, by their names. Adding a control once the scene exists fails the
  test. Every `scene_.` in the scene tests (about 90) becomes `GetScene()->`.
  Most tests already add their controls before their properties and mappings,
  but `ViewMappingTest`'s didn't: it adds its view on first use
  (`GetView()`), and its tests add their controls first, `AddReader()` taking
  a control already added.
- The plugin's strip loops run over the strips present (the elements of
  `strip` and `route_strip`), not the shape's size, so the track list has as
  many items as there are strips, as today.

### Output modes by name

`ViewMapping::WriteConfig::mode` and the modes in `ModeOverride` become names
(`"blink"`, `"off"`), and an empty name is mode 0. `AddMapping()` looks each up
in the control's mode names in the shape, and fails, logging why, on a name the
control doesn't have. As the shape is there for an absent control, its modes are
validated too.

A timeline position written to text is the exception: the mode is its format
(`ControlTextOutput::SetTimelineText()`), which isn't one of the control's
modes. Its names are `ViewMapping::kTimelineFormats` (`beats`, `time`,
`frames`, and `samples`, each a `TimelineMode`), and an empty name is the
ruler's format, mode 0. `ControlTextOutput::GetTimelineTextMode()` is the one
place a `TimelineMode` becomes a mode, and `ViewMapping::WritesTimelineText()`
the one place a mapping is found to write one. `ViewMapping::FindMode()` has
both rules, and `AddMapping()` passes the numbers to the mapping, with only its
read config, so the mapping keeps no names.

`SceneTest`'s fake catalog names the modes of a control with more than one by
number, `mode0`, `mode1`, and so on (`SceneTest::GetModeName()`).

### JPRSurf's devices and widgets

The plugin's device list is the config model's:

| Device       | Type              | Ports         | Required | Overrides                   |
| ------------ | ----------------- | ------------- | -------- | --------------------------- |
| `xtouch`     | `XTouch`          | `X-Touch`     | yes      |                             |
| `xtouch_ext` | `XTouchExt`       | `X-Touch-Ext` | no       | `strip[2].fader`: no press  |

Its widgets are the config model's: the X-Touch's fields apart from `strip`,
then `strip` (the extender's strips, then the X-Touch's), `route_strip` (the
extender's strips, then the X-Touch's first 7), and `info_strip` (the X-Touch's
last strip). The strip loops run over the size of `strip` and `route_strip`,
with paths such as `strip[9].mute`, and there are no device prefixes or device
checks left.

### To confirm

Both checked by the user on the hardware, with no code.
1. **What REAPER lists identical units' ports as.** They differ: the X-Touch is
   `X-Touch`, the first extender `X-Touch-Ext`, and the second
   `2 - X-Touch-Ext`, the same in REAPER and in MIDI-OX, so Windows likely
   names them. So explicit port names work, and CL4 is unchanged. Which unit
   gets which name seems to follow the order they are powered on (unconfirmed),
   and nothing in the ports tells identical units apart, so a surface with two
   of them relies on powering them on in the same order.
2. **What the ring's modes 4 to 7 show.** As `McuEncoder()`'s comment says:
   modes 0 to 3 with the outermost LEDs always lit.

## CLs

### CL1 [x] device: The X-Touch's catalogs, and controls named by them

Depends on: nothing.

- `CheckWidgets()` becomes `ValidateWidgets()`, and `WidgetChecker`
  `WidgetValidator`, with the Widgets summary to match.
- `DeviceXTouch::GetCatalog()` for the X-Touch and the extender.
- `DeviceXTouch` names its controls by their catalog paths, from the same
  tables the catalog is built from. It keeps only the name constants and
  functions the plugin uses, returning the new names, so the plugin is
  unchanged, and `device_xtouch_test.cc` names controls by their paths
  instead.
- `device_xtouch_test.cc` validates each catalog with `ValidateWidgets()`,
  checks that both models' strips are the same, and resolves the whole catalog
  on each model to check that every control is there, with its catalog entry's
  inputs, outputs, and mode count (the extender's third fader apart, until
  CL2), and that there are no others.
- No visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- The tests above.

### CL2 [x] device: Control overrides and device types

Depends on: CL1.

- `ControlOverride` and `ValidateControlOverrides()`. `Device` takes
  overrides, and `IsUsed()` tells a derived device what not to build.
  `DeviceXTouch` takes them, and stops leaving out the extender's third fader's
  touch.
- `DeviceType`, `GetDeviceTypes()`, `FindDeviceType()`, `CreateDevice()`, and
  `ValidateDeviceControls()`, in a new `device_types.h`.
- The plugin creates its two devices with `CreateDevice()`, passing the
  extender's override, so its fader behaves as it does today. This is the only
  plugin change.
- `device_test.cc` tests overrides on a device of fake controls, and
  `device_types_test.cc` creates each type on the fake ports and checks it
  against its catalog, replacing CL1's check.

**Verify**
- Standard checks.
- The tests above, and `track_strip_test.cc`'s test of the extender's third
  fader still passes.

### CL3 [x] device, doc: Documentation of the device types

Depends on: CL2, for the device types it documents.

- `WidgetDoc`, `CatalogDoc`, and `ValidateCatalogDoc()`, in `device`'s
  `widget_doc.h` (see Catalog documentation). `DeviceXTouch::GetCatalogDoc()`
  documents both models, and `DeviceType` gets its `catalog_doc`.
- The `jpr_doc` library in `src/jpr/doc/`, with `GenerateDeviceTypePage()`,
  the `update_device_docs` target, and the pages it writes,
  `docs/user/devices/XTouch.md` and `XTouchExt.md`. CLAUDE.md's directory
  structure and list of libraries gain it, as outside their order.
- `user_guide.md` and its images move to `docs/user/` together, so its image
  links still work, and the README's link and CLAUDE.md's directory structure
  move to match. The guide links to the device pages.
- `widget_doc_test.cc` tests `ValidateCatalogDoc()` on a hand-built catalog,
  and `device_types_test.cc` validates each type's doc. `jpr_doc`'s tests check
  a page of a hand-built type, and compare each type's checked-in page.

**Verify**
- Standard checks.
- The tests above, and that they fail when a control is added without a doc,
  and when a catalog or a doc changes without its page.
- The pages and the moved guide read correctly, with working links.

### CL4 [x] common, device: The device list, and the widget root

Depends on: CL2.

- `DeviceSpec`, `DeviceList`, and `CreateDevices()`, and in `common`,
  `MidiPorts::HasInput()` and `HasOutput()`, with the fake's ports able to fail
  to open.
- `ResolveWidgets()` returns a `WidgetRoot`.
- Unused, so no visible change.

**Verify**
- Standard checks, and the contract tests, for the change to the fake.
- `CreateDevices()` on the fake REAPER's ports, with the cases above.

### CL5 [x] common, device: MIDI port handles

Depends on: CL4.

- `MidiPorts` opens ports as handles, and closes each as its last handle goes
  (see MIDI port handles).
- Devices hold their ports' handles, and `CreateDevices()` closes the ports of
  absent devices and of a list that fails.
- The plugin passes the handles it opens to `CreateDevice()`, until CL8.
- No visible change.

**Verify**
- Standard checks.
- `MidiPorts` tests of two handles on a port, the port closing with the last,
  a moved handle, a released output closing once it is done sending, queued
  output sent as `MidiPorts` goes, and a handle left as it goes.
- `CreateDevices()` tests that the ports of an absent device, and of a list
  that fails, are closed.
- The surface tests that the surface clears on shutdown, and the load check.

### CL6 [x] scene, plugin: Map through widgets

Depends on: CL4.

- The scene takes its devices and widget root when it is created, in place of
  `Scene::AddDevice()`, and `View::AddMapping()` and `Scene::GetControl()` take
  widget paths, with absent widgets as above. `SceneTest` creates its scene on
  first use, from its fake device.
- The plugin defines JPRSurf's widgets over the devices it creates as it does
  today, and maps through widget paths. Its strip loops run over the widget
  arrays, still behind the device checks, which CL8 removes.
- `DeviceXTouch`'s name constants and functions go.
- This crosses into `plugin` because the scene's mapping API changes under it.

**Verify**
- Standard checks.
- Scene tests of mapping to an absent widget, to a path that names nothing, and
  to a struct. The surface tests verify the plugin.

### CL7 [x] device, scene, plugin: Output modes by name

Depends on: CL6.

- Mode names in `WriteConfig` and `ModeOverride`, looked up in the shape.
- The plugin names its modes (`blink`, `center`, `center_edges`, `off`).
- Timeline formats by name (`ViewMapping::kTimelineFormats`), with the format's
  mode from `ControlTextOutput::GetTimelineTextMode()` in `device`.

**Verify**
- Standard checks.
- Scene tests of a mode by name, an override by name, a name the control
  doesn't have, and a name on an absent control. The surface tests verify the
  plugin.

### CL8 [ ] plugin: Devices from the list

Depends on: CL7.

- JPRSurf's device list, created with `CreateDevices()`, with the X-Touch
  required and the extender optional. Without the X-Touch, the plugin logs the
  error and adds no scene, so the surface does nothing.
- The device checks around the strip loops and the global mappings go, as the
  widgets already shrink with what is present.

**Verify**
- Standard checks.
- A surface test that the extender alone does nothing, and the existing surface
  tests with both units and with the X-Touch alone.
- The load check.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow).

## Checks in REAPER

In the user's REAPER, at the end of the feature:
1. With both units: every strip, button, light, ring, and display works as
   before, with the extender's strips to the left.
2. The extender's third fader still follows REAPER after a short delay when
   moved, as it has no touch.
3. With the X-Touch's USB unplugged at startup, the surface does nothing, and
   `jprsurf.log` names the X-Touch as absent.
4. The extension loads with no new errors in `jprsurf.log`, "Slow run"
   warnings included. No idle profile, as nothing per run changes.
