# Surface Config Model

JPRSurf is heading toward a surface that is entirely described by a config
file, with the plugin as a thin host that loads it. This doc defines what such
a config says: its concepts, and the named components it composes. It
deliberately says nothing about syntax, which belongs to the config language
(*Config file language and loader* in the [backlog](backlog.md)).

It ties together the backlog items that build toward a data driven surface, and
it seeds the public spec of the config language. Until those items land,
JPRSurf's surface is still built in C++ by `PluginSurface`, and this doc
describes what that code turns into.

Property names in this doc are the ones the code uses, except for properties
that components will provide, whose names are illustrative. The **Today** notes
use the current C++ names. [Names](#names) covers what changes before any of
them are frozen.

## Principles

- **The config says what; C++ says how.** A config composes named,
  parameterized components. The only logic it expresses is a condition that a
  property equals a value. Anything more is a new component, written in C++
  below `plugin`.
- **REAPER commands are the escape hatch.** Any REAPER action, including
  scripts and other extensions' actions, can be mapped by its command ID, so a
  user can get behavior JPRSurf doesn't have without writing code.
- **Mappings never name a device.** Widgets assemble the devices' controls into
  one surface, and mappings only use widgets. Adding a second extender is a
  change to the widgets.
- **Names are forever.** Every name a config can use is part of the public
  spec: properties, controls, output modes, components, and their parameters.
  No name is generated from an index.
- **A config is checked without REAPER or hardware.** Validation needs only the
  config and the catalogs of device types and properties, so it can be unit
  tested.
- **The scene owns all surface state.** Nothing in the plugin outlives the
  scene built from the config, so reloading a config is building a new scene.
- **A config costs nothing per run.** Name lookups, validation, and expanding
  templates happen once, when the scene is built. At run time, a scene built
  from a config is the same objects as one built by hand.

## The model at a glance

| Part       | What it is                                                                   | Today                                              |
| ---------- | ---------------------------------------------------------------------------- | -------------------------------------------------- |
| Devices    | Hardware units: a device type, MIDI ports, and control overrides             | `MidiPorts`, `Scene::AddDevice()`                  |
| Widgets    | Named controls, structs, and arrays, assembled from the devices' controls    | `"XTouchExt/"` prefixes and the strip loops        |
| Properties | Named, typed values in namespaces: built in, or declared by the config       | `Scene::GetProperty()`, `Scene::AddUserProperty()` |
| Views      | A tree of mapping sets, each with an enable condition, a subject, and a list | `View`                                             |
| Mappings   | A property and a control widget, with read and write options and a condition | `View::AddMapping()`                               |
| Templates  | Named, parameterized sets of mappings                                        | `AddTrackStripMappings()`                          |
| Components | C++ behavior a config declares: enumerated values, anchors, references, ...  | Classes such as `TrackPickProperty`                |
| Settings   | Surface-wide values                                                          | `Scene`'s track filter                             |

## Devices

A device is one hardware unit. For each device, the config gives:
- A **name**, which widgets use to refer to its controls.
- A **device type**, from the registry of types JPRSurf provides in C++. Today
  these are the X-Touch and the X-Touch Extender, both `DeviceXTouch`.
- Its **MIDI input and output ports**, by the names REAPER lists them under.
  Today these are hard coded as "X-Touch" and "X-Touch-Ext".
- Whether it is **required** or **optional** (see below). A device is required
  unless the config says otherwise.
- **Control overrides**: inputs or outputs of particular controls that this
  unit doesn't use, for differences between units of the same type. The only
  one today is hard coded: the author's extender has lost touch sensing on its
  third fader, so that fader has no press input. Overrides work the same for
  every device type. The type builds its controls as usual, and `Device` drops
  the overridden inputs and outputs before creating each `Control`. A motorized
  fader without a press input already falls back to delayed feedback, so
  nothing else changes.

A device type could also define options of its own, listed in its catalog, but
only once one is actually needed. None are today.

### Device types

A **device type** is a registry entry, by name, that JPRSurf provides in C++. It
holds:
- A **catalog** of the type's controls, described as a struct widget (see
  [Widgets](#widgets)). Its fields are the device's single controls, and arrays
  of structs for the controls that repeat, so the X-Touch's catalog has a
  `strip` array of 8 structs, each with rec, solo, mute, select, fader, pot, pot
  button, meter, two scribble lines, and a scribble color, rather than 88
  numbered names. For each control, the catalog gives its inputs (value, delta,
  press), its outputs (continuous value, discrete value, text, color), and the
  names of its output modes (indexed by today's mode numbers).
- A **factory** that creates a `Device` from its ports, its control overrides,
  and the run registry.

The catalog is plain data that needs no hardware or ports, and doesn't depend
on the MIDI or REAPER code, so checking a config against it can be unit tested.
Checking also rejects a control override that names a control the type doesn't
have, or an input or output that control doesn't have.

The catalog and the device's constructor must agree. As a device can't be
created in a unit test (it needs MIDI ports), the constructor takes its control
names from the catalog rather than its own constants, and `Device` checks once,
when it is created, that the controls it built match the catalog exactly,
logging an error if they don't.

A device whose ports aren't found at startup is **absent**. Devices are found
once, at startup, as they are today.
- If a **required** device is absent, the config doesn't load, with an error
  naming the device, and the surface does nothing. A config written for a
  particular set of hardware never runs half working on a different set.
- If an **optional** device is absent, the config loads without it: its
  controls are absent, and so are the widgets defined over them (see
  [Absent devices](#absent-devices)). This is how the surface already works
  with only one of the two units connected.

## Widgets

A **widget** is a named piece of the surface that mappings use, without saying
which device it is on. Widgets are the only way mappings refer to hardware, so
they are the only part of a config that knows which device a control is on. A
widget is one of:
- A **control**: one device control.
- A **struct**: named fields, each of which is a widget.
- An **array**: an ordered list of widgets that all have the same shape.

The config's widgets are one top-level struct, and a widget is named by its path
from it: `strip[3].fader` is the `fader` field of the fourth element of the
`strip` array. Indices start from 0. An index in a path is an address rather
than part of a name, so paths don't break the rule that no name is generated
from an index.

Device catalogs use the same three types: a device is a struct widget, so a
path can also start at a device, such as `xtouch.play` or
`xtouch.strip[7].fader`. Paths through devices are only for defining the
config's widgets. Mappings only use paths in the top-level struct, which is how
they never name a device.

A widget only names something. What a mapping does with it (its direction,
press behavior, and output mode) belongs to the mapping, within what the
control's catalog entry allows, and several mappings can share one widget.

### Defining widgets

- **Control**: a path to a device control, such as `xtouch.play`.
- **Struct**: a path to a struct (`info_strip` is `xtouch.strip[7]`), or new
  fields, each defined as any widget. A struct can also **include** the fields
  of another struct, leaving out any it names. JPRSurf's top-level struct
  includes the X-Touch's, apart from its strips, so its buttons keep their
  catalog names without naming each one. A field defined twice, directly or by
  an include, is an error.
- **Array**, in one of two ways:
  - **Joined**: slices of other arrays, one after another. The source arrays can
    be device arrays or the config's own. JPRSurf's `strip` is the extender's
    strips 0-7 then the X-Touch's strips 0-7, their physical order from left to
    right. Each element is the source array's element.
  - **Zipped**: a struct whose fields are array slices of the same length,
    turned into an array of structs. Element *i* gets element *i* of each
    slice, so each field's index comes from its position in its slice. This
    builds strips from controls that no device groups together, such as faders
    on one device and displays on another.

Every widget is defined this way, by paths and slices, so there are no index
expressions.

### Shape

Every element of an array must have the same shape:
- **Structs**: the same field names, with each field the same shape, all the
  way down.
- **Controls**: the same inputs, outputs, and output mode names, as the
  catalogs give them.

This is what makes a mapping written once for the element's `fader` valid for
every element. Control overrides don't count, so the fader with no touch sensing
still matches the others, and its press input is just missing at run time, as it
is today. Shapes must match exactly, until a real device needs something
looser.

### Absent devices

- A control widget on an absent device is absent, and mappings to it do
  nothing.
- A joined array drops the elements that come from an absent device's arrays,
  so its size depends on what is connected.
- A zipped array and a struct keep all their elements and fields, and the ones
  on an absent device are absent.

### JPRSurf's widgets

- `strip` joins the extender's strips and the X-Touch's. The extender is
  optional, so without it `strip` has 8 elements, as the strip loops do today.
- A device control can be in more than one widget. The X-Touch's last strip is
  both `strip[15]` and `info_strip`, and the mode decides which one is in use.
- Adding a second extender is adding the device, and its strips to the joined
  arrays. Nothing else changes, as views repeat over whatever size the arrays
  are.

The config's own widget names aren't part of the spec. The names of device
types, their catalog fields, and their output modes are.

## Properties

A property is a named, typed value that mappings connect to controls. Its type
is one of the `ViewProperty` types (action, toggle, pan, volume, normalized,
text, color, timeline position, or enumerated), and a mapping picks which of
the control's inputs and outputs to use from it.

Every property name is a namespace and a name, joined by `:`. The namespace
says what kind of property it is and what its scope is:

| Namespace | What                                                                            | Scope          | Examples                                                            |
| --------- | ------------------------------------------------------------------------------- | -------------- | ------------------------------------------------------------------- |
| `cmd:`    | REAPER commands: a toggle if REAPER reports a toggle state, otherwise an action | Global         | `cmd:40029` (Undo), `cmd:_SWS_ABOUT`                                |
| `state:`  | REAPER state: the polled state rows, the timeline, the rulers, and references   | Global         | `state:can_redo`, `state:timeline_position`, `state:selected_track` |
| `mod:`    | Modifiers, built in and declared                                                | Global         | `mod:shift`, `mod:send_hold`                                        |
| `const:`  | Fixed values that mappings write, with an explicit type, named by a number      | Global         | `const:1`                                                           |
| `track:`  | The view's subject, when it is a track                                          | View           | `track:name`, `track:has_routes`                                    |
| `route:`  | The view's subject, when it is a send or receive                                | View           | `route:volume`, `route:other_track`                                 |
| `view:`   | The view itself: its list, and navigation                                       | View           | `view:bank_inc`, `view:child_route_toggle`                          |
| `user:`   | Declared by the config, other than modifiers                                    | Where declared | `user:anchor_select`, `user:surface_mode`                           |

A namespace groups properties by what they are, not by how they are
implemented. `state:` holds both the polled state rows and the timeline and
ruler properties, which are different C++ classes, because to a config they are
all REAPER's global state.

**Lookup.** The namespace says where a name is looked up, so there is no
guessing between a view and the scene:
- `cmd:`, `state:`, `mod:`, and `const:` are global.
- `track:`, `route:`, and `view:` resolve against the view the mapping,
  condition, or mode override is in.
- `user:` resolves to its declaration: in that view, one of its ancestors, or
  the top level.
- A name with a `.` is a property of a named thing: the part before the `.` is
  looked up by the rules above, and asked for the part after it. The thing is a
  reference (whose properties are its fields, see [References](#references)),
  or a component that provides more than one property (none does yet). It is
  only ever one level deep.
- Views give names a context, but never a scope: no name refers to another
  view's properties, as that view may be inactive, and its properties not
  current.

**Constants.** A `const:` property is added with an explicit type and value
(`Scene::AddConstProperty()`): a toggle, normalized, text, or color value so
far. Its name is just a unique number, which a config never spells: code that
maps a constant gets the name from the property it added. Writes to a constant
do nothing.

As built in properties and declared ones are in different namespaces, a
config's names can never clash with a built in name, including one added in a
later version.

**Track changes.** A track's selected, mute, solo, rec arm, pan, and volume
each have two properties, which read the same value but change it differently.
The plain one (`track:mute`) sets the value on the track alone. The `ui_` one
(`track:ui_mute`) runs the scene's track action, like the control in REAPER's
UI, with the surface's modifiers, grouping, and anchors (see
[track_actions.md](worklog/track_actions.md)).

### References

A **reference** is a named value that is a subject (a track, a route, or later
any other kind, see [Subjects](#subjects)), or nothing. It isn't a property
itself, but its **fields** are: the properties of the subject it refers to,
named after a `.`, so `state:selected_track.has_routes` is the selected track's
`track:has_routes`. A reference's name has a namespace like a property's, which
says where it is looked up. Only a reference's name can be bound to or
followed, so a property can never be used where a reference is needed.
- A condition on whether a reference refers to something uses its `exists`
  field (`user:current_track.exists`), which is also false while it refers to a
  deleted track.
- Fields are one level deep: `route:other_track.name`, but never a field of a
  field.
- **Built in** references are global and read-only: `state:master_track`,
  `state:last_touched_track`, and `state:selected_track`, which is the selected
  track if exactly one track other than the master is selected and it is on the
  surface. They change only on the REAPER events that can change them, so
  reading them costs no more than any other property.
- **Subjects** have references too: a route's `route:other_track` is the track
  at the other end, so a route strip shows `route:other_track.name`.
- **Declared** references are writable, and views can be bound to them (see
  [Reference](#reference)).

### Declared properties

A config **declares** an instance of a component, with a name and parameters,
and gets the properties it provides. The behavior is always C++; only the name
and parameters come from the config. A declaration that provides one property
is named by its declared name (`user:anchor_select`), and one that provides
several would give each its own name under it (`user:<name>.<property>`).

Where a declaration is made fixes its scope, rather than where it is used:
- **At the top level**: one instance, global.
- **On a view**: one instance for each view, so each strip of a repeated view
  gets its own under the same name. The select anchor is `user:anchor_select`
  on every strip, rather than `anchor_select_3`. It is visible in the view and
  its descendants.

Each component says where it may be declared: an anchor on a view, as it needs
the view's track, and an enumerated value at the top level. Declaring a name
that is already visible (in the same view, an ancestor, or the top level) is an
error, so a declaration never silently hides another.

**Today:** `Scene::AddUserProperty()` declares at the top level, and
`View::AddUserProperty()` on a view. Both refuse a name that would hide, or be
hidden by, another ([declared_properties.md](worklog/declared_properties.md)).
A component that acts on its view is told the view when it is added
(`ViewProperty::SetView()`), so it can't act on one view and be added to
another.

**Modifiers** are the exception to `user:`. A declared modifier is in `mod:`
with the built in ones, as what matters wherever a modifier is used is that it
is one: global, backed by a modifier bit that is cheap to test, and allowed in
modifier sets. Modifiers are declared only at the top level, as modifier bits
are global. A declared modifier takes precedence over a built in one of the same
name, so a built in modifier added in a later version never breaks a config
that already declares that name.

## Views

A view is a set of mappings, in a tree. It has four parts:
- Whether it is **enabled**.
- Its **subject**: the REAPER thing its properties refer to.
- Optionally, a **list** of subjects, which its children show.
- Its **contents**: mappings, the templates it uses, the components it
  declares, and its child views.

A view is active while it is enabled and its parent is active, and only an
active view's mappings do anything. A view has a **name**, unique among its
siblings, which it only needs if something refers to it.

### Enabled

A view is either always enabled, or enabled while a condition is met: a
property equals a value (true, for a toggle), the same as a mapping's
condition. A mode is a view enabled by one value of an enumerated value (see
[Modes](#modes)).

The scene applies changes to which views are enabled between runs, never while
it is running them, so anything can change a view's condition at any time.

### Subjects

A view's **subject** is the REAPER thing its properties refer to. Each kind of
subject has its own namespace:

| Kind  | Namespace | Notes                                                                 |
| ----- | --------- | --------------------------------------------------------------------- |
| None  |           | The root view, and any view that doesn't need one                     |
| Track | `track:`  |                                                                       |
| Route | `route:`  | A send or receive. `route:other_track` is the track at the other end. |

Later kinds would each add a namespace: an FX instance (`fx:`, with an
`fx:track` reference), a marker, a region, a track group, or a media item. None
of these is needed for the X-Touch, but other devices may want them, and adding
one is adding a namespace and its lists, not changing how views work.

A view's subject comes from one of:
- **Its parent**: the same subject as its parent. This is the default, and the
  root has none.
- **A reference** it is bound to, such as `state:master_track`, or a declared
  reference like `user:folder`. Navigation that moves the view (up to the
  parent folder, into a folder, or across a route) writes the reference, so a
  view bound to a read-only reference can't navigate.
- **An item of its parent's list**, for a repeated view (see
  [Showing a list](#showing-a-list)).

Each of these fixes the kind of subject without REAPER, so checking can reject
a `route:` property on a view whose subject is a track.

### Lists

A view can have a **list**: an ordered list of subjects of one kind, with a
**scroll position**. A child view repeated over an array widget shows it (see
[Showing a list](#showing-a-list)).

| List         | Items                                                                            |
| ------------ | -------------------------------------------------------------------------------- |
| Child tracks | The child tracks of the view's track that are on the surface                     |
| Routes       | The sends or the receives of the view's track, depending on the list's direction |

A routes list's **direction** (sends or receives) is part of its state:
- A parameter picks the direction whenever the view's track changes: sends,
  receives, or sends unless the track only has receives (JPRSurf's choice).
- `view:child_route_toggle` switches it, and `view:child_route_type_name` names
  it ("Send" or "Recv").
- Crossing a route (`view:parent_route_other_track`) sets the opposite
  direction, so the route back to the original track is shown.

A config that only ever wants sends picks sends, and doesn't map the toggle.

Later lists could come from other kinds of subject (a track's FX), or from the
project as a whole (its markers, regions, or track groups), with no subject at
all.

#### Showing a list

A list is shown by one child view that is **repeated over an array widget**:
- There is one instance for each element of the array, in array order, and
  the instance for element *i* shows the item at the scroll position plus *i*.
- The array's element is the only thing that differs between instances:
  widget paths use it (the element's `fader`), and property names never do.
- The number of instances is the array's size, so a missing device or a second
  extender changes it without changing the config.

The hardware decides how many strips there are, and the list decides what is on
them. A repeated view's parent must have a list, and a view with a list has
exactly one repeated child. It can have other children too, which share its
own subject (Send/Receive mode's Info strip could be one).

#### Scrolling

- `view:child_inc` and `view:child_dec` scroll by one item, and `view:bank_inc`
  and `view:bank_dec` by the **bank size**: a number, or the number of
  instances (Send/Receive mode pages through every route strip at once).
- An instance past the end of the list has an empty subject of the list's
  kind, so its `track:exists` or `route:exists` is false, and its controls show
  nothing. An empty subject's values read as nothing where they can (no name,
  black, the fader down), and a mapping whose value would still show something
  does nothing while `exists` is false: a condition on it, so the control is
  cleared (a volume's text, which would read "-inf dB"), or a mode override (a
  ring's mode with no lights). A template such as a track strip bundles these.
- The scroll position stays in range when the list changes, such as when
  tracks are deleted or hidden.
- A list can **reveal** a reference: scroll so that its subject is shown,
  whenever the track it refers to changes while the view is active, and when
  the view becomes active. For a list of child tracks, revealing a track in
  another folder first moves the view's bound reference to that folder.

### Modes

A mode isn't a concept of its own. It is:
- An **enumerated value** whose values are the modes, such as
  `user:surface_mode` with `track` and `send_receive` (see
  [Enumerated value](#enumerated-value)). It is always exactly one of them.
- A **view for each mode**, enabled by a condition on its value.
- **Mappings** for the mode buttons: a press sets the value (a range whose ends
  are the same), and each mode's view lights the buttons, so the current mode
  blinks, and another is lit while it is available.

A mode that shows a particular subject, such as Send/Receive mode's track, is
bound to a declared reference. It is entered by two mappings on the same press,
with the same condition that the new subject can be shown: a **pick**, which
sets the reference (see [Pick](#pick)), and a mapping that sets the mode. If the
reference's track is deleted, the mode stays, and shows nothing until the
reference changes.

### Templates

A **template** is a named set of mappings (and uses of other templates) with
parameters, which any number of views can use. Its parameters are widgets (a
control, or a struct such as a strip) and property names. There are no
expressions. Using a template on a view adds its mappings to that view, as if
they were written there.

JPRSurf's `track_strip` template, with a strip as its parameter, is today's
`AddTrackStripMappings()`. Every Track mode strip uses it, and so does the
Send/Receive Info strip. The automation buttons are five uses of another,
whose parameters are a button, a command, and three state properties (today's
`kAutoModeButtons` table).

Templates and repetition are expanded when the scene is built, so they cost
nothing at run time.

## Mappings

A mapping connects one property to one control widget, in one view.

- **Direction**: read (the control changes the property), write (the property
  drives the control's output), or both.
- **Read options**:
  - **Input**: chosen from the property's type, or named.
  - **Press behavior**: press, long press (held for 350ms), double press, or
    tap (see [Tap](#tap)). A press waits for the release when the control also
    has a long or double press mapping.
  - **Held**: the property is on while the button is held, and off when it is
    released (`press_release`). With a long press, it is on from when the long
    press fires.
  - **Range**: the property values that the control's range maps to. A press
    toggles between them. On an enumerated property, a press steps through the
    range instead, wrapping around, or with **press toggles**, toggles between
    its ends (Marker and Nudge). A range whose ends are the same sets that
    value (the pot button centers pan, and Track sets `track`).
  - **Required modifiers** (see [Modifier sets](#modifier-sets)).
- **Write options**: the output mode, by name, and an ordered list of mode
  overrides, each a condition and a mode. The first override whose condition is
  met picks the mode. A strip's pan ring is off when it has no track, and
  different for a folder.
- **Condition**: the mapping only acts while a property equals a value (true,
  for a toggle), read as the value's type. A write mapping writes, and holds
  the control, only while it is met. A read mapping keeps its input, and checks
  the condition when the input arrives (see [Sharing a
  control](#sharing-a-control)).
- **Value**: a write mapping can write a fixed value, such as a light that is
  always on in a view (the Track mode button), or a fixed label on a display.
  A config writes the value directly, and the scene adds a `const:` property
  for it, so this is an ordinary mapping.

A condition names exactly one property, and compares it for equality.
Combining states is a component's job, or a polled state row's:
`state:auto_override_any_latch` is a row that is on for Latch or Latch Preview.
Mode overrides are value to mode maps, compared the same way. Conditions and
modifier sets are the only logic in a config.

### Where conditions go

- **A view's condition** switches a whole set of mappings, such as a mode.
  Leaving a view unregisters its mappings.
- **A mapping's condition** decides whether that mapping acts right now. For a
  write, it picks which property drives the control. For a read, it decides
  whether the input does anything, and the mapping keeps its input either way.
- **A component** takes a condition only for a rule about state it owns, which
  must hold however that state is changed. None does yet.

So a condition that says whether a button does something goes on its mapping,
not in the property the mapping triggers. When one press does two things, such
as picking a track and entering a mode, each is its own mapping with the same
condition.

### Sharing a control

Any number of mappings can use one control. These keep them from fighting:
- **Conditions**. The automation lights switch between showing the selected
  tracks' modes and the global override, and Rewind and Forward between their
  steps. A read's condition is checked when its input arrives, so switching it
  never loses a pending long or double press. While it isn't met, the input
  does nothing, but it still counts in the control's modifier sets and press
  behaviors, so it doesn't fall through to another mapping. A held mapping
  still applies the release of a press it applied, so what it holds can't stick
  on.
- **Modifier sets**, for reads that depend on buttons held on the surface.
- **Views**, for whole modes.
- **Press behaviors**: a press, a long press, a double press, and a tap on one
  control are different gestures.

A control that no active mapping writes clears itself (light off, ring off,
scribble blank), so a mode doesn't have to map the controls it doesn't use.

### Modifier sets

A read mapping can require a set of modifiers. Among all the modifiers that the
read mappings on a control require, a mapping fires only when exactly its own
set is on. Modifiers that no mapping on the control mentions are ignored. Undo
runs Undo, and with Shift held, Redo. Select's mappings don't mention Shift, so
it doesn't choose between them (the track action reads Shift itself, which
[track_actions.md](worklog/track_actions.md) moved to `scene`).

A mapping meant to fire with a modifier held, whatever else is held, needs a
copy for each combination of the other modifiers on its control. Letting a
mapping list modifiers it ignores would fix that, and as it is purely additive,
it is in the backlog (*Modifiers a mapping ignores*) rather than decided here.

## Components

A component is a piece of C++ below `plugin` that a config declares by kind,
with a name and parameters (see [Declared properties](#declared-properties)). It
owns its state, provides properties (including references, which views can be
bound to), and enforces its own rules, so a config can't use it wrong.

These are the components needed to express what `PluginSurface` does today.
All of them exist in `scene` or `device`, and `PluginSurface` composes them
from C++ until *Build the scene from a SurfaceSpec* (see [Getting
there](#getting-there)).

### Modifier

- **Declared:** at the top level.
- **Parameters:** none.
- **Provides:** `mod:<name>`, a toggle backed by a modifier bit, which modifier
  sets can use.
- **Today:** `Scene::AddModifierProperty()`, for `mod:send_hold` and
  `mod:select_anchor`.

### Enumerated value

- **Declared:** at the top level.
- **Parameters:** the names of its values, in order, and optionally the value
  it starts as (otherwise the first).
- **Provides:** `user:<name>`, an enumerated property that is always exactly
  one of its values, by construction. Mappings set it (a range whose ends are
  the same sets one value, and press toggles switches between two), and
  conditions on its values enable views and mappings. Its text is the value's
  name.
- **Today:** `EnumeratedValueProperty`. JPRSurf declares `user:transport_step`
  (`measure`, `beat`, and `marker`), which Marker and Nudge set, and
  `user:surface_mode` (`track` and `send_receive`)
  ([enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)).
  Values are numbered in the order of their names, which C++ has to match
  until a config names values.

### Track anchor

- **Declared:** on a view, as it acts on the view's track.
- **Parameters:** a track action (select, mute, solo, or rec arm), and
  optionally a modifier that is on while it is held.
- **Provides:** a toggle, for a held mapping. While it is on, it holds the
  action's anchor on the view's track, unless the strip is empty. The anchor is
  released with the button, or when the view's subject changes.
- **Today:** `TrackAnchorProperty`. JPRSurf's strip views add
  `user:anchor_select` (with `mod:select_anchor`), `user:anchor_mute`,
  `user:anchor_solo`, and `user:anchor_rec_arm`.

### Reference

- **Declared:** at the top level.
- **Parameters:**
  - The kind of subject it refers to (a track).
  - Optionally, a fallback: a reference whose subject it starts as, and returns
    to if its own subject is deleted. Otherwise, it starts as, and returns to,
    nothing.
  - Optionally, a reference to follow: whenever that reference changes to a
    track that is on the surface and isn't the master, this one changes to it
    too.
- **Provides:** a writable reference (see [References](#references)), which
  views can be bound to, and which navigation and picks write.
- **Today:** `Scene::AddTrackReference()`, and the view configs
  (`View::Config`) that bind to references and list their tracks or routes
  ([view_subjects.md](worklog/view_subjects.md)). JPRSurf declares
  `user:folder` (falling back to `state:master_track`) for the track list, and
  `user:current_track` (following `state:last_touched_track`) for Send/Receive
  mode, which the track list also reveals.

### Pick

- **Declared:** on a view, where the source is the view's subject, or at the
  top level, where the source is a reference.
- **Parameters:** the reference to set, and the source reference, if it is at
  the top level.
- **Provides:** an action that sets the reference to the source's track, if it
  has one. Whether a pick should happen is its mapping's condition, and
  anything else the same press does is another mapping on it (see [Where
  conditions go](#where-conditions-go)).
- **Today:** `TrackPickProperty`. JPRSurf picks `user:current_track`, with a
  mapping beside each pick that sets `user:surface_mode` to `send_receive`,
  both requiring `has_routes`: at the top level from `state:selected_track`
  (`user:pick_selected_track`, tapping Send), and on each strip view from its
  track (`user:pick_track`, select while Send is held).

### Tap

- **Parameters:** none. It is a press behavior.
- **Behavior:** a tap fires on release, if the button was released before a
  long press. It never delays the control's other presses, so a button can be
  held for one thing and tapped for another. Send taps and holds: holding it is
  `mod:send_hold`, and a tap is mapped in each mode's view. In Track mode it
  enters Send/Receive mode for the selected track, and in Send/Receive mode it
  toggles between sends and receives. A mode change while Send is held
  deactivates the view holding the tap mapping, which drops the pending tap.
- **Today:** `InputConfig::PressBehavior::kTap`.

### Polled state

- **Parameters:** none. Each row of the polled state table is a named `state:`
  property, with a read function and optionally a write function.
- **Today:** the polled toggle rows of the `kStateProperties` table
  (state_properties.cc).

### Navigation

The `view:` properties, which already exist:
- `child_inc`, `child_dec`, `bank_inc`, and `bank_dec` scroll the view's list.
- `track_parent` and `track_root` move the view's bound reference up a folder,
  or to the master track. `parent_track_child`, `parent_track_parent`, and
  `parent_track_root` do the same to the parent view's reference, from one of
  its children.
- `parent_route_other_track` moves the parent view's reference across a route
  to the track at the other end, and sets its routes list to the opposite
  direction.
- `child_route_toggle` and `child_route_type_name` switch and name a routes
  list's direction.

### Beneath the config

These are needed, but a config never names them:
- **Deferred view changes**: the scene applies changes to views' conditions
  between runs, as views can't be enabled or disabled while the scene is
  running them.
- **Keeping subjects and lists current**: references and lists react to track
  list and visibility changes themselves, rather than the plugin re-pointing
  views (`TrackReference::Update()` and `ViewList::Update()`).
- **Host plumbing**: forwarding REAPER's notifications to `TrackCache`, polling
  track visibility, `ContinuousUndo`, and the `Run()` performance log
  ([extension_host.md](worklog/extension_host.md)).

## The escape hatch: REAPER commands

- A command is mapped by its ID, as a number (`cmd:40029`), or as a named
  command ID starting with `_` (`cmd:_SWS_ABOUT`, or a script's `cmd:_RS...`
  ID), resolved when its property is created.
- A command that reports a toggle state is a toggle, so it can light a button.
- A command acts on REAPER's own state (its selection, its last touched track),
  not on a view's subject. Anything that needs the view's track or route is
  a component.
- A command that isn't installed is a config error, so building the scene
  detects it, and the config doesn't load, with an error naming the command. As
  with a required device, a config never runs half working. Today the mapping
  fails with its property not found. `NamedCommandLookup()` returns 0 for a
  name nothing has registered, and `kbd_getTextFromCmd()` returns empty text
  for a numeric ID REAPER doesn't know.
- Other extensions and scripts register their commands when REAPER starts,
  before it creates control surfaces, so they are all known by the time the
  scene is built.
- If a config later needs to work whether or not a command is installed, a
  mapping could be marked **optional**, so a missing command only makes that
  mapping do nothing. That is purely additive, so it can wait until it is
  needed.

## Settings

- **Track filter**: which tracks the surface shows (the mixer's, the track
  panel's, or all of them). Today it is the mixer's, the default for the
  `Scene` constructor's track filter.

Press timings (350ms for a long press, and the double press window) stay fixed
in C++ until there's a reason to change them.

## Checking and building a config

Checking is separate from building, so that it needs no REAPER or hardware and
can be unit tested. It checks that:
- Every device type, control, member, and output mode exists in the catalogs,
  and every property is built in, declared where it is used, or a well formed
  command ID.
- Each mapping's property type can use its control's inputs or outputs in the
  mapping's direction.
- Conditions (on mappings, mode overrides, and views) name a property, and a
  value of a type it can be read as.
- Every view's subject properties match the kind of its subject, its list
  suits its subject, and every reference it is bound to refers to the right
  kind.
- Every template use gives all its parameters.
- Every view with a list has exactly one repeated child, which names an array
  widget, and no view without a list has one.
- Each component is declared where its kind allows, no declaration hides
  another that is visible where it is made, and declared modifiers fit in the
  60 available.

Building then opens the ports, stops if a required device is absent, creates
the devices that are present, expands templates and repeated views against
them, creates the properties and components (stopping if a command isn't
installed), and activates the scene. How
errors are reported, and whether a config with errors loads partly or not at
all, is for the config language.

## Lifetime

The plugin holds the config, the scene built from it, and the host plumbing.
All surface state (modes, anchors, held modifiers, references, and lists) lives
in the scene, so rebuilding the scene resets the surface completely, and
reloading a config needs no special cases. State in `common` singletons
(`TrackCache`, `ContinuousUndo`, the modifier state) outlives scenes, and
components must not rely on it being fresh. Anchors are released by their
views, so a destroyed scene holds none.

## Names

The public spec freezes every name a config can use. Today's names grew one
feature at a time, so they need changes before they are frozen. *Property
namespaces and names* made the ones that didn't need a new component: every
property is in a namespace, the polled state rows have names rather than
indices, rec arm is `rec_arm` everywhere, and the secondary ruler is
`state:secondary_ruler_*` rather than `ruler2_*`. The audit below is what is
left. Properties the plugin declares are in `user:`, as a config's will be, and
device control names and output modes wait for *Device types and catalogs*.

### Conventions

- Every word is lower case, with underscores between words: properties,
  controls, struct fields, output modes, components, and parameters.
- A property name is a namespace and a name, joined by `:`, such as
  `track:mute` or `state:can_redo` (see [Properties](#properties)). Widget paths
  never have a namespace, so a property name never looks like a widget path,
  even though both can contain `.`.
- Indices start from 0, and are never part of a name.
- A name says what a thing is, not how it is implemented.

### Audit

| Today                                                                      | Problem                                                                                             | Change                                                                                                         |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| `mod:send_hold`, `mod:select_anchor`                                       | Declared by the plugin                                                                              | Declared by the config: `mod:send_hold`, and so on                                                             |
| `view:track_parent`, `view:track_root`, and the other view properties      | Named before the model had references and lists                                                     | Settled when navigation is reviewed as a component                                                             |
| `Fader1`, `Scribble1Line2`, `AssignTrack`                                  | Mixed case, with 1-based numbers inside names                                                       | Catalog arrays with lower case fields (a strip's `fader`), and lower case single controls (`assign_track`)     |
| `XTouch/`, `XTouchExt/`                                                    | Mappings name devices                                                                               | Widgets                                                                                                        |
| Output modes 0, 1, 5, and 8                                                | Numbers from the MCU protocol                                                                       | Named by the device type for each output: solid and blink for lights, and the ring styles and off for encoders |
| `Track1` to `Track16`, `Route1` to `Route15`, `TrackMode`                  | Generated, and mixed case                                                                           | Repeated views need no names. The others follow the conventions.                                               |

## JPRSurf's surface in the model

```
devices
  xtouch               X-Touch, ports "X-Touch", required
  xtouch_ext           X-Touch Extender, ports "X-Touch-Ext", optional, third
                       fader without touch
widgets
  (include)            xtouch's fields, apart from its strips
  strip                joined: xtouch_ext's strips 0-7, then xtouch's 0-7
  route_strip          joined: xtouch_ext's strips 0-7, then xtouch's 0-6
  info_strip           xtouch's strip 7
declared at the top level
  modifiers            mod:send_hold, mod:select_anchor
  enumerated values    user:transport_step: measure, beat, and marker;
                       user:surface_mode: track and send_receive
  references           user:folder, a track falling back to the master;
                       user:current_track, a track following the last
                       touched track
  pick                 user:pick_selected_track: user:current_track from
                       state:selected_track
templates
  track_strip          mute, solo, rec arm, pan and its ring, pot button,
                       volume, name, color, meter
  auto_mode_button     the command and lights for one automation button
views
  root                 no subject. Modifiers, transport, timecode, utility
  │                    buttons, automation buttons, Marker and Nudge
  │                    (press toggles on user:transport_step, lit on their
  │                    value), Rewind and Forward (conditioned on
  │                    user:transport_step), holding Send (mod:send_hold),
  │                    and Track, which sets user:surface_mode to track
  ├── master_fader     bound to state:master_track
  ├── track_mode       enabled by user:surface_mode being track. Tapping
  │   │                Send runs user:pick_selected_track and sets
  │   │                user:surface_mode to send_receive, both requiring
  │   │                state:selected_track.has_routes. Track blinks, and
  │   │                Send is lit from state:selected_track.has_routes
  │   └── track_list   bound to user:folder. Lists its children, revealing
  │       │            user:current_track. Global, Bank, and Channel
  │       └── (strip)  repeated over strip. Declares anchors for select (with
  │                    mod:select_anchor), mute, solo, and rec arm, and
  │                    user:pick_track of its track. Maps track_strip, select
  │                    (press, double press, long press, and anchor), and
  │                    while Send is held, the pick and setting
  │                    user:surface_mode to send_receive, both requiring
  │                    track:has_routes. The select light switches on
  │                    mod:send_hold. The other anchors, and volume on
  │                    scribble line 2
  └── send_receive_mode  enabled by user:surface_mode being send_receive, and
      │                bound to user:current_track. Lists its routes (sends,
      │                unless it only has receives), banking by all its
      │                strips. Bank and Channel. Tapping Send toggles the
      │                direction. Send blinks, and Track is lit. track_strip
      │                on info_strip, and the direction on its line 2
      └── (route)      repeated over route_strip: route volume, pan, and
                       mute, route:other_track's name and color, route
                       volume on scribble line 2, select across the route
```

Without the extender, the arrays shrink exactly as the strip loops do today, to
8 track strips and 7 route strips. Without the X-Touch, the config doesn't
load, where today the extender alone gives 8 strips with no transport, modes,
or navigation. That's a deliberate change: the extender alone was never a
useful surface.

What becomes of the plugin's code:

| `PluginSurface` today                                                                     | In the model                                                     |
| ----------------------------------------------------------------------------------------- | ---------------------------------------------------------------- |
| Opening the MIDI ports and adding the devices                                             | Devices                                                          |
| `has_xtouch` checks, device prefixes, and the strip loops                                 | Widgets and repeated views                                       |
| `AddTrackStripMappings()`                                                                 | The `track_strip` template                                       |
| The `kAutoModeButtons` table                                                              | The `auto_mode_button` template                                  |
| `AddTrackAnchorMapping()`                                                                 | Track anchor                                                     |
| `kTransportSteps`, `user:transport_step`, and `user:surface_mode`                         | Enumerated values, and conditions on their values                |
| `user:pick_selected_track` and `user:pick_track`, and the mappings beside them            | Pick                                                             |
| `user:folder`, `user:current_track`, and the views' configs                               | References, subjects, and lists                                  |
| Track list and visibility refresh, `TrackCache` events, `ContinuousUndo`, the `Run()` log | Host plumbing                                                    |

## Getting there

The backlog items build this in three tracks, which can interleave, and then
come together:
- **Properties and views**, each with no change in behavior, used from C++:
  1. *Property namespaces and names* (done): the final names, before anything
     else adds more.
  2. View conditions and constant properties (done).
  3. *View subjects, lists, and references* (done,
     [view_subjects.md](worklog/view_subjects.md)): the largest change to
     `View`.
  4. *Properties declared on views, and track anchors* (done,
     [declared_properties.md](worklog/declared_properties.md)).
  5. *Modes from enumerated values and picks* (done,
     [enumerated_values_and_picks.md](worklog/enumerated_values_and_picks.md)):
     the plugin has no surface state or callbacks left.
- **Devices and widgets**, unit tested: *Widgets*, then *Device types and
  catalogs*, ending with the plugin creating devices by type and mapping
  through widgets.
- **Independent pieces**: *Named command IDs* (done), *Track actions in scene*
  ([track_actions.md](worklog/track_actions.md), done), and *Extension host in
  common* ([extension_host.md](worklog/extension_host.md), done).

Then, in order:
1. *Build the scene from a SurfaceSpec*: this model as a C++ struct, with
   checking (against device and property catalogs) separate from building.
2. *Config file language and loader*: the syntax, and reading it into the same
   struct.
3. *Public config spec and user guide split*: this doc becomes the spec.

Meanwhile, a new feature is a component below `plugin`, plus its use in
JPRSurf's surface, and the plugin gains no new state or callbacks (see Feature
workflow in CLAUDE.md).
