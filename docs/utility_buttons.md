# Utility Buttons

Map the X-Touch utility buttons (Undo, Save, Cancel, Enter) to REAPER
commands, and light them from REAPER state. Along the way, REAPER state that is
polled each run shares one read-only property type.

## Behavior

| Button | Press                | Shift + press                                |
| ------ | -------------------- | -------------------------------------------- |
| Undo   | Undo                 | Redo                                         |
| Save   | Save project         | Save new version of project                  |
| Cancel | Unselect all items   | Remove time selection and loop points        |
| Enter  | Insert new MIDI item | Insert empty item                            |

The Undo button is lit while there is anything to redo. The Save button is lit
while the project has unsaved changes (this needs "undo/prompt to save" enabled
in REAPER's preferences, or REAPER never reports the project as dirty).

The Solo LED (lit while any track is soloed) keeps its current behavior.

## Design

All of these are plain REAPER commands, so they are `kCmd*` constants in
`scene/reaper_property.h`, which `Scene::GetProperty()` creates on demand as a
`CommandActionProperty` (or `CommandToggleProperty` if REAPER reports a toggle
state). No new property types are needed.

REAPER command IDs:

| Constant                    | ID    | REAPER action                                              |
| --------------------------- | ----- | ---------------------------------------------------------- |
| `kCmdUndo`                  | 40029 | Edit: Undo                                                 |
| `kCmdRedo`                  | 40030 | Edit: Redo                                                 |
| `kCmdSaveProject`           | 40026 | File: Save project                                         |
| `kCmdSaveNewProjectVersion` | 41895 | File: Save new version of project (increment project name) |
| `kCmdUnselectAllItems`      | 40289 | Item: Unselect (clear selection of) all items              |
| `kCmdRemoveTimeSelection`   | 40020 | Time selection: Remove time selection and loop points      |
| `kCmdInsertMidiItem`        | 40214 | Insert new MIDI item...                                    |
| `kCmdInsertEmptyItem`       | 40142 | Insert empty item                                          |
| `kCmdSoloDefeat`            | 40340 | Track: Unsolo all tracks                                   |

Mappings, all in the root view with the other global XTouch mappings:
- Shift variants use `ReadConfig::required_modifiers = kModShift`. `Control`
  masks off shift for the unmodified registration, so each press runs exactly
  one of the two commands.
- There are no long presses. A long press on Cancel that both unselected items
  and removed the time selection ran two commands, leaving two undo points.

### Compile time property names

Command and polled state property names are a prefix and a number (`cmd:40029`,
`state:0`). Rather than spelling out each string, they are built at compile time
from the prefix constant and the number, so the prefix can't be mistyped or
drift from what `Scene::GetProperty()` parses:

```cpp
inline constexpr std::string_view kCmdUndo = kCmdName<40029>;
inline constexpr std::string_view kAnyTrackSolo = kStateName<0>;
```

`kCmdName<Id>` and `kStateName<Index>` in `reaper_property.h` are aliases for
the generic `kNumberedName<Prefix, Value>` in `common/numbered_name.h`, a
`std::string_view` of "<prefix><value>" for a non-negative integer value. A
`std::string_view` can't be a template parameter, but a reference to a
`constexpr` one can. The characters are in a `static constexpr std::array` in a
helper class template, so the view always refers to valid static storage, and
there is no runtime cost.

### Polled REAPER state

The LEDs come from REAPER state that has no control surface notification, so it
is polled each run. `PolledToggleProperty` in `scene/reaper_property.h` is a
read-only toggle `SceneStateProperty` built from a name and a `bool (*)()` read
function. `UpdateState()` calls the function and notifies when the result
changes. Writes are ignored.

Like `cmd:<id>` for commands, polled toggles are named `state:<index>`
(`kStatePrefix`), where the index is into a constexpr table of read functions
(`kPolledToggles`) in `reaper_property.cc`, keeping the SDK calls out of the
header. `Scene::GetProperty()` parses the index and gets the read function from
`PolledToggleProperty::GetReadFunction(index)`, which returns null for an index
out of range, so a lookup costs the same however many there are.

Each table row holds its name constant as well, and a `static_assert` checks
that every row's name is `kStateName<index>` with its own index, so a row out of
order or a mistyped index fails the build. The only mistake it can't catch is a
name constant with no row, which fails gracefully at runtime (the property is
not found). Adding a polled toggle only touches `reaper_property.h` and `.cc`:

| Property        | Read function                         | Used by   |
| --------------- | ------------------------------------- | --------- |
| `kAnyTrackSolo` | `AnyTrackSolo(nullptr)`               | Solo LED  |
| `kCanRedo`      | `Undo_CanRedo2(nullptr) != nullptr`   | Undo LED  |
| `kProjectDirty` | `IsProjectDirty(nullptr) != 0`        | Save LED  |

Each read function only returns state REAPER already has, so polling should be
cheap.

`kCanRedo` started as its own `CanRedoProperty` (CL4), and `kAnyTrackSolo` as
`AnyTrackSoloProperty`, which were the same code apart from the REAPER call.
`AnyTrackSoloProperty` also unsoloed all tracks when written false, but nothing
used that, and it doesn't belong on a state property. That action is now its own
command, `kCmdSoloDefeat` ("solo defeat" is the usual name for it). It can't
replace `kAnyTrackSolo`, as REAPER reports no toggle state for 40340.

## CLs

### CL1 [x] scene: Document why AnyTrackSoloProperty is needed

Depends on: none.

- Comment on `AnyTrackSoloProperty` explaining why it reads `AnyTrackSolo()`
  instead of being a `CommandToggleProperty` for 40340. No code changes.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).

