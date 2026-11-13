# Mapping Actions

What a button does to a track is decided in code. The `ui_` properties
(`track:ui_mute`, `track:ui_rec_stage`, and the rest) each run one fixed bundle
of behaviors, chosen by fixed modifiers: Ctrl for this track alone, Opt for the
selected tracks, Shift for a range, Alt to clear every track, and so on. A
config can't change which modifier means what, leave a behavior out, or change
one. Trying the record stages on the hardware showed the need: Rec wants a
double press that records and sends every other recording track back to
monitoring, no Opt, and no Ctrl+Alt. None of that can be said in a config.

This feature lets a mapping's options say what a read does, in three parts:
which tracks (a scope, such as `grouped`), how they get the new value (an
action, such as `set(value = record)`), and how every other track's value
changes (an action after `others`, such as `others limit(max = monitor)`). A
`when` in the options can override any of them while its condition is met, as
it overrides the mode today. Every behavior a `ui_` bundle has is one such
mapping, so a bundle becomes mappings in the config, one for each modifier set,
shared through a template. The `ui_` properties and their modifier rules then
go away, as do the options `set` and `toggle`, which become action kinds. A
track anchor becomes a reference that a scope starts from. The X-Touch example
keeps every behavior it has today, except Rec, which gets the new
workflow.

## Behavior

### Rec in the X-Touch example

| Gesture                          | Today                                                            | After                                                                                                                                   |
| -------------------------------- | ---------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| Press                            | Off to monitor, or back to off, with the group                   | The same                                                                                                                                |
| Double press                     | Monitor to record, or record back to monitor, with the group     | Record, with the group, and every other recording track goes back to monitor; or off, with the group, if it records and no track outside the group does |
| Ctrl + double press              | Record or monitor, this track alone                              | Record, or off if it records, this track alone                                                                                          |
| Ctrl + press                     | Off or monitor, this track alone                                 | The same                                                                                                                                |
| Shift, Ctrl+Shift + press        | The range from the last touched track                            | The same                                                                                                                                |
| Alt + press                      | Every track off                                                  | Every recording track back to monitor, still armed                                                                                      |
| Alt + double press               | Every recording track back to monitor, still armed               | Every track off                                                                                                                         |
| Opt, Ctrl+Alt, Shift+Alt + press | The selected tracks; this track alone recording; its group alone | Nothing                                                                                                                                 |
| Hold, and press another          | The range, at the held track's stage                             | The same                                                                                                                                |

A double press on a recording track with other tracks recording sends them back
to monitor and keeps recording, by `toggle`'s rule with `others` (see Action
kinds), as a press of select selects only its track before unselecting it. The
next double press turns it off.

### Other changes from today

- **A held anchor no longer overrides the modifiers.** Today, while an anchor
  is held, a press on another track sets the range, whatever modifiers are
  held. After, the range is a mapping of its own, which requires the anchor's
  modifier, so holding Ctrl with it does nothing (see Modifier sets in the
  spec). A mapping can still ignore modifiers, to keep the old behavior (see
  the brackets, below).
- **A modifier only matters to a mapping that requires it.** Today a `ui_`
  property reads the modifiers itself, so Ctrl on the pot button, which no
  mapping requires, still pans the track alone. After, the X-Touch example has
  a Ctrl mapping wherever a `ui_` property used Ctrl, so nothing it does
  changes.
- **The pot button centers its group.** Today it centers this track, and moves
  its grouped tracks by the same amount. After, it is `grouped, set(value =
  0.0)`, which centers every grouped track, and Ctrl still centers this track
  alone.
- **A range on a read stays**, as `range(min = a, max = b)`, with every
  behavior today's `range a .. b` has. Nothing in the X-Touch example uses it.
- Everything else in the X-Touch example behaves as it does today. The new
  scopes on select, volume, and pan are there for configs to use; the example
  uses only the ones its buttons have today.

## Design

### A mapping's options

```
<control> [<how it is read>] <arrow> <property or value> { <options> }
```

The braces hold statements. Each is one kind name, with its parameters in
parentheses if it has any, so the kind alone says what the statement is:

| Statement                          | `->` | `<-` | `<->` | What it says                                                   |
| ---------------------------------- | ---- | ---- | ----- | -------------------------------------------------------------- |
| A scope kind                       | yes  | no   | yes   | Which tracks get the new value. Without one, this track.      |
| An action kind                     | yes  | no   | yes   | How they get it. Without one, `set`.                          |
| `others` and an action kind        | yes  | no   | yes   | How every other track's value changes. Without one, nothing.  |
| `range(min, max)`                  | yes  | no   | yes   | The values the control maps to. Without one, the whole range. |
| `mode <name>`                      | no   | yes  | yes   | The output mode. Without one, the control's first.            |
| `when <condition> { <options> }`   | yes  | yes  | yes   | Overrides the statements it holds while its condition is met  |

