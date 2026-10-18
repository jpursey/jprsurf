# Widgets

Widgets are the names mappings will use for hardware, without saying which
device a control is on (see [Widgets](../config_model.md#widgets) in the config
model). A widget is a control, a struct of named widgets, or an array of
widgets of the same shape. A config defines its widgets from paths through the
devices' catalogs, and they resolve to the `Control`s of the devices that are
present, so `strip[3].fader` is the fourth fader on the surface whether or not
the extender is connected.

This feature adds widgets to `device` as plain data, unit tested against
hand-built shapes. Nothing uses them yet: device catalogs, and the plugin
mapping through widgets, are *Device types and catalogs*. There is no change in
behavior.

## Design

Everything is in `device`, with no REAPER dependency, in four parts: paths,
shapes, definitions (checked against shapes), and widgets resolved on devices.
Checking needs no devices, so a config can be checked without hardware, and
resolving checks first, so nothing resolves a definition that doesn't check.

Everything here runs once, when the widgets are resolved, and nothing per run.

### Names

| New name                                                                  | Alternatives     | Existing names it might be confused with                         |
| ------------------------------------------------------------------------- | ---------------- | ---------------------------------------------------------------- |
| `Widget`                                                                  | `ResolvedWidget` | Game Bits' `gb/imgui` widgets, which JPRSurf doesn't use         |
| `WidgetShape`, `WidgetKind`                                               | `WidgetType`     | Device types, and `ControlInput::Type` and `ControlOutput::Type` |
| `WidgetPath`                                                              | `WidgetAddress`  | Property names, which have a namespace, so never look like paths |
| `WidgetFieldDefinition`, `WidgetStructDefinition`, `WidgetJoinDefinition` | `WidgetSpec`     | `SurfaceSpec`, which will hold the definitions                   |
| `WidgetSlice`, `WidgetInclude`                                            | `WidgetRange`    | Read ranges (`ReadConfig::property_min` and `property_max`)      |
| `WidgetDevice`                                                            | `NamedDevice`    | `Device`, which it refers to                                     |
| `CheckWidgets()`, `ResolveWidgets()`                                      | `BuildWidgets()` | The spec's checking and building, which will call them           |

"Shape", "slice", "include", and "joined" are the config model's own words.

### Paths

```
// A path to a widget from another widget: field names and array indices,
// such as `strip[3].fader`. Indices start from 0. An empty path is the widget
// itself.
class WidgetPath {
 public:
  // A field name or an array index.
  using Step = std::variant<std::string, int>;

  // Returns the path, or nullopt if the text isn't one: a step that is empty,
  // a field that isn't a widget name, or an index that isn't a number.
  static std::optional<WidgetPath> Parse(std::string_view text);

  absl::Span<const Step> GetSteps() const;

  // The path's text, as Parse() reads it.
  std::string ToString() const;
};

// Returns true if the name is a widget name: lower case words of letters and
// digits, joined by single underscores, starting with a letter.
bool IsWidgetName(std::string_view name);
```

- Widget names follow the config model's conventions, so a name that would
  break them can never get into a catalog or a config.

### Shapes

```
enum class WidgetKind { kControl, kStruct, kArray };

// The shape of a widget: what a mapping can use a control for, a struct's
// fields and their shapes, or an array's size and the shape of every element.
class WidgetShape {
 public:
  // A field of a struct shape: { std::string name; WidgetShape shape; },
  // defined after the class.
  struct Field;

  static WidgetShape ForControl(Control::Inputs inputs,
                                Control::Outputs outputs,
                                std::vector<std::string> mode_names = {});
  // A field whose name is taken is left out, logging an error.
  static WidgetShape ForStruct(std::vector<Field> fields);
  static WidgetShape ForArray(WidgetShape element, int size);

  WidgetKind GetKind() const;

  // For a control, its inputs, its outputs, and the names of its output
  // modes, by mode number. Empty for any other kind.
  Control::Inputs GetInputs() const;
  Control::Outputs GetOutputs() const;
  absl::Span<const std::string> GetModeNames() const;

  // For a struct, its fields in order, and the field with a name, or null if
  // it has none.
  absl::Span<const Field> GetFields() const;
  const WidgetShape* GetField(std::string_view name) const;

  // For an array, its size, and the shape of its elements.
  int GetSize() const;
  const WidgetShape* GetElement() const;

  // Returns the shape at the path from this one, or null if there is no
  // widget there.
  const WidgetShape* Find(const WidgetPath& path) const;

  // Equal shapes are the same kind, and: controls with the same inputs,
  // outputs, and mode names; structs with the same field names, each of an
  // equal shape, in any order; or arrays of the same size with equal element
  // shapes.
  friend bool operator==(const WidgetShape& a, const WidgetShape& b);
};
```

- A device catalog (*Device types and catalogs*) is a struct shape, built in
  C++ with these.
- A control's inputs, outputs, and mode names are on `WidgetShape` itself,
  with no type of their own, and no kind has one: each is two or three values
  of distinct types. A shape from `ForControl()` is already the value a
  catalog reuses for every control of a kind, such as a button with a light.
- Shapes match exactly, as the model says, apart from the order of a struct's
  fields, which a mapping never sees. Field order is kept, for errors and
  listings.

### Definitions and checking

```
// A slice of an array: `count` elements from `first`, or the rest of the
// array if `count` isn't set.
struct WidgetSlice {
  std::string path;  // Through a device, such as "xtouch.strip".
  int first = 0;
  std::optional<int> count;
};

// The fields of the struct at a path through a device, apart from the ones
// it names.
struct WidgetInclude {
  std::string path;
  std::vector<std::string> excluded;
};

struct WidgetFieldDefinition;

// A struct: the fields of its includes, in order, then its own.
struct WidgetStructDefinition {
  std::vector<WidgetInclude> includes;
  std::vector<WidgetFieldDefinition> fields;
};

// A joined array: the slices' elements, one after another.
struct WidgetJoinDefinition {
  std::vector<WidgetSlice> slices;
};

// A field of a struct, and how it is defined: the widget at a path through a
// device (a control, a struct, or an array), a new struct, or a joined array.
struct WidgetFieldDefinition {
  std::string name;
  std::variant<std::string, WidgetStructDefinition, WidgetJoinDefinition>
      widget;
};

// A device that paths can start at.
struct WidgetDevice {
  std::string name;  // As paths start with it, such as "xtouch".

  // The device type's catalog: a struct shape, which must outlive the call.
  const WidgetShape* catalog = nullptr;
};

// Checks the config's widgets, the fields of its top-level struct, against
// the devices' catalogs. Returns the struct's shape, or nullopt, with an
// error added for each problem found.
std::optional<WidgetShape> CheckWidgets(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices, std::vector<std::string>* errors);
```

- **Every path in a definition starts at a device**: `xtouch.play`, or
  `xtouch.strip[7]`, which is JPRSurf's `info_strip`. A path can name any
  widget, so a path to a device's array is an array.
  - The config model also lets a join slice the config's own arrays. Leaving
    that out keeps each definition independent of the others: there is no
    order between fields, no cycle to find, and no clash between a device's
    name and a field's. A slice of a catalog's array also means the same
    strips whatever is connected, where a slice of the config's own would have
    to say whether it counts the strips defined or the strips present.
  - The cost is repetition. `route_strip` repeats `strip`'s slices, with the
    X-Touch's shortened to 0-6, and a second extender is added to both. That
    edit is needed either way, as "all but the last strip" is a different
    slice of the config's own `strip` once it has 24 strips.
  - JPRSurf's last strip is `xtouch.strip[7]`, as the X-Touch is required and
    on the right. The last strip of whatever is connected, for a config whose
    rightmost unit is optional, needs paths into the config's own arrays,
    counted from the end after absent strips are dropped. It waits for a
    config that needs it (see Summary).
- Checking finds every problem rather than stopping at the first, and each
  error names the field being defined (`strip`, or `transport.play`):
  - A device name, or a field name, that isn't a widget name, or a device
    named twice.
  - A path that isn't one, starts at no device, or names nothing in its
    device's catalog.
  - A field defined twice in one struct, directly or by an include.
  - An include of something that isn't a struct, or that excludes a field the
    struct doesn't have.
  - A join with no slices, a slice of something that isn't an array, a slice
    that is empty or runs past the array's end, and slices whose elements'
    shapes differ (naming both slices).
- The shape of a path definition is the catalog's shape there, of a struct
  definition its fields in order, and of a join an array of the first slice's
  element shape, sized by all the slices together.
- **Zipped arrays** wait for a device that needs one (see Summary).

### Resolving

```
struct WidgetDevice {
  ...
  // The device, or null if it is absent. Checking ignores it.
  Device* device = nullptr;
};

// A widget on the devices that are present. The devices must outlive it.
class Widget {
 public:
  WidgetKind GetKind() const;

  // For a control, the device's control, or null if it is absent.
  Control* GetControl() const;

  // For a struct, the field with a name, or null if it has none. A struct
  // keeps every field it is defined with, whatever is absent.
  const Widget* GetField(std::string_view name) const;

  // For an array, the elements that are present.
  absl::Span<const Widget> GetElements() const;

  // Returns the widget at the path from this one, or null if there is none:
  // a field its struct doesn't have, or an index past the elements present.
  const Widget* Find(const WidgetPath& path) const;
};

// Checks the widgets as CheckWidgets() does, and resolves them on the devices
// that are present. Returns the top-level struct, or nullopt with the errors
// checking found.
std::optional<Widget> ResolveWidgets(
    const WidgetStructDefinition& widgets,
    absl::Span<const WidgetDevice> devices, std::vector<std::string>* errors);
```

- **A device's controls are named by their catalog paths**, such as
  `strip[0].fader` or `play`, so a control in a catalog resolves to
  `Device::GetControl()` of its path's text. A device that is present but has
  no control of that name gives an absent control. *Device types and catalogs*
  renames `DeviceXTouch`'s controls to match, and checks each device against
  its catalog when it is created.
  - Each control then has one name, in logs, errors, traces, and tests, and a
    catalog stays only a shape.
  - The catalog could instead give each control's device name (`Fader1`), but
    then every control has two names to keep in sync, a shape holds a name
    its equality has to ignore, and the 88 strip controls each need one
    written out.
  - Or each device could build its own widget as it creates its controls, but
    then its structure is written twice, in its catalog and in its code, and
    `Widget` needs a public way to build one by hand.
- **Absent devices.** A catalog describes a device type, and never changes.
  Resolving makes each device's instance, and an absent device's instance has
  every field at its default: its controls are absent, its arrays are empty,
  and its structs keep their fields.
  - So a joined array has the elements of the devices present, in order, with
    no rule of its own.
  - An array reached any other way, by a path or an include, is empty the
    same way. The config model only says so for joined arrays. If such an
    array kept its 8 strips with nothing on them, a view repeated over it
    would put tracks on strips that aren't there, and banking would skip past
    them.
- **Paths address two different things.** A path in a definition addresses a
  catalog, so `xtouch.strip[7]` is the same strip whatever else is connected.
  A path into the resolved widgets (a mapping's) addresses what is present, so
  `strip[3]` is the fourth strip on the surface, and `strip[15]` is none
  without the extender, though checking accepts it. That is the model's rule
  that a mapping to an absent widget does nothing.
- A widget doesn't hold its shape: `WidgetShape::Find()` of the same path
  gives it, as every element of an array has the same shape.

**Brittleness:**
- Nothing can resolve a definition that doesn't check, as resolving checks
  first.
- A widget holds its devices' controls by pointer, so the devices must outlive
  it, as they already must outlive the views' mappings. The scene will own
  both.
- Until *Device types and catalogs* checks each device against its catalog, a
  control whose name doesn't match its catalog path is silently absent. Only
  the tests' fake devices are resolved until then.

### To confirm

Nothing: widgets touch neither REAPER nor the hardware.

## CLs

### CL1 [ ] device: Widget paths and shapes

Depends on: nothing.

- `widget_path.h`: `WidgetPath` and `IsWidgetName()`.
- `widget_shape.h`: `WidgetKind` and `WidgetShape`.
- Unused, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `widget_path_test.cc`: paths of fields and indices parse, and `ToString()`
  gives back the text; the empty path has no steps; malformed paths are
  rejected (an empty step, a leading or trailing `.`, an unclosed or empty
  `[]`, a negative or non-numeric index, upper case, a leading digit); and
  `IsWidgetName()` accepts and rejects names by the conventions.
- `widget_shape_test.cc`: each kind's accessors, with null and 0 from the
  other kinds' accessors; `Find()` through structs and arrays, and null for an
  unknown field, an index out of range, or a step into a control; equality,
  where a control's inputs, outputs, and mode names each matter, a struct's
  field order doesn't, and an array's size and element shape do; and
  `ForStruct()` leaving out a field whose name is taken.

### CL2 [ ] device: Define widgets, and check them against catalogs

Depends on: CL1.

- `widget_definition.h`: `WidgetSlice`, `WidgetInclude`, the definitions,
  `WidgetDevice` (without its device), and `CheckWidgets()`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- `widget_definition_test.cc`, against two hand-built catalogs: a device with
  buttons and an array of strip structs, and a second whose strips have the
  same shape (and a third whose strips don't):
  - The shapes of path, struct, and join definitions: includes with
    exclusions, nested structs, a join sized by all its slices, and a slice
    with no count taking the rest of its array.
  - Each error in Definitions and checking, with the field it names, and
    several errors reported by one check.

### CL3 [ ] device: Resolve widgets on the devices present

Depends on: CL2.

- `widget.h`: `Widget` and `ResolveWidgets()`, and `WidgetDevice::device`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- `widget_test.cc`, with `FakeDevice`s whose controls are named by their
  catalog paths:
  - Controls resolve to the device's controls, through path, struct, include,
    and join definitions, and `Find()` reaches them from the top and from an
    element.
  - With a device absent: its controls are null, its structs keep their
    fields, its arrays are empty, and joins keep the other devices' elements
    in order. `Find()` past the elements present is null.
  - A present device without a catalog control gives a null control.
  - A definition that doesn't check returns nullopt with its errors.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation, and
  update the config model's Widgets section to match (paths in definitions
  start at a device, an absent device's arrays are empty however they are
  reached, and a device's controls are named by their catalog paths). Move
  into the backlog, in the same change (see the workflow): zipped arrays;
  paths into the config's own arrays, counted from the end, for the last strip
  of whatever is connected; and anything else left undone.

## Checks in REAPER

None by the user, as nothing the plugin runs changes until *Device types and
catalogs*. At the end of the feature, the load check passes
(`check_load_in_reaper`), as the plugin links the new code.
