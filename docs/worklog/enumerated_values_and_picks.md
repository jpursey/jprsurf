# Modes from Enumerated Values and Picks

The last of `PluginSurface`'s surface state and callbacks move into components
below `plugin`:
- **Enumerated values** hold the surface mode (`user:surface_mode`) and what
  Rewind and Forward step by (`user:transport_step`), replacing `SurfaceMode`,
  `kModeInfo`, `ModeButton`, the mode functions, `AddExclusiveToggleMapping()`,
  and the `mod:marker` and `mod:nudge` modifiers.
- **Conditions compare values**, for equality only, so a view or mapping can
  depend on one value of an enumerated property.
- **Read conditions are checked when the input arrives**, rather than by
  registering and unregistering the mapping's input, so a mapping keeps its
  gesture and never loses a pending press.
- **Picks** set Send/Receive mode's track, from the Send button and from a
  strip's select button, and mappings on the same presses that set
  `user:surface_mode` enter the mode, replacing `TryEnterSendReceiveMode()` and
  the pick callback.
- **Tap** is a press behavior in `device`, replacing `send_press_mode_`,
  `send_press_time_`, and `ApplySendRelease()`.
- **Mode lights** become mappings in the mode views.

After this, `PluginSurface` has only host plumbing (see
[extension_host.md](extension_host.md)) and its mappings, which *Build the
scene from a SurfaceSpec* turns into data.

This replaces the config model's *Exclusive group*
([config_model.md](../config_model.md)) with an enumerated value, which makes
"exactly one" true by construction, rather than enforced across several
toggles.

## Behavior

Nothing changes, apart from:
- Marker and Nudge are one value, so they are exclusive however they are
  changed, not just from their buttons.
- The Send/Receive light updates when the selected track's routes change, not
  only when the selection or track list does.
- Releasing Send within 350ms of pressing it is always a tap. Today, holding
  Send to pick a track cancels the release, so two quick cases differ:
  - Holding Send, pressing select on a track without routes, and releasing
    Send enters Send/Receive mode for the selected track, if it has routes.
  - Releasing Send in the frame right after a pick (within ~33ms), before the
    mode views switch, runs Track mode's tap too, which picks the selected
    track instead.
- Deleting the Send/Receive track leaves the surface in Send/Receive mode,
  showing nothing until another track is touched, rather than returning to
  Track mode. This is rare, and harmless: the mode follows the next track
  touched, and the Track button leaves it.

## Design

### Names

- **Tap** (`InputConfig::PressBehavior::kTap`): the config model's name. It is
  a press behavior like long and double press.
- **`press_toggles`** (`ViewMapping::ReadConfig`): a press on an enumerated
  property toggles between the ends of its range, rather than stepping through
  it.
- **Enumerated value** (`EnumeratedValueProperty`): an enumerated property that
  holds its own value, as `ToggleValueProperty` holds a toggle, with a **name**
  for each value.
- **`user:surface_mode`**, with values **`track`** (the default) and
  **`send_receive`**.
- **`user:transport_step`**, for what Rewind and Forward step by, with values
  **`measure`** (the default), **`beat`** (Nudge), and **`marker`** (Marker).
- **Track pick** (`TrackPickProperty`): named like `TrackAnchorProperty`, for
  what it picks. **`user:pick_selected_track`** is the top level pick (tapping
  Send), and **`user:pick_track`** each strip's (select while Send is held),
  replacing `user:pick_send_receive_track`.

### Where conditions go

- **A view's condition** switches a whole set of mappings, such as a mode.
  Leaving a view unregisters its mappings.
- **A mapping's condition** decides whether that mapping acts right now. For a
  write, it picks which property drives the control. For a read, it is
  checked when the input arrives, and the mapping keeps its input either way.
- **A component** takes a condition only for a rule about state it owns, which
  must hold however that state is changed. None does yet.

So a condition that says whether a button does something goes on its mapping,
not in the property the mapping triggers.

### device: Tap (control_input_handle.h, control.h/.cc)

```
enum class PressBehavior {
  ...
  // Delivered when the button is released, if it was held for less than a long
  // press. A button that holds a modifier (a press_release mapping) can also
  // act when it is tapped. A tap never defers the other presses in its group.
  kTap,
};
```

- `PressGroup` gains `tap_ids`, and the time its button was pressed. Only long
  and double press defer a normal press, as today, so the `press_release`
  mapping that holds `mod:send_hold` still turns on at once.
- Rebuilding the press groups, as when the view holding a tap mapping
  deactivates, drops a pending tap. So a mode change while Send is held drops
  its tap, as today.
- Without release support, a tap is delivered on the press, as every press is
  then a tap.