- **One of each.** Outside a `when`, a mapping has at most one scope, one
  action, one `others` action, one range, and one mode, and a second of any is
  an error. Inside one `when`, the same.
- **`when` overrides.** For each of the five parts, the first `when` that holds
  one and whose condition is met gives it, and otherwise the mapping's own
  applies. That is the rule for mode overrides today, applied to every part. A
  `when` in a `when` is an error, as today. A read override is checked only
  when an input arrives, so it costs nothing per run, unlike a mode override,
  which watches its property.
- **A mapping's own condition** is still a `when` around it, in a `mappings`
  block: `when` goes around what it applies to. Around a mapping, it says
  whether the mapping acts at all; in its options, it says which of its
  statements apply.
- **Separators.** A comma may follow any statement, in every `{ }` body, not
  only a mapping's options, and is good style between statements on one line.
  Requiring it there needs a parser feature (see config: The syntax).
- **Names.** Scope kinds, action kinds, and the statement words (`others`,
  `range`, `mode`, `when`) share one set of names, so no name is ever two of
  them. Parentheses are left out when a kind has no parameters given, as
  for `view <name>[(<params>)]`.
- **Flags.** In any parameter list, a parameter's name alone is short for
  `= true`, as `when <property>` is short for `= true`: `toggle(each)`, or
  `from(track = user:rec_anchor, subfolders)`. This holds for components,
  devices, and views too.

```
strip.rec [double] -> track:rec_stage {
  grouped, toggle(on = record, off = off)
  others limit(max = monitor)
}
strip.fader <-> track:volume { grouped, scale }
strip.pot_button -> track:pan { grouped, set(value = 0.0) }
assign_track -> user:surface_mode { set(value = track) }
strip.rec <- track:rec_arm {
  mode blink
  when track:rec_stage = monitor { mode solid }
}
```

**The brackets** say how a read mapping reads its control, unchanged except for
`ignore`:

| In the brackets                 | What it means                                                |
| ------------------------------- | ------------------------------------------------------------ |
| `value`, `delta`, `press`       | Reads only that input                                        |
| `long`, `double`, `tap`         | The press behavior                                           |
| `held`                          | On while held, and off when released                         |
| `touch`                         | Reads whether a hand is on the control                       |
| A modifier, such as `mod:shift` | Must be on (see Modifier sets in the spec)                   |
| `ignore(<modifiers>)`           | New: modifiers that don't count against this mapping's set   |

`ignore(...)` is the backlog's "Modifiers a mapping ignores": a mapping
requiring `mod:send_hold` with `ignore(mod:select_anchor)` fires whether or not
the select anchor is on, so it needs no copy for it.

A `held` mapping takes a scope, but no action: it is on (true) while held, and
off when released, which is how it reads, not what it does. Its scope is the
tracks it holds on and off together, such as a fader's touch on every track the
fader moves:

```
strip.fader [mod:opt] -> track:volume { selected, scale }
strip.fader [touch, held, mod:opt] -> track:volume_touched { selected }
```

**The range** is how the control's value maps onto the property, which is
today's `range a .. b`, unchanged except for its syntax. Scope and action are
about the property, and the range about the control, so it is a statement of
its own:
- A value input maps onto `min` to `max`, and a delta stays within them.
- A press is the type's own press (see Action kinds) between them: a pan,
  volume, or normalized value at either end goes to the other, and from
  between them to the nearer end, and a pan whose range spans the center steps
  through `min`, the center, and `max`. Text and color go to `max` unless they
  are already there, and an enumerated value steps through them, wrapping
  around. If `min` and `max` are the same, a press sets that value.
- A `held` mapping is `max` while held, and `min` when released.
- Either end may be left out, for the property's own.

The range decides only this track's new value, before the scope and action put
it on other tracks. So `grouped, scale` on a volume or pan still hands the
ranged value to REAPER's grouped setter, and REAPER moves the group exactly as
it does without a range. A range with an action that doesn't read the control
(`toggle`, `step`, `copy`, `limit`, or `set` or `scale` with a `value`) is an
error. On an enumerated press, `range(min = a, max = b)` and `step(min = a, max
= b)` do the same: the range means the same on every input and type, and
`step` is for when nothing is said about the control.

### Action kinds

A read gives this track a new value, and the action puts it on every track in
the scope. The action is about the property only, not the input: the control's
value is what the mapping reads today, whatever the control is.
- **The control's value:** a value input maps onto the range (the property's
  whole range without one), a delta moves this track's value within it, and a
  press is the type's own press (below), within it.
- **The action** either takes the control's value, or a `value` it is given,
  or decides one itself from this track's value.

