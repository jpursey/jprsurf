# Config file language and loader

JPRSurf's surface comes from a config file, written in a small language of its
own, rather than from C++. A surface's config string in REAPER names the file,
which REAPER's control surface dialog picks. Reading a file fills in the
`SurfaceSpec` (see [surface_spec.md](surface_spec.md)), which `CreateScene()`
checks and builds the scene from, as before.

The language is bespoke, parsed with Game Bits' `gb/parse`, rather than
`gb::ReadConfigFromText()`, because a `gb::Config` map is unordered, keeps no
source locations for errors, and would make each mapping an object of its own,
where a mapping should be one dense line.

## Behavior

- **JPRSurf's own surface is a config file**, `config/jprsurf.jprsurf`, which
  the build deploys beside the DLL, in REAPER's `UserPlugins` folder. It is the
  only definition of JPRSurf's surface: `GetDefaultSpec()` is gone, and the
  surface tests read the file from the source tree.
- **The config string is the file's path.** An empty config string is the
  deployed `jprsurf.jprsurf`, and so is `null`, which earlier versions wrote,
  and which the surface gives back to REAPER as empty.
- **REAPER's control surface dialog picks the file:** Edit shows a Config file
  box with a Browse button. OK recreates the surface, which reads the file
  again, even if the path didn't change, so Edit, then OK, reloads it.
- **A config with an error runs nothing.** Every error is logged, with the
  file and line it is on, and the whole config is rejected: the surface does
  nothing until it is fixed, as a config never runs half working.
- **The user guide** has a page on config files
  ([config_files.md](../user/config_files.md)): picking one, the language by
  example, and errors.

Nothing looks or feels different: the scene built from the file is the one
`GetDefaultSpec()` built, which a parity test proved before `GetDefaultSpec()`
went. No call count bound moved.

## The language

`config/jprsurf.jprsurf` is the whole language at work. Brackets say what kind
of thing is inside them, everywhere in the file:
- `{ }` holds a **body**: a list of statements, each starting with a keyword,
  except in a `mappings` block, where each is a mapping.
- `( )` holds **parameters**, `name = value`, separated by commas: of a device,
  a component, a view, a named `mappings` block, a use, and a list. A
  parameter is given at most once, so what can be repeated is a statement in
  a body.
- `[ ]` holds a **list**: an enumerated value's values, the inputs and outputs
  a control override leaves out, and how a mapping reads its control.

`#` starts a comment, to the end of the line, and line breaks are only
whitespace. The first statement is the language's version, `jprsurf 1`, and a
version the reader doesn't know is an error.

**The top level is the scene's root view**, with no `view` statement of its
own. It has what any view's body has (`components` and `mappings` blocks,
child views, and `when`), and what only the top level has: exactly one
`devices` and one `widgets`, at most one `settings`, and any number of named
`mappings` blocks (templates). **Order doesn't matter**, except for a view's
components, across its blocks, as a component names those declared before it.

| Where                                                | Statement                                                                                                                                      | Spec                                                                           |
| ---------------------------------------------------- | ---------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------ |
| Top level, first                                     | `jprsurf <version>`                                                                                                                            | Checked by the reader only                                                     |
| Top level, once                                      | `devices { ... }`                                                                                                                              | `SurfaceSpec::devices`                                                         |
| Top level, once                                      | `widgets { ... }`                                                                                                                              | `SurfaceSpec::widgets`                                                         |
| Top level, at most once                              | `settings { track_filter <mixer \| track_panel \| all> }`                                                                                      | `SurfaceSpec::track_filter`                                                    |
| Top level                                            | `mappings <name>[(<params>)] { ... }`                                                                                                          | `TemplateSpec`                                                                 |
| Top level and views                                  | `components { ... }`                                                                                                                           | At the top level, `SurfaceSpec::components`; in a view, `ViewSpec::components` |
| Top level and views                                  | `mappings { ... }`                                                                                                                             | `MappingSpec`s and `TemplateUse`s                                              |
| Top level and views                                  | `view <name>[(subject = <reference>, list = <list>)] { ... }`                                                                                  | A child `ViewSpec`                                                             |
| Top level and views                                  | `view <element> in <array> { ... }`                                                                                                            | A repeated child `ViewSpec`                                                    |
| Top level and views, `mappings`, a mapping's options | `when <condition> { ... }`                                                                                                                     | A view's condition, each mapping's, or a `ModeOverride`                        |
| `devices`                                            | `<name> = <type>(input = "<port>", output = "<port>"[, optional = true])`, with an optional body of `without <control> [<inputs and outputs>]` | `DeviceSpec`                                                                   |
| `widgets`                                            | `include <path>`, with an optional body of `except <field>`, and `<name> = <path>`, `join(<slices>)`, or `{ ... }` for a nested struct         | `WidgetStructDefinition`                                                       |
| `components`                                         | `<kind> <name>[(<param> = <value>, ...)]`                                                                                                      | `ComponentSpec`                                                                |
| `mappings`                                           | `use <name>[(<param> = <argument>, ...)]`                                                                                                      | `TemplateUse`                                                                  |
| `mappings`                                           | A mapping (below)                                                                                                                              | `MappingSpec`                                                                  |