- Costs nothing per run: the work is on the press and the release.
- `Control` runs on REAPER's clock, so it has no unit test. The tap is tested
  in REAPER once the Send button uses it (CL7).

**Brittleness:** none. Whatever else the button does while it is held, a tap
is only its timing.

### scene: Read conditions checked when the input arrives (view_mapping.h/.cc)

- A read mapping registers its input while its view is active, whatever its
  condition. `ReadControl()` does nothing while the condition isn't met.
- A held (`press_release`) mapping still applies the release of a press it
  applied, even if its condition changed in between, so a held modifier can't
  stick on.
- The write side is unchanged: it writes, and holds the control's output, only
  while the condition is met.
- A registered read still counts in its control's modifier sets and press
  groups while its condition isn't met: a Send+select that picks nothing does
  nothing, rather than falling through to select the track. Leaving a view is
  what unregisters mappings.
- `ViewMapping::Config::condition`'s note about losing pending presses goes, as
  conditions no longer re-register inputs.
- The only read conditions today are the automation buttons', which behave the
  same.

### scene: Conditions that compare values (view_condition.h, view_property.h/.cc)

```
struct Config {
  std::string property;

  // The value the property must equal, such as true for a toggle, or an int
  // for an enumerated property.
  ViewProperty::Value value = true;
};
```

- **`ViewProperty::Equals(value)`** reads the property as the value's type,
  with its adapter functions (`GetBool()`, `GetInt()`, `GetText()`,
  `GetColor()`), and compares. A double is read in the property's own range:
  `GetPan()` for a pan property, `GetVolume()` for a volume property, and
  `GetNormalized()` otherwise. A `TimelinePosition` is only compared with a
  timeline position property (it gains `operator==`), and an empty value is
  never equal.
- A bool compares `GetBool()`, which is today's rule, so every condition
  behaves the same. Mode overrides use `Equals()` too, replacing
  `PropertyValueEquals()` (an exact match of the same type) in
  view_mapping.cc. Every override today is a bool on a toggle, which reads the
  same either way.
- Any value can be compared with any property, so no value is an error. A
  value that the property never reads as, such as an int on a toggle (whose
  `GetInt()` is 0), is never met, rather than being caught when the view or
  mapping is added.
- The rest of *Conditions that compare values* in the
  [backlog](../backlog.md) (not equal, less, greater) stays there.

### scene: press_toggles (view_mapping.h/.cc)

```
// For an enumerated property with a press input: without press_toggles, a
// press steps to the next value in [property_min, property_max], wrapping
// around (such as the timecode display's modes). With it, a press sets
// property_max, or property_min if the value already is property_max.
bool press_toggles = false;
```

Marker is `[measure, marker]` with `press_toggles`, and Nudge `[measure,
beat]`. `ReadConfig`'s range comment is corrected to match.

**Setting one value.** With the same min and max, a press sets that value, for
every property type but toggles (whose press always flips them). The Track
button is `[track, track]`, and the mappings beside the picks
`[send_receive, send_receive]`. `ReadConfig`'s range comment says so. A config
file (and possibly the SurfaceSpec) may offer a single value as shorthand for
this, but the runtime keeps only the range, as it is simpler.

### scene: EnumeratedValueProperty (value_property.h)

```
// An enumerated property that holds its own value, for state owned by the code
// that creates it. Mappings that read a control into it change it the same
// way.
class EnumeratedValueProperty final : public ViewProperty {
 public:
  // The value names, in order, which GetText() returns. Its values are 0 to
  // the number of names minus one, and it starts as `value` (clamped to them).
  EnumeratedValueProperty(std::string_view name,
                          std::vector<std::string> value_names, int value = 0);

  int GetMaxValue() const override;
  std::string GetText() const override;

  // Sets the value with the name, or reads other text as a number.
  void SetText(std::string_view value) override;
};
```

- Its type is `kEnumerated`, with values 0 to `GetMaxValue()`, the number of
  names minus one. With no names it has one value, whose text is empty.
- Header only, beside `ToggleValueProperty`.
- Setting it always sets the value asked for (clamped, as for any enumerated
  property), and notifies only on a change.
- Pure state, with no REAPER dependency, so it is unit tested.

**Brittleness:** code refers to values by index, so the plugin's constants
must match the order of its names. The plugin keeps each list beside its
constants, until the spec names values instead.

### scene: TrackPickProperty (track_pick_property.h/.cc)

```
// An action that picks a track: it sets a track reference to a source's track,
// if the source has a track. With no source, it picks its view's track, so it
// must be added to a view with a track subject.
class TrackPickProperty final : public ViewProperty {
 public:
  // The pick sets `reference` to the track of `source`, or of its view if
  // `source` is null. Both must outlive the pick.
  TrackPickProperty(std::string_view name, TrackReference& reference,
                    const TrackReference* source = nullptr);
};
```