| Kind     | Parameters           | The new value                                    | How each track in the scope gets it                                                           |
| -------- | -------------------- | ------------------------------------------------ | --------------------------------------------------------------------------------------------- |
| `set`    | `value`              | `value`, or without one, the control's           | Exactly                                                                                       |
| `scale`  | `value`              | `value`, or without one, the control's           | It moves by this track's change since their starts (below): the same amount for a pan or normalized value, and the same dB for a volume, kept within its range |
| `toggle` | `on`, `off`, `each`  | `on`, or `off` if this track is already `on`     | Exactly, or with `each`, each track decides from its own value                                |
| `step`   | `min`, `max`, `each` | The one after this track's, from `min` to `max`, wrapping around | Exactly, or with `each`, each track steps from its own value                    |
| `copy`   | `track`              | The value of the track `track` refers to (a track reference) | Exactly                                                                           |
| `limit`  | `max`                | None                                             | A value past `max` goes to `max`, and the rest stay                                           |

- **`set` and `scale`** are the same on this track alone. With a wider scope,
  `set` puts exactly the new value on every track, and `scale` keeps each
  track's offset from this one, as REAPER moves the selected tracks when one's
  fader is dragged. The X-Touch fader is `grouped, scale`, as it is today, and
  its pot button `grouped, set(value = 0.0)`, which centers every grouped
  track. `grouped, scale(value = 0.0)` would center this track and move its
  group by the same amount, as the pot button does today.
- **Starts.** `scale` moves each track from its start, not from where the
  last move left it, exactly as REAPER moves grouped tracks (see To confirm):
  a track's start is where it was when this track began leading it, so taking
  this track to silence and back, or pushing another track against the end of
  its range and back, puts every track back where it was. It has nothing to do
  with the control or its touch.
  - The starts are taken again, from where the tracks are, when another track
    leads, or when this track was changed other than by its own `scale` (alone,
    in REAPER, or by another mapping).
  - A track that was changed some other way, or wasn't in the scope at the
    last move (one selected since, say), takes a new start of its own, and the
    others keep theirs.
  - Each property has one set of starts, shared by every mapping that scales
    it, as REAPER has one for a volume and one for a pan. A change is found by
    value: a track that isn't at the value `scale` last gave it was changed.
  - The starts are scene state, so loading a config again forgets them.
    `grouped, scale` on a volume or a pan is REAPER's own, so REAPER keeps
    those.
- **A volume's change** is the same in dB, which is the same ratio of gains.
  As in REAPER, silence counts as -200 dB: taking this track to silence takes
  the others to tiny gains that read as -inf, and taking it up again brings
  them back. One that starts at silence moves the others by its whole rise
  from -200 dB.
- **`toggle` has no order.** `on` and `off` are any two values: it goes to
  `on` unless this track is already there, and then to `off`, whichever comes
  first in the type's order. So `toggle(on = off, off = record)` is as valid
  as the other way around, and means the opposite.
- **`toggle` with `others`.** A toggle goes to `off` only if this track is
  `on` and the `others` action would change nothing. Without an `others`
  action, that is just "is `on`". This is select's rule today: a press selects
  only this track, or unselects it if it was already the only one selected.
- **`others`** takes `set`, with a `value`, and `limit`.
- **Values.** `value`, `on`, `off`, `min`, and `max` are values of the
  property's type, as a `range` end is. `on` and `off` default to true and
  false, or an enumerated type's last and first values. `min` and `max`, which
  have an order (`step`'s values between them, and what is past `limit`'s
  `max`), default to the type's whole range. `value` has no default. "Past" is
  in the value order: true is past false, an enumerated value is past the ones
  before it, and a number is past the smaller ones.

What each type takes, and a press's own value:

| Type               | Action kinds                                    | A press's own value                                                                 |
| ------------------ | ----------------------------------------------- | ----------------------------------------------------------------------------------- |
| action             | none                                            | It triggers the action                                                              |
| toggle             | `set`, `toggle`, `copy`, `limit`                | It flips                                                                            |
| enumerated         | `set`, `toggle`, `step`, `copy`, `limit`        | The next value, wrapping around                                                     |
| pan                | `set`, `scale`, `toggle`, `step`, `copy`, `limit` | -1.0, the center, and 1.0 in turn, as a pan's press does today                    |
| volume, normalized | `set`, `scale`, `toggle`, `copy`, `limit`       | The other end of its range, as today                                                |
| text, color        | `set`, `toggle`                                 | None: a press needs a `value`, `toggle`'s `on` and `off`, or a range's              |
| timeline position  | `set`                                           | None: it reads only a delta                                                         |

`copy` takes a track reference, so only a track property takes it. `step` on a
pan takes `min` and `max`, and steps through the center if they span it.

### Scope kinds

A scope picks the tracks that get the new value, from this track. Every track
property that can be written takes every scope, except a track's name, and its
touches without `held`; an error says so for any other. Route properties take
none yet: the backlog's "Ranges in Send/Receive mode" would give them the same
kinds, meaning routes (`all` the routes, and so on), as a scope's meaning comes
from the property's subject.