### CL2 [x] scene: Utility button commands

Depends on: none.

- Add the `kCmd*` constants for undo, redo, save, save new version, unselect
  all items, remove time selection, insert MIDI item, and insert empty item.
- Unused, so there is no visible change yet.

**Verify**
- Standard checks.
- Covered by CL3.

### CL3 [x] plugin: Map the utility buttons

Depends on: CL2.

- Undo, Save, Cancel, and Enter mappings as in Behavior.

**Verify**
- Standard checks.
- Undo undoes, Shift + Undo redoes.
- Save saves the project. Shift + Save saves a new version (project file name
  incremented).
- Cancel unselects all items and leaves the time selection and loop points.
  Shift + Cancel removes the time selection and loop points, and leaves the
  item selection.
- Enter opens insert new MIDI item. Shift + Enter inserts an empty item.
- Each press runs one command (check the undo history for a single entry).

### CL4 [x] scene: CanRedoProperty

Depends on: none.

- `CanRedoProperty` and `kCanRedo` in `reaper_property.h`/`.cc`, created on
  demand by `Scene::GetProperty()`.
- Unused, so there is no visible change yet.

**Verify**
- Standard checks.
- Covered by CL5.

### CL5 [x] plugin: Light Undo while there is anything to redo

Depends on: CL4.

- `kWriteControl` mapping from `kCanRedo` to the Undo button.

**Verify**
- Standard checks.
- Undo is dark with nothing to redo. It lights after an Undo (from the surface
  or REAPER), stays lit through further undos, and goes dark after redoing
  everything or making a new edit.
- It updates when switching project tabs.
- Steady state `Run()` time doesn't regress.

### CL6 [x] common: kNumberedName

Depends on: none.

- New `common/numbered_name.h` with `kNumberedName<Prefix, Value>`.
- `common/numbered_name_test.cc`, checked with `static_assert`s (plus a runtime
  test so it runs in `jpr_common_test`).
- Unused, so there is no visible change yet.

**Verify**
- Standard checks.
- `jpr_common_test` passes: a prefix with a single digit, multiple digits, zero,
  and an empty prefix.

### CL7 [ ] scene: PolledToggleProperty and compile time names

Depends on: CL4, CL6.

- `PolledToggleProperty` in `reaper_property.h`/`.cc`, with `kStatePrefix`,
  the `kPolledToggles` table and its `static_assert`, and `GetReadFunction()`.
- `kAnyTrackSolo` (`state:0`) and `kCanRedo` (`state:1`) become rows in the
  table, removing `AnyTrackSoloProperty` and `CanRedoProperty`.
  `Scene::GetProperty()` handles `state:` names in place of their cases.
- `kCmdPrefix` replaces the literal `"cmd:"` in `Scene::GetProperty()`.
- `kCmdName<Id>` and `kStateName<Index>`, and every `kCmd*` and state constant
  uses them.
- `kCmdSoloDefeat` replaces `AnyTrackSoloProperty`'s unused write. It is not
  mapped to anything yet.

**Verify**
- Standard checks.
- The Solo LED and Undo LED behave exactly as before.
- Every other command mapping still works (covered by the smoke test:
  transport, click, cycle, solo in front, the utility buttons, and automation
  modes).

### CL8 [ ] scene: kProjectDirty

Depends on: CL7.

- `kProjectDirty` and its `kPolledToggles` entry reading `IsProjectDirty()`.
- Unused, so there is no visible change yet.

**Verify**
- Standard checks.
- Covered by CL9.

### CL9 [ ] plugin: Light Save while the project is dirty

Depends on: CL8.

- `kWriteControl` mapping from `kProjectDirty` to the Save button.

**Verify**
- Standard checks.
- Save is dark in a freshly opened or saved project, lights after any edit, and
  goes dark after saving (from the surface or REAPER), including Shift + Save.
- Undoing back to the saved state: matches whatever REAPER shows in its title
  bar.
- It updates when switching project tabs.
- Steady state `Run()` time doesn't regress.