- `Scene::GetTrackReference()` becomes public, as it is const, so a source can
  be a built in reference such as `state:selected_track`.
- With no source, it picks `View::GetTrack()`, as `TrackAnchorProperty` does,
  which is the stub track (never existing) in a view without a track subject.
- A pick acts only if the source's track exists.
- Whether a pick should happen (such as `has_routes`) is its mapping's
  condition (see Where conditions go), and anything else the same press does,
  such as entering the mode, is another mapping on it (see Setting one
  value), with
  the same condition.
- The reference is a pointer, as only its owner can change it: a pick can't
  set a reference it wasn't given.

**Brittleness:** the pointers must outlive the pick, which the scene's member
order already guarantees.

### plugin: PluginSurface

- **Rewind and Forward**: `user:transport_step`. The Marker and Nudge buttons
  read it with `press_toggles`, and light with a `const:` on value and a
  condition on their value. Rewind and Forward switch on conditions instead
  of `mod:marker` and `mod:nudge`.
- **Send**: the root view holds `mod:send_hold` (`press_release`, as today).
  The Track mode view maps a tap to `user:pick_selected_track`, and another to
  `user:surface_mode` with the range `[send_receive, send_receive]`, both with
  the condition `state:selected_track.has_routes`. The Send/Receive mode view
  maps a tap to `view:child_route_toggle`.
- **Mode lights**: each mode view lights the mode buttons with `const:` on
  values. Track mode: Track blinks, and Send is lit from
  `state:selected_track.has_routes`. Send/Receive mode: Track is solid, and
  Send blinks. This keeps the current mode lit whether or not it is available.
- **Modes**: `user:surface_mode`. The mode views are enabled by its values,
  and the Track button sets `track` (`[track, track]`). Both picks set
  `user:current_track`, and a mapping beside each pick mapping sets
  `send_receive`. Each strip's pick and mode mappings have the condition
  `track:has_routes`. Mappings sync in order, and view conditions apply after
  the run, so the track is always set before the mode's view activates.

### To confirm

- **A mode change while Send is held drops its tap.** (CL7)
- **The Send/Receive light follows route changes** on the selected track, via
  `TrackProperties::OnTrackRoutesChanged()`. (CL8)
- **Cost of the mode lights.** Watching `state:selected_track.has_routes` in
  Track mode refreshes the selected track each run
  (`TrackReference::Update()`), which nothing did before. (CL8)

## CLs

### CL1 [x] device: Tap

Depends on: nothing.

- `PressBehavior::kTap` in `InputConfig`, and its handling in `Control`.
- Unused, so no visible change. It is tested in REAPER through CL7.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test). The smoke test covers the press, long press, and double
  press handling the tap sits beside.

### CL2 [x] scene: Read conditions checked when the input arrives

Depends on: nothing.

- `ViewMapping` registers reads while its view is active, checks the condition
  in `ReadControl()`, and applies the release of a held press it applied.
- No visible change.

**Verify**
- Standard checks.
- The automation buttons, with and without the global override: each press
  sets the selected tracks' mode or the override, as before, and switching the
  override in and out doesn't leave a button acting for the other state.

### CL3 [x] scene: Conditions that compare values, and press_toggles

Depends on: nothing.

- `ViewCondition::Config::value` becomes a `ViewProperty::Value`.
- `ViewProperty::Equals()`, used by conditions and mode overrides, replaces
  `PropertyValueEquals()`. `ViewProperty::GetValue()`, then unused, is removed.
- `ReadConfig::press_toggles`.
- The backlog's *Conditions that compare values* drops equality.
- No visible change: every condition and mode override today is a bool, and
  nothing sets `press_toggles`.

**Verify**
- Standard checks. The smoke test covers the existing conditions (the pan
  ring's overrides, the select light while Send is held, and the automation
  lights with and without an override), and the timecode display still cycles
  its modes.
- `jpr_scene_test`: new `view_property_test.cc` (`Equals()` for each value
  type, on properties of the same and other types) and `view_condition_test.cc`
  (met while the property equals the value, and changes while watched).

### CL4 [x] scene: Enumerated values

Depends on: nothing.

- `EnumeratedValueProperty`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- `jpr_scene_test` (new `value_property_test.cc`): the start value, setting and
  clamping, change notices only on a change, `GetMaxValue()`, and `GetText()`.

### CL5 [x] scene: Track picks

Depends on: nothing.