| Kind                                     | The tracks                                                                                             |
| ---------------------------------------- | ------------------------------------------------------------------------------------------------------ |
| `this`                                   | This track alone, ignoring grouping and ganging. The default.                                          |
| `grouped`                                | This track, and the tracks grouped or ganged with it, as REAPER's UI reaches them                      |
| `selected`                               | This track, and if it is selected, every other selected track                                          |
| `all`                                    | Every track in the project                                                                             |
| `from(track = <reference>)`              | The tracks between that track and this one, in the track list the surface shows, that share its parent |
| `from(track = <reference>, subfolders)`  | The same, including the tracks in folders between them                                                 |

- **`grouped`** reaches what REAPER's groups reach for the property: for the
  record and monitoring modes, what their property describes, and for
  selection, what To confirm finds. On a volume or pan, `scale` is REAPER's own
  grouping, offsets and VCAs included, exactly as today. `set` sets each
  grouped track to exactly the value: the tracks REAPER's volume or pan groups
  reach from this one, by the same lead and follow rule as a grouped record
  arm. A volume's VCA followers are left out, as their faders don't move with
  their lead, so `set` never fights REAPER's VCA. A pan has no VCA.
- **`from`** does nothing if either end isn't in the surface's track list, or
  the reference refers to nothing. Its `track` is any track reference: the last
  touched track (`state:last_touched_track`), or an anchor (below).
- **`others`** are every track in the project that the scope doesn't pick, on
  the surface or not, the master track included.
- **The last touched track:** a mapping sets it to this track, as REAPER's UI
  does, except with `from` or `all`, with `selected` on a track that isn't
  selected, when it unselects this track, and on a volume or pan. That is
  exactly what the `ui_` properties do today.
- **Undo points:** a press's changes are one `TrackBatch`, so one undo point,
  named as today. A fader's volume or pan changes, on any number of tracks,
  are REAPER's one held surface undo point, which the fake already models.
- **Cost:** a scope sets each of its tracks, so `scale` with `all` on a large
  project makes a setter call per track per fader move. The deferred refresh
  pays REAPER's UI refresh once per run, and the call count tests bound it.

### Examples

| Today                                              | After                                                                 |
| -------------------------------------------------- | --------------------------------------------------------------------- |
| `-> track:ui_pan { set 0.0 }`                      | `-> track:pan { grouped, set(value = 0.0) }`, and `[mod:ctrl] -> track:pan { set(value = 0.0) }` |
| `-> user:surface_mode { set track }`               | `-> user:surface_mode { set(value = track) }`                         |
| `-> user:transport_step { toggle measure .. beat }`| `-> user:transport_step { toggle(on = beat, off = measure) }`        |
| `{ range a .. b }`                                 | `{ range(min = a, max = b) }`                                         |
| `<-> track:ui_volume`                              | `<-> track:volume { grouped, scale }`, and `[mod:ctrl] -> track:volume` |

Mute and solo share a template in the X-Touch example, which is Mute's
modifiers today, written out:

```
mappings track_toggle(widget button, property flag) {
  button <-> flag { grouped }
  button [mod:ctrl] -> flag
  button [mod:opt] -> flag { selected, toggle(each) }
  button [mod:shift] -> flag {
    from(track = state:last_touched_track)
    copy(track = state:last_touched_track)
  }
  button [mod:shift, mod:ctrl] -> flag {
    from(track = state:last_touched_track, subfolders)
    copy(track = state:last_touched_track)
  }
  button [mod:alt] -> flag { all, set(value = false) }
  button [mod:alt, mod:shift] -> flag {
    grouped, set(value = true)
    others set(value = false)
  }
  button [mod:alt, mod:ctrl] -> flag {
    set(value = true)
    others set(value = false)
  }
}
```

Rec, in `track_strip`:

```
strip.rec -> track:rec_stage { grouped, toggle(on = monitor, off = off) }
strip.rec [mod:ctrl] -> track:rec_stage { toggle(on = monitor, off = off) }
strip.rec [mod:shift] -> track:rec_stage {
  from(track = state:last_touched_track)
  copy(track = state:last_touched_track)
}
strip.rec [mod:shift, mod:ctrl] -> track:rec_stage {
  from(track = state:last_touched_track, subfolders)
  copy(track = state:last_touched_track)
}
strip.rec [mod:alt] -> track:rec_stage { all, limit(max = monitor) }
strip.rec [double] -> track:rec_stage {
  grouped, toggle(on = record, off = off)
  others limit(max = monitor)
}
strip.rec [double, mod:ctrl] -> track:rec_stage {
  toggle(on = record, off = off)
}
strip.rec [double, mod:alt] -> track:rec_stage { all, set(value = off) }
strip.rec <- track:rec_arm {
  mode blink
  when track:rec_stage = monitor { mode solid }
}
```

Select, in the track list's strip view:

