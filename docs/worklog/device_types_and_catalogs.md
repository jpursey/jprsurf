# Device types and catalogs

Each kind of hardware JPRSurf supports is a **device type**, by name: a
**catalog** of its controls, as a struct widget shape, the catalog's
documentation, and a factory that creates the device on its ports (see
[Devices](../user/config_spec.md#devices) in the config spec). The
surface's devices are an explicit list, each with its name, type, ports,
whether it is required, and its **control overrides**. The plugin creates its
devices from that list, defines JPRSurf's widgets over them, and maps through
widget paths and output mode names, rather than `"XTouch/Fader3"` and mode
numbers. There are no device prefixes or device checks left in the plugin, so
*Build the scene from a SurfaceSpec* has only the plugin's own code to replace.

## Behavior

With both units, or with the X-Touch alone, nothing changed, and the surface
tests verify it.
- **The extender alone does nothing.** The X-Touch is required, so without it
  the plugin logs why (`xtouch: is required, but its input port "X-Touch"
  isn't found`) and adds no scene. The extender alone used to give 8 strips
  with no transport, modes, or navigation, which was never a useful surface.
  This is the config model's rule that a config written for some hardware
  never runs half working on other hardware.
- **The extender's third fader** still falls back to delayed feedback, as its
  touch sensing is broken. It is a control override in JPRSurf's device list,
  rather than code in `DeviceXTouch`.
- **Each device type has a page** in `docs/user/devices/` (`XTouch.md`,
  `XTouchExt.md`), generated from its catalog and documentation, which the
  user guide links to. The guide and its images moved to `docs/user/`.

Everything here runs once, when the surface is created, and nothing per run, so
there was no benchmark, and the call count bounds didn't move.

### Names

- Device types are capitalized (`XTouch`, `XTouchExt`), so they read
  differently from the names JPRSurf gives its devices (`xtouch`,
  `xtouch_ext`). Like every name, neither is enforced beyond being a C
  identifier.
- Each control is named by its path in its device's catalog, such as
  `strip[2].fader` or `play`, which is how `ResolveWidgets()` finds it. The
  X-Touch's buttons are named by their labels in lower case (`assign_track`,
  `show_time_beats`), and the function keys are an array, `function[0]` to
  `function[7]`, as an index is never part of a name.
- Output modes have names. Every light has `solid` and `blink`, and a ring has
  `dot`, `center`, `fill`, `spread`, the same four with `_edges` (the outermost
  LEDs always lit), and `off`. A control with one mode has no mode names, and a
  mapping to it names none.
- A timeline position written to text names its format instead, as it isn't
  one of the control's modes: `beats`, `time`, `frames`, or `samples`, or
  nothing for the ruler's.
- A function that reports what is wrong is `Validate*()`, not `Check*()`, as
  "check" doesn't say whether it judges anything (`CheckWidgets()` became
  `ValidateWidgets()`).

### Validating

Every problem is found, rather than the first, and each error starts with what
it is about, as `ValidateWidgets()`'s do.
- **A catalog** is validated by `ValidateWidgets()` (building a shape never
  fails, so a bad or repeated name only shows up there), and **a device** by
  `ValidateDeviceControls()`: every control in its catalog, and nothing else,
  each with the catalog's inputs and outputs less its override, and as many
  modes as the catalog names (or one, if it names none). An override that drops
  an output may lower a control's mode count. `CreateDevice()` validates each
  device it creates, logging an error for each mismatch, and each type's test
  does the same on the fake REAPER's ports.
- **A catalog's documentation** must document every widget, and every mode of
  each control with modes, each with a description, and nothing the catalog
  doesn't have (`ValidateCatalogDoc()`). Each type's test validates its doc, so
  adding a control without documenting it fails `device`'s own tests, and
  `jpr_doc_test` fails when a checked-in page is out of date.
- **An override** must name a control in the catalog, at most once, and drop
  something, but only inputs and outputs that control has
  (`ValidateControlOverrides()`).
- **A device list** fails, opening no ports, if a type isn't a device type, an
  override isn't valid, a port is also an earlier spec's, a name isn't a widget
  name or is repeated, or a required device's port isn't found, and fails, with
  its ports closed again, if a required device's ports fail to open.
- **A mapping** fails, logging why, on a mode name its control doesn't have. A
  mapping to an absent control is validated as any other (its path, modes,
  properties, and condition), and then not added, as it would never read or
  write: the model's rule that a mapping to an absent widget does nothing.

### MIDI ports

A port stays open while any handle to it lives, and closes as the last goes, so
nothing can close a port out from under its other users, or forget to close
one. Opening a port that is already open gives another handle to the same port.
An output port closes only once it is done sending (nothing queued, and nothing
sent within the flush wait), so the surface still clears on shutdown, though the
scene's devices go before the ports do. A handle left as `MidiPorts` goes logs
an error.

## Structure

### device

- **`widget_shape.h`:** `FindControlShape()` finds the shape of the control at a
  path, with the errors a mapping and an override share.
- **`widget_definition.h`:** `ResolveWidgets()` returns a `WidgetRoot`: the
  top-level struct, and its shape, which has every widget whether or not its
  device is present, so a mapping can tell a path that names nothing from one
  whose device is absent, and look up mode names on an absent control.
  `WidgetRoot::FindControl()` gives a path's shape, and its control, or null.
