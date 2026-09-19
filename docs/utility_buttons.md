# Utility Buttons

Map the X-Touch utility buttons (Undo, Save, Cancel, Enter) to REAPER
commands, and document why `AnyTrackSoloProperty` can't be a REAPER command
toggle.

## Behavior

| Button | Press                | Shift + press                                |
| ------ | -------------------- | -------------------------------------------- |
| Undo   | Undo                 | Redo                                         |
| Save   | Save project         | Save new version of project                  |
| Cancel | Unselect all items   | Remove time selection and loop points        |
| Enter  | Insert new MIDI item | Insert empty item                            |

The Undo button is lit while there is anything to redo.

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

Mappings, all in the root view with the other global XTouch mappings:
- Shift variants use `ReadConfig::required_modifiers = kModShift`. `Control`
  masks off shift for the unmodified registration, so each press runs exactly
  one of the two commands.
- There are no long presses. A long press on Cancel that both unselected items
  and removed the time selection ran two commands, leaving two undo points.

The Undo LED comes from a new read-only toggle, `CanRedoProperty`
(`kCanRedo`), in `scene/reaper_property.h`. It is a `SceneStateProperty`, like
`AnyTrackSoloProperty`, whose `UpdateState()` checks
`Undo_CanRedo2(nullptr) != nullptr` each run and notifies when it changes.
Writes are ignored. REAPER has no control surface notification for undo history
changes, so it is polled; `Undo_CanRedo2` only returns a string REAPER already
has, so this should be cheap.

`AnyTrackSoloProperty` stays as it is. It looks like it could be a
`CommandToggleProperty` for "Track: Unsolo all tracks" (40340), but REAPER
reports no toggle state for that command, so the state must come from the SDK
`AnyTrackSolo()` function. A comment on the class explains this so it isn't
mistaken for redundant code again.

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