```
strip.select <-> track:selected { toggle, others set(value = false) }
strip.select [mod:ctrl] -> track:selected
strip.select [mod:shift] -> track:selected {
  from(track = state:last_touched_track), set(value = true)
  others set(value = false)
}
strip.select [mod:shift, mod:ctrl] -> track:selected {
  from(track = state:last_touched_track, subfolders)
  set(value = true)
  others set(value = false)
}
strip.select [long] -> track:selected { toggle, others set(value = false) }
strip.select [mod:select_anchor] -> track:selected {
  from(track = user:select_anchor), set(value = true)
  others set(value = false)
}
```

The Send/Receive pick, which needs no copies for the select anchor:

```
when track:has_routes {
  strip.select [mod:send_hold, ignore(mod:select_anchor)] -> user:pick_track
  strip.select [mod:send_hold, ignore(mod:select_anchor)]
      -> user:surface_mode { set(value = send_receive) }
}
```

### Anchors

An anchor today is held on a view's track by a `track_anchor` component
declared on the view, and `TrackActions` keeps one per action, which the `ui_`
properties check before their modifiers. With the range a scope of its own, an
anchor is just the track the range starts from, so the component stays where it
is, and loses its `action`:

```
components {
  track_anchor user:rec_anchor(modifier = mod:rec_anchor)
}
mappings {
  strip.rec [held] -> user:rec_anchor
  strip.rec [double, held] -> user:rec_anchor
  strip.rec [mod:rec_anchor] -> track:rec_stage {
    from(track = user:rec_anchor)
    copy(track = user:rec_anchor)
  }
}
```

- **One anchor for each declaration.** Every instance of the view that declares
  it shares the one anchor, so the eight strips hold one Rec anchor, as today's
  share one per action. Two declarations are two anchors, so a config can have
  as many as it wants.
- **Its name has two uses.** As a mapping's property, it is this view's toggle:
  while it is on, it holds the anchor on the view's track. As a `track`
  parameter, it is the anchor's track, whichever view holds it, or nothing. The
  catalog knows it is an anchor, so each is unambiguous.
- **Holding.** The toggle turns off, releasing the anchor, when it is written
  false, or when the view's subject changes. Holds are first come: a hold while
  another view holds the anchor fails, and the toggle stays off, so it is on
  only while it holds the anchor. Its `modifier` is on while the anchor is
  held.
- **Only the views that declare it can name it**, as a mapping finds names in
  its own view and its parents, never in a child view.
- **A held mapping** makes the hold last as long as the press, as in the
  example. Any toggle mapping can write it, so a plain press latches it
  instead. While a latched anchor is held, its modifier is on, so the button's
  own press needs a mapping that requires the modifier to release it, beside
  the range on the same gesture, which is harmless: a range from the anchor's
  own track is that track alone, and changes nothing.
- Mute and solo get anchor modifiers in the X-Touch example, as the press on
  the other track needs one to require.

### Names

| Name                                                   | What it is                                                  | Might be confused with                                           |
| ------------------------------------------------------ | ----------------------------------------------------------- | ---------------------------------------------------------------- |
| scope, action                                          | The two kinds of statement a read's options have (in docs)  | An action property, which a press triggers                       |
| `this`, `grouped`, `selected`, `all`, `from`           | Scope kinds                                                 | `track:selected`; `view:to_parent_track`                         |
| `track`, `subfolders`                                  | `from`'s parameters                                         |                                                                  |
| `set`, `scale`, `toggle`, `step`, `copy`, `limit`      | Action kinds                                                | The old options `set` and `toggle`; the toggle type; `scale` as multiplying |
| `value`, `min`, `max`, `each`, `track`                 | Action parameters                                           |                                                                  |
| `others`                                               | The prefix for the other tracks' action                     | A modifier, which is a `mod:` property                           |
| `ignore(...)`                                          | Modifiers a mapping doesn't count                           |                                                                  |

All of them are open to wordsmithing before the config CL makes them public.
`track` means a track reference wherever it is a parameter's name.

### device: Modifiers an input ignores

`InputConfig` gains `ignored_modifiers`. A registration's off mask (the
modifiers required by the control's other registrations, which must be off)
leaves them out, so it fires whether they are on or not. Press groups are keyed
by the masks, so a press group is found the same way.

### scene: Actions, scopes, and read overrides

```
// Which tracks get a read's new value (see Scope kinds).
struct ActionScope {
  enum class Kind { kThis, kGrouped, kSelected, kAll, kFrom };

  Kind kind = Kind::kThis;
  std::string track;  // kFrom: the track reference's name.
  bool subfolders = false;
};

// How a read's new value is decided, and put on its scope (see Action kinds).
struct ValueAction {
  enum class Kind { kSet, kScale, kToggle, kStep, kCopy, kLimit };

  Kind kind = Kind::kSet;
  std::optional<ViewProperty::Value> value, on, off, min, max;
  std::string track;  // kCopy: the track reference's name.
  bool each = false;
};

// The values a read maps its control to (see The range), each end unset for
// the property's own.
struct ReadRange {
  std::optional<ViewProperty::Value> min, max;
};

// What a read does, each part unset for its default (or, in an override,
// for the mapping's own).
struct ReadAction {
  std::optional<ActionScope> scope;
  std::optional<ValueAction> action;
  std::optional<ValueAction> others;
  std::optional<ReadRange> range;
};
```

