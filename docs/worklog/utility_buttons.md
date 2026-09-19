# Utility Buttons

The X-Touch utility buttons (Undo, Save, Cancel, Enter) run REAPER commands,
with Shift selecting an alternate command, and several buttons light up from
REAPER state to show when pressing them would do something. Along the way,
command and state property names became compile time constants, and REAPER
state that has to be polled shares one read-only property type.

## Behavior

### Buttons

| Button | Press                | Shift + press                         | Ctrl + press        | Hold        |
| ------ | -------------------- | ------------------------------------- | ------------------- | ----------- |
| Undo   | Undo                 | Redo                                  |                     |             |
| Save   | Save project         | Save new version of project           |                     |             |
| Cancel | Unselect all items   | Remove time selection and loop points |                     |             |
| Enter  | Insert new MIDI item | Insert empty item                     | Insert click source |             |
| Solo   | Toggle solo in front |                                       |                     | Solo defeat |

- Each press runs exactly one REAPER command, so it adds at most one undo point.
  The utility buttons have no long press. An earlier long press on Cancel that
  both unselected items and removed the time selection left two undo points.
- Solo is the standalone button above the transport. Holding it unsolos every
  track ("solo defeat"). As it has a long press, a short press toggles solo in
  front on release rather than on press, like Select and Global.

### Lights

| Light      | On while                                  | Mode     |
| ---------- | ----------------------------------------- | -------- |
| Undo       | There is anything to redo                 | Solid    |
| Save       | The project has unsaved changes           | Blinking |
| Cancel     | Any media items are selected              | Solid    |
| Global     | Track mode, below the top level           | Solid    |
| Solo (LED) | Any track is soloed (unchanged)           | Solid    |

- Save needs "undo/prompt to save" enabled in REAPER's preferences, or REAPER
  never reports the project as dirty.
- Cancel only reflects the item selection, which is what a plain press clears.
  Shift + Cancel doesn't affect it.
- Global is lit exactly when Global (up one level) or holding it (to the top)
  would move. It is off at the top level and in Send/Receive mode.
- All of them follow the current project when switching project tabs.

## Structure

### scene: commands (reaper_property.h)

Every REAPER command is a `kCmd*` constant, which `Scene::GetProperty()` creates
on demand as a `CommandToggleProperty` if REAPER reports a toggle state for it,
and a `CommandActionProperty` otherwise. The commands added here:

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
| `kCmdInsertClickSource`     | 40013 | Insert click source                                        |
| `kCmdSoloDefeat`            | 40340 | Track: Unsolo all tracks                                   |

### scene: polled state (reaper_property.h/.cc)

REAPER state that has no control surface notification is polled each run by a
`PolledToggleProperty`, a read-only toggle `SceneStateProperty` built from a
name and a `bool (*)()` read function. `UpdateState()` calls the function and
notifies only when the result changes, and writes are ignored.

| Property                | Read function                          | Used by    |
| ----------------------- | -------------------------------------- | ---------- |
| `kStateAnyTrackSolo`    | `AnyTrackSolo(nullptr)`                | Solo LED   |
| `kStateCanRedo`         | `Undo_CanRedo2(nullptr) != nullptr`    | Undo LED   |
| `kStateProjectDirty`    | `IsProjectDirty(nullptr) != 0`         | Save LED   |
| `kStateAnyItemSelected` | `CountSelectedMediaItems(nullptr) > 0` | Cancel LED |

This replaced `AnyTrackSoloProperty` (and a short lived `CanRedoProperty`),
which were the same code apart from the REAPER call. `AnyTrackSoloProperty`
also unsoloed all tracks when written false, which nothing used and which
doesn't belong on a state property. That action is now `kCmdSoloDefeat`, mapped
to holding Solo. `kStateAnyTrackSolo` can't be a `CommandToggleProperty` for
40340 instead, as REAPER reports no toggle state for that command.