- **`control_override.h`:** `ControlOverride` (a control's catalog path, and the
  inputs and outputs it doesn't use) and `ValidateControlOverrides()`. `Device`
  takes the overrides, and a derived device asks `IsUsed()` before it builds
  each input and output, so nothing the unit doesn't use is built.
- **`device_types.h`:** `DeviceType` (a name, a catalog, its doc, and a
  `create` function on MIDI port handles), `GetDeviceTypes()`,
  `FindDeviceType()`, `CreateDevice()`, and `ValidateDeviceControls()`. The
  types are a fixed list in `device_types.cc`, which knows every type, so there
  is no registration to get wrong.
- **`device_spec.h`:** `DeviceSpec` (a name, a type, its ports, whether it is
  required, and its overrides), `DeviceList` (the devices present, and a
  `WidgetDevice` for each spec, null if absent), and `CreateDevices()`. Nothing
  is inferred from a type or from the ports that exist, so several units of one
  type are several entries.
- **`widget_doc.h`:** `WidgetDoc` (a field-only path, such as `strip.pot`, a
  description, and its modes' descriptions, which controls with the same modes
  share), `CatalogDoc`, and `ValidateCatalogDoc()`. The doc is linked into the
  plugin beside the catalog, so a GUI could use it at runtime.
- **`device_xtouch.h`:** `GetCatalog(Type)` and `GetCatalogDoc(Type)` for the
  X-Touch and the extender, with the descriptions in `device_xtouch_doc.cc`.
  The catalog and the controls are built from the same tables. Both models have
  the same `strip` array, so one join can take strips from each.
- **`control_output.h`:** `ControlTextOutput::GetTimelineTextMode()`, the one
  place a `TimelineMode` becomes a text output's mode.

### common

`MidiPorts::HasInput()` and `HasOutput()` find a port without opening it, and
`OpenInput()` and `OpenOutput()` return a `MidiInHandle` or `MidiOutHandle`
(`MidiPortHandle` in `midi_port.h`). The fake's ports can fail to open
(`SetOpenFails()`).

### scene

- **`Scene`** is created on its devices, which it keeps, and their
  `WidgetRoot`, so no mapping is added before its widgets exist, and nothing
  replaces them. `View::AddMapping()` and `Scene::GetControl()` take widget
  paths.
- **`ViewMapping`:** `WriteConfig::mode` and `ModeOverride`'s modes are names.
  `FindMode()` has the rule for both the control's modes and the timeline
  formats (`kTimelineFormats`), and `WritesTimelineText()` is the one place a
  mapping is found to write a timeline position. The mapping keeps only numbers.
- **`SceneTest`** creates its scene the first time a test calls `GetScene()`,
  on its fake device and a struct of its controls, and fails a test that adds a
  control after. It names a control's modes `mode0`, `mode1`, and so on
  (`GetModeName()`). `ViewMappingTest` adds its view on first use.

### plugin

`PluginSurface` has JPRSurf's device list (`GetDeviceSpecs()`) and widgets
(`GetWidgetDefinition()`): the X-Touch's fields apart from `strip`, then
`strip` (the extender's strips, then the X-Touch's), `route_strip` (the
extender's strips, then the X-Touch's first 7), and `info_strip` (the X-Touch's
last strip). Its strip loops run over the strips present, and `scene_` is null
if the devices weren't created.

### doc

`jpr_doc` (`src/jpr/doc/`) generates documentation from the code, and may use
any library, while nothing uses it, so it is outside the libraries' order.
`GenerateDeviceTypePage()` gives a type's page: its description, a table of its
catalog (an array once, as `strip[0..7]`, with its element's fields as
`strip[i].fader`), and a table of each set of modes. The `update_device_docs`
target writes every type's page, and the pages are checked in, so they read on
GitHub without a build.

## Building blocks

- **A new device type:** a catalog, a doc, a `create` function, and an entry in
  `device_types.cc`. `device_types_test.cc` then validates its controls and its
  doc on the fake REAPER's ports, and building `update_device_docs` writes its
  page.
- **Unused inputs and outputs** of one unit are a `ControlOverride` in its
  spec, not code: a derived device only has to ask `IsUsed()`.
- **One shape for everything a path addresses:** `WidgetRoot`'s shape has the
  absent devices' widgets too, so validation by path and mode name works the
  same whether or not the hardware is connected.
- **Port handles:** anything that needs a port holds a handle, and the port
  closes with the last, with no close calls to get wrong.
- **Generated reference pages:** `jpr_doc`, with a test that compares each
  checked-in page with what the code generates, is the pattern for later pages
  (such as the scene's properties).

## Checked facts

Checked by the user on the hardware:
- **What REAPER lists identical units' ports as:** the X-Touch is `X-Touch`,
  the first extender `X-Touch-Ext`, and the second `2 - X-Touch-Ext`, the same
  in REAPER and in MIDI-OX, so Windows likely names them. Which unit gets which
  name seems to follow the order they are powered on (unconfirmed), and nothing
  in the ports tells identical units apart.
- **What the ring's modes 4 to 7 show:** modes 0 to 3 with the outermost LEDs
  always lit.

## Left for later

In the backlog: *Tell identical units apart*, *Paths into the config's own
widget arrays* (for an `info_strip` on whatever unit is rightmost), and a page
for each property namespace, with *Build the scene from a SurfaceSpec*.