- `ReadConfig` gains `action` (a `ReadAction`), `overrides` (a condition and a
  `ReadAction` each, in order), and `ignored_modifiers`. `action` replaces
  `press_toggles`, and its `range` holds `property_min` and `property_max`, so
  a `when` can override it like any other part.
- **Any property:** the mapping reads the control's value as today, over the
  range, and decides `set`, `toggle`, `step`, and `limit` itself, from the
  property's value. It picks the parts from its overrides when an input
  arrives. Today's read functions, which take the range when they are made,
  are made once for the mapping's own range and once for each override that
  holds one, so picking one costs nothing more.
- **Track properties:** a property that takes scopes is given the new value,
  this track's old one, and the `ReadAction` to run (generalizing
  `ViewProperty::PressInt()` and `EnumeratedPress`, which it replaces).
  References in it are resolved when the mapping is created, in its view, and
  read when it fires.
- **The catalog** lists the scope and action kinds with their parameters, and
  which each property takes, so creating the scene rejects a part a property
  doesn't take, and the generated pages show them: a new
  `docs/user/scene/actions.md` for the kinds (as `components.md` has the
  component kinds), and the track page's scopes. `config_spec.md` defines only
  the statements, and links there (see "Spec, not catalog").
- **`TrackActions`** runs a `ReadAction` on a track and a `TrackValue`. The
  toggles become `TrackValue`s too (two values, false and true), so mute, solo,
  record arm, and select take the same path as the enumerated values. Volume
  and pan take every scope: `grouped, scale` through the `TrackGrouping` they
  already have, and the others through `TrackBatch`'s setters, one track at a
  time, `scale` computing each track's value from this track's change since
  their starts. The starts are a small class of the scene's, one for each
  property scaled: the lead, and each track's start and the value `scale` last
  gave it, which a move checks, so nothing is polled. `grouped, set` finds the grouped tracks from REAPER's volume and pan group
  flags, as `TrackValue::GetGroup()` does for a record arm, which is for a
  press, not a run, as it reads REAPER's groups.
- **Anchors** move from `TrackActions` to the `track_anchor` component, one
  `Anchor<Track>` for each declaration.
- The `ui_` properties' modifier rules become a table, from each modifier set
  to the `ReadAction` it runs, so the `ui_` tests pass unchanged against the
  actions. This shows the actions cover every behavior before the example
  moves to them.

**Brittleness:** the last touched track's rule is a list of exceptions, kept in
one function with a test for each, rather than a parameter, so no mapping has
to remember to set it. No scope kind, action kind, or statement word may share a
name, which the catalog checks with a test.

### config: The syntax

```
option {
  $mode=("mode" $name=%ident);
  $when=("when" $condition=condition "{" $items=option* "}");
  $others=("others" $kind=%ident ["(" $params=param,* ")"]);
  $kind=($kind=%ident ["(" $params=param,* ")"]);
}

param {
  $name=%ident ["=" $value=value];
}
```

- Each statement, in every body, may be followed by a comma. The reader looks a
  kind's name up in the scene's catalog to tell a scope from an action, and
  `range` is a name of its own, which no scope or action kind may take.
- The brackets gain `$ignore=("ignore" "(" $modifiers=%qualified_name,+ ")")`.
- **Requiring the comma** between statements on one line can't be checked from
  each statement's first line, as a statement can span lines, and tokenizing
  every line break would need `%linebreak*` wherever one is allowed. The parser
  already knows whether a token touches the last (`^`); the same for lines, a
  match that succeeds without consuming anything when the next token starts a
  new line, would let a list take `","` or a line break as its separator. That
  is a Game Bits backlog item, with JPRSurf's own item to require the comma
  once it is done.

### plugin: The X-Touch example

The example moves from the `ui_` properties to `track:` properties with
scopes and actions, as in the examples above: the `track_toggle` template for
mute and solo, Rec's mappings in `track_strip`, select's, the fader's, pot's,
and pot button's with their Ctrl mappings, an anchor modifier for each
anchor, and the Send/Receive pick with `ignore(...)`. `xtouch.md` changes only
for Rec.

**Performance:** a `ui_` mapping becomes one mapping for each modifier set,
about 25 more on each strip, so about 200 more in Track mode, which syncs every
mapping on every run (`View::SyncMappings()`). The record stages' 64 more cost
about 6% of an idle Track mode run, so this may cost about 20%. That is a guess
from one data point; the benchmark measures it. Most of the new mappings only
read, so if the cost is too high, the likely fix is not syncing read-only
mappings on every run, which would be a backlog item to measure first.