### scene: kTrackHasParent (track_properties.h/.cc)

`TrackProperties::kTrackHasParent` is a toggle that is true while a track has a
parent track, alongside `kTrackIsFolder` and `kTrackExists`. Top level tracks
have the master track as their parent, so only the master (and stub) track has
none, and on the track list view it is true exactly when `View::kTrackParent`
would move up a level. It needs no polling: it notifies when a view's track
changes and when the track is removed, and moving a track in REAPER never takes
its parent away.

### plugin: mappings (control_surface.cc)

All in `ControlSurface::InitViews()`, next to the other global X-Touch mappings:
- Each utility button has an unmodified read mapping, and a read mapping with
  `ReadConfig::required_modifiers = kModShift`. `Control` masks Shift off the
  unmodified registration, so each press runs exactly one of them.
- Solo keeps its `kCmdSoloInFront` read/write mapping, and adds a `kLongPress`
  read mapping to `kCmdSoloDefeat`.
- The lights are write mappings from the polled states to Undo, Save, and
  Cancel. Save uses output mode 1, the MCU blinking light mode from
  `ControlDValueOutputMidiNote::McuLight()`.
- Global's light is a write mapping from `kTrackHasParent` on the track list
  view, which is only active in Track mode. When it is inactive nothing writes
  to Global, and a control with no writers clears its output, so it is off in
  Send/Receive mode without any extra mapping.

## Building blocks

- **`kNumberedName<Prefix, Value>` (common/numbered_name.h)**: a compile time
  `std::string_view` of a prefix followed by a non-negative integer, such as
  `"cmd:40029"`.
  - A `std::string_view` can't be a template parameter, but a reference to a
    `constexpr` one (with static storage duration) can.
  - The characters are in a `static constexpr std::array` in a helper class
    template, so the view always refers to valid static storage, with no
    runtime cost. It is not null terminated.
  - Checked with `static_assert`s in `jpr_common_test`.
- **`kCmdName<Id>` and `kStateName<Index>` (scene/reaper_property.h)**: property
  names built on `kCmdPrefix` and `kStatePrefix`, which `Scene::GetProperty()`
  parses. The prefix can't be mistyped or drift from the parser:
  ```cpp
  inline constexpr std::string_view kCmdUndo = kCmdName<40029>;
  inline constexpr std::string_view kStateAnyTrackSolo = kStateName<0>;
  ```
- **Polled toggles (scene/reaper_property.cc)**: to add one, add a
  `kStateName<N>` constant to `reaper_property.h`, and a row at index N in the
  `kPolledToggles` table with its read function.
  - Lookup is by index, like `cmd:<id>`, so it costs the same however many
    there are. `PolledToggleProperty::GetReadFunction()` returns null for an
    index out of range.
  - Each row holds its name constant, and a `static_assert` checks every row is
    named `kStateName<index>` for its own index, so a row out of order or a
    mistyped index fails the build.
  - The one mistake it can't catch is a name constant with no row. That fails
    gracefully at runtime: the property isn't found.
  - The table keeps the SDK calls out of the header, and `scene.cc` needs no
    change for a new one.

## Performance

Measured in a large, real project (103 tracks, hundreds of items):
- `AnyTrackSolo()`, `Undo_CanRedo2()`, and `IsProjectDirty()` only return state
  REAPER already has, and cost nothing measurable.
- `CountSelectedMediaItems()` adds a steady ~10us to the `Run()` average (74-76us
  to 85-86us), the same with no items, a few, or all of them selected. That
  suggests it walks every item in the project rather than keeping a count. It
  is ~1.6ms of UI thread time a second, and was accepted.
- With REAPER out of focus, the `Run()` average drops to ~40us.
- The big frames are REAPER running the commands on its UI thread, the same as
  the equivalent key press would: unselect all items ~14-17ms, remove time
  selection and loop points ~85ms (even with nothing to remove), and in this
  project undo up to ~1.3s and save ~1s.
