# Modes from Enumerated Values and Picks

The last of `PluginSurface`'s surface state and callbacks moved into components
below `plugin`. `PluginSurface` now has only host plumbing (see
[extension_host.md](extension_host.md)) and its mappings, which *Build the
scene from a SurfaceSpec* turns into data.
- **Enumerated values** hold the surface mode (`user:surface_mode`) and what
  Rewind and Forward step by (`user:transport_step`). They replaced
  `SurfaceMode`, `kModeInfo`, the mode functions, `AddExclusiveToggleMapping()`,
  and the `mod:marker` and `mod:nudge` modifiers.
- **Conditions compare values**, for equality, so a view or mapping can depend
  on one value of an enumerated property.
- **Read conditions are checked when the input arrives**, rather than by
  registering and unregistering the mapping's input, so a mapping keeps its
  gesture and never loses a pending press.
- **Picks** set Send/Receive mode's track, from the Send button and from a
  strip's select button. Mappings on the same presses set `user:surface_mode`
  to enter the mode. They replaced `TryEnterSendReceiveMode()` and the pick
  callback.
- **Tap** is a press behavior in `device`, which replaced `send_press_mode_`,
  `send_press_time_`, and `ApplySendRelease()`.
- **Mode lights** are mappings in the mode views.
- **`ControlSurfaceListener`** lost `OnTracksChanged()`, `OnSelectionChanged()`,
  and `OnLastTouchedTrackChanged()`, which nothing used any more.

This replaced the config model's *Exclusive group*
([config_model.md](../config_model.md)) with an enumerated value, which makes
"exactly one" true by construction, rather than enforced across several
toggles.

## Behavior

Nothing changed, apart from:
- Marker and Nudge are one value, so they are exclusive however they are
  changed, not just from their buttons.
- The Send light follows route changes on the selected track, not only
  selection and track list changes.
- Releasing Send within 350ms of pressing it is always a tap. Before, holding
  Send to pick a track cancelled the release, so two quick cases differ:
  - Holding Send, pressing select on a track without routes, and releasing
    Send enters Send/Receive mode for the selected track, if it has routes.
  - Releasing Send in the frame right after a pick (within ~33ms), before the
    mode views switch, runs Track mode's tap too, which picks the selected
    track instead.
- Deleting the Send/Receive track leaves the surface in Send/Receive mode,
  showing nothing until another track is touched, rather than returning to
  Track mode. This is rare, and harmless: the mode follows the next track
  touched, and the Track button leaves it.
- The plugin's "Surface mode changed" log line is gone. The scene logs each
  mode view's activation and deactivation.

## Design decisions

- **Compose existing pieces.** Each piece is as small as it can be, and a
  feature combines them (see the scene layer paragraph in CLAUDE.md). A pick
  only picks. Whether it should happen is its mapping's condition, and entering
  the mode is a second mapping on the same press, with the same condition.
  Setting one value is a range whose ends are the same, not a separate option.
  A config (or SurfaceSpec) may offer shorthands for these, which expand to
  the runtime form.
- **Where conditions go:**
  - A view's condition switches a whole set of mappings, such as a mode.
    Leaving a view unregisters its mappings.
  - A mapping's condition decides whether that mapping acts right now. For a
    write, it picks which property drives the control. For a read, it is
    checked when the input arrives, and the mapping keeps its input either
    way.
  - A component takes a condition only for a rule about state it owns, which
    must hold however that state is changed. None does yet.

  So a condition that says whether a button does something goes on its
  mapping, not in the property the mapping triggers.
- **Equality is a coerced comparison.** `ViewProperty::Equals(value)` reads
  the property as the value's type and compares. Any value can be compared
  with any property, and one it never reads as (an int on a toggle) is never
  met, rather than an error.
- **A tap is only timing.** It never defers the other presses in its group, so
  the `press_release` mapping holding `mod:send_hold` turns on at once.
  Rebuilding a control's press groups drops a pending tap, so a mode change
  while Send is held drops the old mode's tap, and the new mode's tap mapping
  never saw the press.

## Names

- **Tap** (`InputConfig::PressBehavior::kTap`): the config model's name, a
  press behavior like long and double press.
- **`press_toggles`** (`ViewMapping::ReadConfig`): a press on an enumerated
  property toggles between the ends of its range, rather than stepping through
  it.
- **Enumerated value** (`EnumeratedValueProperty`): an enumerated property that
  holds its own value, as `ToggleValueProperty` holds a toggle, with a name for
  each value.
- **`user:surface_mode`**, with values `track` (the default) and
  `send_receive`. **`user:transport_step`**, with values `measure` (the
  default), `beat` (Nudge), and `marker` (Marker).
- **Track pick** (`TrackPickProperty`): named like `TrackAnchorProperty`, for
  what it picks. **`user:pick_selected_track`** is the top level pick (tapping
  Send), and **`user:pick_track`** each strip's (select while Send is held).

## Structure

### device: Tap (control_input_handle.h, control.h/.cc)

- `PressBehavior::kTap` is delivered on release, if the button was held for
  less than a long press (350ms).
- A press group keeps its tap ids, and the time its button was pressed. Only
  long and double press defer a normal press, as before.
- Without release support, a tap is delivered on the press, as every press is
  then a tap.
- `Control` runs on REAPER's clock, so it has no unit test.

### scene: Read conditions (view_mapping.h/.cc)

- A read mapping registers its input while it is enabled and its view is
  active, whatever its condition. `ReadControl()` does nothing while the
  condition isn't met.
- A registered read still counts in its control's modifier sets and press
  groups while its condition isn't met, so a Send+select that picks nothing
  does nothing, rather than falling through to select the track.
- A held (`press_release`) mapping applies the release of a press it applied
  (`holding_`), even if its condition changed in between, so a held modifier
  can't stick on.