- `TrackPickProperty`, and `Scene::GetTrackReference()` made public.
- `ReadConfig`'s range comment notes that the same min and max set a value.
- `ViewProperty::SetValue()`, which nothing calls, is removed.
- Unused, so no visible change. It needs `TrackCache`, so it has no unit test,
  and is tested through CL9.

**Verify**
- Standard checks.

### CL6 [x] plugin: Rewind and Forward steps as an enumerated value

Depends on: CL2, CL3, CL4.

- `user:transport_step`, the Marker and Nudge buttons and lights, and Rewind
  and Forward on conditions. Removes `AddExclusiveToggleMapping()`,
  `user:toggle_*`, `mod:marker`, and `mod:nudge`.

**Verify**
- Standard checks.
- Marker and Nudge each toggle their light, and turning one on turns the other
  off. Rewind and Forward move by measure (neither), beat (Nudge), and marker
  (Marker).
- Performance: the `Run()` log line's avg against before the change.

### CL7 [x] plugin: Tap Send

Depends on: CL1.

- The Track mode view maps a Send tap to the Send button's `select` callback,
  which now enters Send/Receive mode for the selected track
  (`TryEnterSendReceiveMode()`) until CL9 removes it, and the Send/Receive mode
  view maps a Send tap to `view:child_route_toggle`. The root view keeps only
  Send's `mod:send_hold`.
- The strip pick no longer resets anything.
- Removes `send_press_mode_`, `send_press_time_`, `ApplySendRelease()`, and
  `kSendHoldDuration`.

**Verify**
- Standard checks.
- Tapping Send in Track mode enters Send/Receive mode for the selected track,
  if it has routes. Tapping it in Send/Receive mode toggles sends and receives.
- Holding Send for 350ms or more, then releasing it, does nothing.
- Holding Send, picking a track with routes, and releasing Send: the new mode's
  tap doesn't fire.
- Holding Send still lights the select buttons of tracks with routes at once,
  as `mod:send_hold` isn't deferred by the tap.
- The smoke test's select long and double presses, and Solo's long press, still
  work, as they share press group handling with the tap.

### CL8 [ ] plugin: Mode lights as mappings

Depends on: nothing.

- The mode views light the mode buttons with `const:` values and
  `state:selected_track.has_routes`, as described above.
- Removes the `available` toggles, `IsModeAvailable()`, `mode_buttons_changed_`
  and the `OnSelectionChanged()` override.

**Verify**
- Standard checks.
- Track mode: Track blinks. Send is solid when exactly one track with routes is
  selected, and off otherwise, following selection changes.
- Send/Receive mode: Send blinks and Track is solid, even after selecting a
  track without routes.
- Adding the first send to the selected track lights Send, and removing it
  turns it off.
- Performance: the `Run()` avg in Track mode, with and without one track
  selected, against before the change.

### CL9 [ ] plugin: Modes as an enumerated value, with picks

Depends on: CL2, CL3, CL4, CL5, CL7, CL8.

- `user:surface_mode`, the mode views enabled by its values, the Track button
  setting `track`, `user:pick_selected_track` (on the root view) and each
  strip's `user:pick_track`, the mappings beside them that set
  `send_receive`, and their `has_routes` conditions.
- Removes `SurfaceMode`, `kSurfaceModeCount`, `kModeInfo`,
  `GetModePropertyName()`, `ModeButton`, `mode_`, `mode_buttons_`,
  `InitModeButtons()`, `UpdateModeButtons()`, `EnterTrackMode()`,
  `EnterSendReceiveMode()`, `TryEnterSendReceiveMode()`, `FinishModeChange()`,
  `CanShowRoutes()`, the CL7 callback, the `OnTracksChanged()` override, and
  the `current_track_`, `track_mode_view_`, and `send_receive_mode_view_`
  members. The mode change log line goes too, as the scene logs each mode
  view's activation.

**Verify**
- Standard checks, and CL7's and CL8's feature checks again.
- Pressing Track in Track mode does nothing, and in Send/Receive mode returns to
  Track mode.
- Holding Send and pressing select picks a strip's track if it has routes, and
  otherwise does nothing (it doesn't select the track).
- Deleting the Send/Receive track leaves Send/Receive mode showing nothing,
  touching another track shows its routes, and Track returns to Track mode.
- Performance: the `Run()` avg in each mode against before the change, and the
  mode switch durations in the view activation log lines.

When the feature is done, this plan becomes a summary, and
[config_model.md](../config_model.md) and [surface_modes.md](surface_modes.md)
are brought up to date: Exclusive group becomes Enumerated value, the pick
loses its field and no longer turns the mode on (another mapping on the same
press does, with a single value range), Tap loses its held modifier rule, Where conditions go is
added, the caveat about read conditions under Sharing a control goes, and the
"Today" notes follow the code.