**A mapping** is `<control> [<how it is read>] -> | <- | <-> <property or
value> { <options> }`:
- The arrow is the way the value flows: `->` reads the control into the
  property, `<-` writes the property to the control, and `<->` does both.
- In brackets before the arrow: an input (`press`, `value`, or `delta`), a
  press behavior (`long`, `double`, or `tap`), `held`, `touch`, and the
  required modifiers by name.
- The property is a property name, or a fixed value (`true`, a number, a
  quoted string, or `rgb(r, g, b)`), for which the scene adds a `const:`
  property. In a template, a property parameter is a plain name.
- Options: `set <value>`, `range <min> .. <max>`, `toggle <min> .. <max>`,
  `mode <mode>`, and `when <condition> { mode <mode> }` (a mode override).

**Named mappings** are a template, with `widget <name>` and
`property <name> [= <default>]` parameters, a default being a property name or
an earlier property parameter. Its properties are found where it is used, and
its widgets are only its parameters and the top-level widgets, so it means the
same wherever it is used.

**A condition** is `<property>` (`= true`) or `<property> = <value>`, where a
value is `true`, `false`, a number, a quoted string, or a name, such as an
enumerated value's. `when` means "applies while": around a view, the view is
enabled; around mappings, each acts; around a `mode`, the mode is used. Each
view, mapping, and mode override has one condition, so a `when` directly in
another, around a `use`, or around a block or an option other than `mode`, is
an error. A `when` around a view is outside it, so it sees the parent's names.

Component kinds and their parameters are read from the catalog
(`GetComponentKinds()`), so a new kind needs no change to the grammar.

### Names