- The write side is unchanged: it writes, and holds the control's output, only
  while the condition is met.

### scene: Conditions that compare values (view_condition.h, view_property.h/.cc)

- `ViewCondition::Config{property, value}`, where `value` is a
  `ViewProperty::Value` that defaults to true. It is met while
  `property->Equals(value)`.
- `ViewProperty::Equals(value)` compares with the property's adapter functions:
  `GetBool()`, `GetInt()`, `GetText()`, and `GetColor()`. A double is read in
  the property's own range (`GetPan()`, `GetVolume()`, or `GetNormalized()`). A
  `TimelinePosition` is only compared with a timeline position property
  (`TimelinePosition` has `operator==`), and an empty value is never equal.
- Mode overrides use `Equals()` too.
- `ViewProperty::GetValue()`, `SetValue()`, and `HasValueType()`, which nothing
  used, are gone.
- `View::CreateCondition()` makes the condition for a view or a mapping.

### scene: press_toggles, and setting one value (view_mapping.h/.cc)

- Without `press_toggles`, a press on an enumerated property steps to the next
  value in `[property_min, property_max]`, wrapping around (the timecode
  display's modes). With it, a press sets `property_max`, or `property_min` if
  the value already is `property_max`.
- A range whose ends are the same sets that value, for every property type but
  toggles (whose press always flips them). The Track button is
  `[track, track]`.

### scene: EnumeratedValueProperty (value_property.h)

- `EnumeratedValueProperty(name, value_names, value = 0)`: values 0 to the
  number of names minus one (one empty-named value with no names), starting at
  `value`, clamped.
- `GetText()` returns the value's name, and `SetText()` sets the value by name,
  or reads other text as a number.
- Setting it always sets the value asked for (clamped), and notifies only on a
  change. Header only, and unit tested (value_property_test.cc).

**Brittleness:** code refers to values by index, so the plugin's constants must
match the order of its names. The plugin keeps each list beside its constants,
until a config names values (see *Value names for every enumerated property* in
the [backlog](../backlog.md)).

### scene: TrackPickProperty (track_pick_property.h/.cc)

- `TrackPickProperty(name, reference, source = nullptr)`: an action that sets
  `reference` to `source`'s track, or to its view's track with no source, if
  that track exists. It overrides `SetView()` to keep its view.
- `Scene::GetTrackReference()` is public, so a source can be a built in
  reference such as `state:selected_track`.
- It needs `TrackCache`, so it has no unit test.

**Brittleness:** the references must outlive the pick, which the scene's member
order already guarantees.

### plugin: PluginSurface (plugin_surface.h/.cc)

- **Rewind and Forward**: each step in `kTransportSteps` maps Rewind and
  Forward to its commands, with a condition on `user:transport_step`. Marker
  and Nudge read it with `press_toggles` (`[measure, marker]` and
  `[measure, beat]`), and light from a `const:` on toggle with a condition on
  their value.
- **Modes**: `user:surface_mode` enables the `TrackMode` and `SendReceiveMode`
  views by its values. The Track button sets `[track, track]`. Tapping Send in
  Track mode runs `user:pick_selected_track` (from `state:selected_track`), and
  sets `[send_receive, send_receive]`, both with the condition
  `state:selected_track.has_routes`. Holding Send (`mod:send_hold`, on the
  root view) and pressing a strip's select does the same with the strip's
  `user:pick_track`, and the condition `track:has_routes`. Both picks set
  `user:current_track`. The mode views switch on the next run, so the track is
  set before its view activates.
- **Send in Send/Receive mode**: a tap runs `view:child_route_toggle`.
- **Mode lights**: Track mode blinks Track, and lights Send from
  `state:selected_track.has_routes`. Send/Receive mode blinks Send, and lights
  Track. The current mode stays lit whether or not it is available.
- The views, `user:current_track`, and the send hold modifier are locals in
  `InitViews()`. `PluginSurface` keeps only its runners, ports, config, and
  scene.

### common: ControlSurfaceListener (control_surface.h/.cc)

- The listener has only `OnRun()` and `GetConfig()`. `ControlSurface` still
  keeps `TrackCache` up to date for track list, visibility, selection, and last
  touched track changes.

## Building blocks

- **Enumerated value**: state with named values, which a view or mapping
  depends on with a condition on one value. A mode is one value, and its view
  is enabled by it.
- **Conditions on values**: `ViewCondition` with any `ViewProperty::Value`,
  compared with `ViewProperty::Equals()`.
- **Read conditions**: a read mapping's condition gates it without losing its
  gesture, so reads can switch on any state, not just modifiers.
- **Setting one value**: a range whose ends are the same.
- **`press_toggles`**: a button that toggles an enumerated property between two
  values, such as Marker and Nudge.
- **Track pick**: sets a track reference from another reference, or from the
  view's track. Anything else the same press does is another mapping on it.
- **Tap**: a button that is held for one thing and tapped for another.

## Performance

- The `Run()` average stayed at ~20-35us throughout, the same as before.
- Mode switches: activating `TrackMode` takes ~125us, and `SendReceiveMode`
  ~25us.
- A tap costs nothing per run: the work is on the press and the release.
- Watching `state:selected_track.has_routes` refreshes the selected track from
  REAPER on each run in Track mode (three REAPER reads), although `has_routes`
  only changes by notification. No change showed in the `Run()` average. See
  *Refresh a reference's track only for polled fields* in the
  [backlog](../backlog.md).
- The strips' `track:has_routes` read conditions add no REAPER queries, only a
  few flag checks per run.
- Rewind and Forward run REAPER's move commands inside `Run()`, which is most
  of their cost. Only one runs per run, however fast the button is pressed.
