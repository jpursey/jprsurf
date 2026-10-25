# Widgets

Widgets are the names mappings will use for hardware, without saying which
device a control is on (see [Widgets](../config_model.md#widgets) in the config
model). A widget is a control, a struct of named widgets, or an array of
widgets of the same shape. A config defines its widgets from paths through the
devices' catalogs, and they resolve to the `Control`s of the devices that are
present, so `strip[3].fader` is the fourth fader on the surface whether or not
the extender is connected.

This feature added widgets to `device` as plain data, unit tested against
hand-built catalogs and fake devices. Nothing uses them yet: device catalogs,
and the plugin mapping through widgets, are *Device types and catalogs*. There
is no change in behavior.

## Behavior

### Names and paths

- A **widget name** is any C identifier: devices, struct fields, and output
  modes all have one. The config model's lower case convention is JPRSurf's
  style for its own names, not a rule that checking enforces.
- A **path** is field names and array indices from a widget, such as
  `strip[3].fader`. Each field but the first follows a `.`, so `[3].fader` is a
  path, but `strip.[3]` isn't. Indices start from 0, and fit an int. Spaces
  between steps are allowed, as paths are parsed with `gb_parse`, and the empty
  path is the widget itself.

### Definitions

A config's widgets are the fields of a top-level struct. A field is defined as:
- A **path through a device**, such as `xtouch.play` or `xtouch.strip[7]`. A
  path can name any widget, so a path to a device's array is an array.
- A **struct**: the fields of its **includes** (structs at paths, apart from
  the fields each excludes), in order, then its own fields.
- A **join**: slices of arrays, one after another. A slice is `count` elements
  from `first`, or the rest of its array.

Every path in a definition starts at a device. Paths into the config's own
arrays, and zipped arrays, wait in the backlog.

A **device** in a definition is one unit, named by the config, with its type's
catalog. Several devices of one type share a catalog under different names, so
three extenders are `ext_1`, `ext_2`, and `ext_3`, and one join can take all
their strips.

### Checking

`CheckWidgets()` checks definitions against the catalogs without any devices,
and returns the top-level struct's shape. It finds every problem rather than
stopping at the first. Each error starts with what it is about: a device and a
path in its catalog (`xtouch: strip[0]: field "fader" is repeated`), or the
struct or field being defined (`transport.play: path "xtouch.paly" names
nothing`). It reports:
- Device names that aren't widget names or are repeated, and devices without a
  struct catalog. A path from such a device isn't reported again.
- In each catalog, at any depth, field and mode names that aren't widget names
  or are repeated in their struct or control. A catalog shared by several
  devices is checked once, and every element of an array once, as `[0]`.
- Field names that aren't widget names, or are defined twice in a struct,
  directly or through an include.
- Paths that aren't paths (with the parser's error), don't start at a device,
  or name nothing.
- Includes of something that isn't a struct, and exclusions of a field the
  struct doesn't have.
- Joins with no slices, slices of something that isn't an array, slices that
  are empty or run past their array, slices whose elements' shapes differ from
  the first slice's, and joins of more than an int's worth of elements.

Shapes match exactly, apart from the order of a struct's fields.

### Resolving

`ResolveWidgets()` checks as `CheckWidgets()` does, and resolves the widgets on
the devices that are present, returning the top-level struct, or null with the
errors.
- **A device's controls are named by their catalog paths**, such as
  `strip[0].fader` or `play`, so a catalog control resolves to
  `Device::GetControl()` of its path's text. A present device without that
  control gives a null control. *Device types and catalogs* renames
  `DeviceXTouch`'s controls to match.
- **An absent device's instance** has every field at its default: its controls
  are null, its arrays are empty, and its structs keep their fields. So a join
  has the elements of the devices present, in order, and an array reached by a
  path or include is empty the same way.
- **Paths address two different things.** A path in a definition addresses a
  catalog, so `xtouch.strip[7]` is a struct with null controls when the X-Touch
  is absent. A path into the resolved widgets (a mapping's) addresses what is
  present, so `strip[15]` is nothing without the extender, though checking
  accepts it. That is the model's rule that a mapping to an absent widget does
  nothing.

## Structure

All of it is in `device`, with no REAPER dependency.

### widget_path.h

- `kWidgetPathLexerConfig`, `kWidgetPathSymbols`, and `kWidgetPathRules`: the
  path grammar. A language that includes paths (the config language) uses a
  superset of the lexer config and includes the rules, matching a path with the
  `WidgetPath` rule. It may add symbols, comments, and other kinds of token,
  but must read identifiers and integers exactly as paths do: no other
  identifier or integer flags, no keywords, and no affixes.
- `IsWidgetName()`: a C identifier, which is exactly what a path reads as one
  field.
- `WidgetPath`: steps (`std::variant<std::string, int>`), `Parse()` (with the
  parser's error), `FromParsedItem()` for a match in another language,
  `GetSubpath()`, and `ToString()`, which `Parse()` reads back as the same
  path.

### widget_shape.h

`WidgetKind` (`kControl`, `kStruct`, `kArray`), and `WidgetShape`: a
control's inputs, outputs, and mode names, a struct's fields (`Field {name,
shape}`, in order), or an array's size and element shape, with `Find()` by
path and equality. Shapes are immutable values that share their contents, so a
catalog reuses one shape for every control of a kind. The `For*` factories
never fail: names are kept as given, and `CheckWidgets()` reports bad or
repeated ones, so a catalog is one nested expression.

### widget_definition.h

- The definitions: `WidgetSlice`, `WidgetInclude`, `WidgetStructDefinition`,
  `WidgetJoinDefinition`, and `WidgetFieldDefinition` (a path, a struct, or a
  join).
- `WidgetDevice`: a name, a catalog, and the `Device`, or null if it is absent.
- `CheckWidgets()` and `ResolveWidgets()`, which share one walk (see Building
  blocks).

### widget.h

`Widget`: a control (a `Control*`, null if absent), a struct (`Field {name,
widget}`, in order), or an array of the elements present, with `GetElement()`
and `Find()` by path. Widgets never change once built, so they can't be copied
or moved, and are held by `std::unique_ptr<const Widget>`: every widget in a
tree keeps its address. `ForStruct()` and `ForArray()` take a vector, or the
fields or elements as arguments, and `Widget::Fields` and `Widget::Elements`
name the spans the accessors return.

## Building blocks

- **One walk for checking and resolving:** `WidgetChecker` builds each
  definition's shape and widget together (`Checked`), so the rules of paths,
  includes, exclusions, and slices are written once. `Find()` returns where a
  path leads (`Found`: its shape, device, and catalog path), and each caller
  instantiates only what it keeps. `CheckWidgets()` resolves as if every device
  were absent.
- **Extending the path grammar:** a config language includes
  `kWidgetPathRules`, and reads a matched path with
  `WidgetPath::FromParsedItem()` (`GrammarWorksInALanguageThatIncludesIt` in
  `widget_path_test.cc` is the example).
- **Errors with subjects:** a check that keeps going collects `"subject:
  message"` errors, with the name checks shared (`CheckWidgetName()`,
  `AddName()`), so every kind of name is reported in the same words.
- **Widgets by hand in tests:** `Widget::ForStruct(Field{"play",
  Widget::ForControl(play)}, ...)`, with `using Field = Widget::Field;`, and
  fake devices whose controls are named by their catalog paths
  (`ResolveWidgetsTest` in `widget_definition_test.cc`).

## Left for later

In the backlog: *Zipped widget arrays*, and *Paths into the config's own
widget arrays* (for the last strip of whatever is connected).