`config` for the library (what the docs call the file), `.jprsurf` for the
file, `ReadSurfaceConfig()` for the reader (it doesn't open a file),
`SpecLine` for where a part was written, `default_value` for a template
parameter's default, and `mixer` and `track_panel` for the track filter, as
REAPER calls its panels. Keywords are the config model's words where it has
one (`when`, `mode`, `held`, `long`, `double`, `tap`, `touch`, `range`,
`subject`, `list`, `child_tracks`, `routes`, `bank`, `reveal`). New ones:
`devices`, `widgets`, `components`, `mappings`, `settings`, `view ... in`,
`set`, `toggle`, `without`, `join`, `include`, `except`, `use`, `input`,
`output`, `widget`, and `property`.

## Errors

- **Every error starts with where it is**: the file, `line <n>: `, then what it
  is about, as checking words it (`<path>: line 61:
  /track_mode/track_list/strip[]: track_strip: strip.mute: ...`).
- **A spec keeps lines, not files.** Each `DeviceSpec`, control override,
  widget include and field, component, template, use, view, and mapping has a
  `SpecLine`, the line it was written on (0 in a spec written in C++, which
  errors leave out), and `GetSpecError()` starts its errors with it. A line
  always compares equal, as it isn't part of what the spec says. Only the
  plugin, which opens the file, names it.
- **Reading reports every error it can.** A syntax error stops reading, as
  `gb::Parser` doesn't recover from one, but the rest are each reported, then
  checking reports everything it finds. Reading checks only what a spec can't
  hold, and leaves the rest to `ValidateSurfaceSpec()`, which knows where each
  part is.

## Structure

### device

- **`spec_line.h`:** `SpecLine`, `SpecPart` (a line and the text an error
  starts with), and `GetSpecError()`, which the device, widget, and scene
  checks share.
- `CreateWidgetDevices()` reports a device with no input or output port, and
  `ValidateWidgets()` a slice that ends before it starts.

### scene

- Components, templates, uses, views, and mappings have a `SpecLine`, and every
  spec struct (and the configs they hold) has `operator==`.
- **Template parameter defaults:** `ValidateSurfaceSpec()` checks a default as
  it checks an argument, and `CreateScene()` uses it where a use gives none.
  Checking also reports a widget and a property parameter with the same name,
  and a modifier a mapping requires twice.

### config (new library)

`jpr_config`, between `scene` and `plugin` (`common` → `device` → `scene` →
`config` → `plugin`), which keeps `gb_parse` out of `scene`.
`ReadSurfaceConfig()` (`surface_config.h`) reads a config's text into a spec,
or null with every error. The grammar is a `gb::ParserProgram`, with a user
token for a property name, so one never reads as a widget path, and widget
paths are read with `device`'s `kWidgetPathRules`, so there is one path
grammar. A `when` fills in the condition of each view or mapping it holds, or
adds a mode override. A use's arguments are sorted into widgets and properties
by its template's parameters, which are read first, as a use may come before
its template.

### common

- **`win_config_panel.h`:** `CreateConfigPanel()`, the panel in REAPER's
  control surface dialog, a dialog from an in-memory template, in the style and
  font of the SDK's examples (`DS_CONTROL`, `WS_CHILD`, 8 point MS Shell Dlg),
  so its controls are sized in dialog units. It writes the path into REAPER's
  buffer on `kWriteConfigMessage` (`WM_USER + 1024`), truncating it and
  logging an error if it doesn't fit. Its one seam is the file picker
  (`PickFile`), which tests replace; `PickFileToOpen()`, Windows' Open
  dialog, is checked only in REAPER.
- **`ControlSurface`:** `Type::create_listener` is a `std::function`, and
  `Type::config_file` (a `FileFilter`), when set, has `ShowConfig()` create the
  panel.
- **Testing:** `FakeReaper::ShowSurfaceConfig()` and `ConfirmSurfaceConfig()`
  act out what REAPER does with the dialog (see Checked facts), and
  `TestDialog` stands in for its window.

### plugin

- **`PluginSurface`** reads its config string as a path in `GetFileSystem()`,
  and creates the scene from the file, logging which file it reads and every
  error with the path in front. It registers the `*.jprsurf` filter.
- **`dll_main.cc`** finds the deployed file beside the DLL
  (`Plugin::Options::config_path`), and registers the operating system's files
  (`gb::LocalFileProtocol::CreateOsRoot()`), so a path opens as Windows writes
  it.
- **The build** deploys `config/jprsurf.jprsurf` beside the DLL, and the tests
  read it where it is checked in (`JPR_CONFIG_FILE`): `ConfigFileTest` checks
  it reads and is valid, and `DefaultConfigTest` builds the surface tests'
  scene from it (`SurfaceTest` takes a config file's path).
- **`PluginTest`** fails on any error logged, as the surface tests do, and
  tests a config string that is empty, `null`, a path, a missing file, a file
  with errors, and the dialog: `null` shown as empty, and OK reading the file
  again.

## Building blocks

- **A change to JPRSurf's surface** is an edit to `config/jprsurf.jprsurf`,
  which `ConfigFileTest` reads and checks before any surface test runs.
- **A new component kind** needs a few lines in the reader that fill in its
  `ComponentSpec` from its parameters' text, beyond what surface_spec.md's
  building blocks say. A test reads one of each kind in `GetComponentKinds()`,
  with every parameter, so a kind added without them fails.
- **A new statement** is a grammar rule, a few lines that fill in the spec, and
  a `SpecLine` on the part it fills, so its errors say where it is.
- **A surface type whose config is a file** sets `Type::config_file`, and gets
  the dialog's panel for free.

## Performance

Everything new runs once, when the surface is created (or recreated from the
dialog). Nothing changed per run, so no benchmark ran. In the user's REAPER,
the end-of-feature test logged no "Slow run" warnings.

## Checked facts

In "Seen in traces" in `docs/testing_and_profiling.md`, and in the fake:
- **The config string** is the rest of the surface's `csurf_<n>` line in
  `reaper.ini` after the type: a path with spaces comes whole, unquoted, with
  spaces inside it kept and those after it dropped. On exit, REAPER writes the
  line back without calling `GetConfigString()`. An empty string is saved as
  the type followed only by spaces.
- **The dialog:** Edit calls `GetConfigString()`, then `ShowConfig()` with that
  string (the surface's, not the one saved: a surface saved as `null` that
  gave back an empty string showed an empty box), and REAPER shows the hidden
  `WS_CHILD` window it returns. On OK, it sends the window `WM_USER + 1024`
  (`wParam` 1024, the buffer's size, and `lParam` the buffer), destroys it,
  destroys the surface, and creates one with that string, even if it is
  unchanged, then calls `GetConfigString()` on it. On Cancel, it only
  destroys the window.

## Left for later

In the backlog:
- New: *Include other config files*, *Template parameters that are values*
  (which the transport steps would use, rather than being written out),
  *Conditions around a use*, *Devices with only an input or an output*, and
  *Read a config with a byte order mark* (one is a syntax error at line 1
  today).
- *Reload the config without restarting REAPER*, narrowed to a REAPER action,
  as the dialog's OK already reloads it.
- *Watch a shared condition once*: `when` blocks gave a shared condition its
  ergonomics, and the scene still watches it once per mapping.
- *Mapping order changes REAPER calls*: the file groups mappings by condition,
  and order doesn't matter, but it moves a few cheap calls.
- *Public config spec and user guide split*, next in the config model's
  "Getting there".

Left as they are: a named `mappings` block that nothing uses does nothing,
silently; and a line always compares equal, which is easy to forget when
comparing specs, but clearing lines first would be a step every comparison has
to remember.