### To confirm

All checked (CL1):
- **Selection grouping** (the user's REAPER). Selecting a grouped track
  selects only that track: grouping doesn't affect selection at all, and its
  group flags have none for it (`GetSetTrackGroupMembership()` has media edit,
  volume, VCA, pan, width, mute, solo, record arm, polarity, and automation
  mode). So `grouped` on `track:selected` reaches the tracks that share any
  group with this one.
- **A grouped fader's memory** (the user's REAPER, then a temporary contract
  test in the test install, of `CSurf_OnVolumeChangeEx()` and
  `CSurf_OnPanChangeEx()` with `allow_gang`). It isn't the touch: REAPER moves
  each follower from a start, not from where the last move left it.
  - While the same track leads, each follower is its start times the lead's
    ratio since then (a volume), or its start plus the lead's change since
    then (a pan). Clamped to its range (+12 dB, and -1 to 1), it comes back
    from an end it went past, as it does from silence, however long between
    the moves, and whether or not the fader was released.
  - The starts are taken again, from where the tracks are, when another track
    leads, or when the lead was changed some other way, such as alone. A
    follower that was changed some other way takes a new start of its own,
    and the others keep theirs.
  - A volume and a pan have separate starts: a pan move by another track
    leaves the volume's.
  - Selected tracks (ganging) move the same way.
  - The lead's own value isn't clamped: REAPER takes a volume of 1000.
- **A grouped fader from silence** (the test install). Silence counts as -200
  dB (a gain of 1e-10) in the ratio, for the lead and for a follower's start.
  Taking the lead to silence leaves its followers at tiny gains (such as
  -206 dB), which read as -inf, and taking it up again brings them back, so
  a lead that starts at silence moves its followers by its whole rise from
  -200 dB, to +12 dB at once. (Under -200 dB, REAPER's followers differ by
  case, such as a group's and a selection's, but every value a control sends
  is 0 or above it.)

## CLs

### CL1 [ ] common: REAPER's grouping, checked

Depends on: nothing.

- Checked: what the user found in REAPER's UI (selection, and a grouped
  fader's memory), and what a temporary contract test found in the test install
  (see To confirm).
- The fake's grouped and ganged volume and pan move each follower from its
  start, as To confirm says: the starts, which are taken again when another
  track leads or a track was changed some other way, the clamp to each range
  and back, and silence as -200 dB. A volume and a pan keep separate starts.
  The fake no longer fails a grouped move from silence, or a grouped pan that
  clamps another track's.
- Commits the findings in To confirm.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- Contract tests (`check_in_reaper`), which pass in the test install as under
  the fake: a grouped volume to silence and back, past the top and back, from
  silence, and with another lead, a follower changed alone, and the lead
  changed alone; a grouped pan past an end and back, with another lead; a pan
  move leaving the volume's starts; and the same for selected tracks.

### CL2 [ ] device: Modifiers an input ignores

Depends on: nothing.

- `InputConfig::ignored_modifiers`, left out of the registration's off mask in
  `Control::RecomputeModifierMasks()`.
- Unused, so no visible change.

**Verify**
- Standard checks.
- `Control` tests: an input that ignores a modifier fires with it on and off;
  one beside it that requires it keeps its own set; press groups (a double
  press beside a plain press) with an ignored modifier.

### CL3 [ ] scene: Actions on any property

Depends on: CL2.

- `ValueAction`, `ReadRange`, `ReadAction` (without scopes yet), and read
  overrides in `ReadConfig`, replacing `press_toggles`, with `property_min`
  and `property_max` moving into the range; `ReadConfig::ignored_modifiers`,
  passed to the input.
- The kinds in `ViewMapping`, by type, with the control's value and a press's
  own value as above, and the overrides picked when an input arrives.
- The catalog of action kinds, and `docs/user/scene/actions.md`.
- `config` builds the same mappings from today's syntax, mechanically, so
  nothing visible changes.

**Verify**
- Standard checks.
- `ViewMapping` tests of each kind on each type and input, a press's own value
  on each type, overrides (first met per part, and the mapping's own
  otherwise, a range included), a range with each action kind, and ignored
  modifiers. The existing toggle tests move to the kinds, and the range tests
  stay.
- Call counts don't move.
- Build `update_user_docs`.

### CL4 [ ] scene: Scopes and track actions

Depends on: CL1, CL3.

- `ActionScope`, `copy`, `each`, and `others`; `TrackActions` runs a
  `ReadAction` on every writable track property, with every scope, and a held
  mapping's scope; `scale` on several tracks.
- The catalog lists the scope kinds and which properties take them, with a
  test that no scope and action kind share a name.
- The `ui_` properties' modifier rules become a table of `ReadAction`s.

**Verify**
- Standard checks.
- `TrackActions` tests of each scope and kind on each property, against the
  fake: grouping (selection's, the tracks sharing any group), the selected
  tracks with and without `each`, `others` (select's toggle rule included),
  `from` with and without subfolders, `scale` on several tracks, matching the
  fake's grouped moves (a volume's in dB, through silence and back, against
  the end of a range and back, from silence, another track leading, a track
  changed some other way, one joining the scope, and two mappings sharing a
  property's starts), `grouped,
  set` on a volume and a pan (lead and follow, and a VCA follower left out), a
  held scope, and the last touched track's rule.
- The `ui_` tests and the surface tests pass unchanged, and call counts don't
  move. New call count bounds for a fader move with `selected` and with `all`
  on the large project.
- Build `update_user_docs`.

### CL5 [ ] scene: Anchors as references

Depends on: CL4.

- `track_anchor` keeps one anchor for each declaration, shared by its view's
  instances, and is a track reference for `from` and `copy`. Its `action`
  becomes optional, and while the `ui_` properties are there, an anchor given
  one also holds `TrackActions`' anchor for it, until CL8.

**Verify**
- Standard checks.
- Tests of holding, releasing on the toggle and on the subject changing, a
  second hold failing, a latch, the modifier, two declarations being two
  anchors, the reference's fields, and a scope from it.
- Build `update_user_docs`.

### CL6 [ ] config: The syntax

Depends on: CL5.

- Scope and action kinds, `others`, and any statement in a `when`, in a
  mapping's options; `ignore(...)` in its brackets; optional commas after
  statements in every body; a parameter's name alone for `= true`. `set` and
  `toggle` are removed as options, and `range a .. b` becomes `range(min = a,
  max = b)`.
- Errors: an unknown kind, a parameter the kind doesn't take, a part the
  property doesn't take, a value of the wrong type, two of one part, an action
  on a write mapping or a held one, a scope or range on a write mapping, a
  range with an action that doesn't read the control, a mode on a read
  mapping, and a `when` in a `when`.
- `config_spec.md`: Options and Modifier sets rewritten, the statements table,
  and commas and flags in The file. The example config's options converted
  mechanically, still on the `ui_` properties.
- The backlog: "Modifiers a mapping ignores" comes out; "Reversed read ranges"
  stays, with its syntax updated; and a new item requires the comma between statements
  on one line, depending on Game Bits' item for matching a line break, which
  is added to Game Bits' backlog with **Requested by**.

**Verify**
- Standard checks.
- Config tests of each statement, each error with its line, commas, and flags.

### CL7 [ ] plugin: Actions in the X-Touch example

Depends on: CL6.

- The example on `track:` properties with scopes and actions, and anchors
  without `action`, as in the design, with Rec's new workflow. `xtouch.md`'s
  Rec section.
- Surface tests: Rec's new double presses (plain, with others recording and
  without, Ctrl, and Alt), Alt + press, and an anchor with Ctrl held. The rest
  pass unchanged.

**Verify**
- Standard checks.
- Call counts: Rec's gestures (the double press now sends other tracks back to
  monitor, which `AltCtrlDoublePressingRecordArm` measures today, and Alt + press
  and Alt + double press swap); the others don't move.
- The benchmark, against the bar: the mapping count is per-run work.

### CL8 [ ] scene: Remove the ui_ properties

Depends on: CL7.

- The `ui_` properties, their modifier table, `track_anchor`'s `action`, and
  `TrackActions`' anchors are removed. After the plugin CL, as the example uses
  them until then.

**Verify**
- Standard checks.
- Build `update_user_docs`.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation
  (behavior, structure, and reusable building blocks), and move anything left
  undone into the backlog, in the same change (see the workflow).
- The record stages' summary (`rec_stage.md`) waits for this feature, as its
  Rec workflow changes here. Ask about both together.

## Checks in REAPER

Copy the example's mappings into `pursey.jprsurf` (its strips, templates, and
modifiers), then:
- Rec, as in the table above: a double press records with the group, and every
  other recording track goes back to monitor, still armed; another double
  press turns the group off once nothing else records; Ctrl + double press
  records or turns off this track alone, leaving the others; Alt + press sends
  every recording track back to monitor, and Alt + double press turns every
  track off; Opt does nothing.
- Mute, solo, select, the fader, and the pot with each modifier: as before.
- The pot button centers every track in the group, and with Ctrl, this track
  alone.
- Holding Rec, mute, solo, or select, and pressing the same button on another
  track, sets the range; with Ctrl also held, nothing happens.
- Send held with select, with and without the select anchor, picks the track.
- A test mapping of a fader with `selected, scale` and its touch with
  `selected` (as in the brackets example): the selected tracks keep their
  offsets, and in Touch mode, each writes its envelope.
- The extension loads with no new errors in `jprsurf.log`, "Slow run" warnings
  included, and the idle profile doesn't regress against `profiles/` beyond
  what the benchmark showed.
