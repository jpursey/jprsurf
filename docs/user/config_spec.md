# Config File Specification

A JPRSurf config file describes a whole control surface: the devices it uses,
the widgets its mappings use, the components it declares, and the mappings,
in views, that connect controls to REAPER. This page is the specification of
the language: every statement, what it means, and the rules a config must
follow.

What a config can name from JPRSurf isn't listed here, but on pages generated
from JPRSurf's code, so they always match the version that reads the config:
- **[Device types](devices/README.md):** each type's controls, with their
  inputs, outputs, and output modes.
- **[The scene](scene/README.md):** the property namespaces, each with a page
  of its properties and references, and the kinds of component a config can
  declare, with their parameters.

The [user guide](user_guide.md) covers JPRSurf's own config,
`jprsurf.jprsurf`, which is the best place to see the whole language at work,
and how to make the surface read another file.

---

## Contents

- [Principles](#principles)
- [A first look](#a-first-look)
- [The file](#the-file)
- [Devices](#devices)
- [Widgets](#widgets)
- [Properties](#properties)
- [Components](#components)
- [Views](#views)
- [Mappings](#mappings)
- [Conditions](#conditions)
- [Named mappings](#named-mappings)
- [REAPER commands](#reaper-commands)
- [Settings](#settings)
- [Checking and errors](#checking-and-errors)
- [Names](#names)

---

## Principles

- **The config says what; JPRSurf says how.** A config composes named pieces
  that JPRSurf provides: device types, properties, and components. The only
  logic it expresses is a condition, that a property equals a value, and the
  modifiers a mapping requires. Anything more is a new component, which is a
  change to JPRSurf.
- **REAPER commands are the escape hatch.** Any REAPER action, including
  scripts and other extensions' actions, can be mapped by its command ID, so a
  config can get behavior JPRSurf doesn't have without a change to JPRSurf.
- **Mappings never name a device.** Widgets assemble the devices' controls into
  one surface, and mappings only use widgets. Adding a second extender is a
  change to the widgets.
- **Names are stable.** Every name this page and the pages it links define is
  part of the language: keywords, device types, their controls and output
  modes, properties, and component kinds and their parameters. A config's own
  names are in namespaces of their own, so they never clash with a name JPRSurf
  adds later.
- **A config is checked as a whole.** With an error anywhere, including a
  missing required device or a command REAPER doesn't have, the surface does
  nothing until it is fixed. A config never runs half working.
- **A config costs nothing as the surface runs.** Names, templates, and
  repeated views are resolved once, when the config is loaded. Running the
  surface is the same work however the config was written.
- **Loading a config starts the surface fresh.** All of the surface's state
  (modes, anchors, held modifiers, references, and lists) comes from the
  config, so loading it again resets the surface completely.

---

## A first look

This is a complete config, for an X-Touch alone. The transport's Play and Stop
work, and the strips show the tracks at the top level of the project, with
their volume and name, and Bank scrolling them. A double press of a strip's
select button shows that track's child tracks.

```
jprsurf 1

devices {
  xtouch = XTouch(input = "X-Touch", output = "X-Touch")
}

widgets {
  include xtouch { except strip }
  strip = xtouch.strip
}

components {
  track_reference user:folder(fallback = state:master_track)
}

mappings {
  play -> cmd:40073                # Pressing Play runs the action.
  play <- cmd:1007                 # Its light shows whether REAPER plays.
  stop -> cmd:1016
}

view tracks(subject = user:folder, list = child_tracks) {
  mappings {
    bank_left -> view:bank_prev
    bank_right -> view:bank_next
  }
  view strip in strip {
    mappings {
      strip.fader <-> track:ui_volume
      strip.scribble_top <- track:name
      strip.select [double] -> view:parent_to_track
    }
  }
}
```

- [`devices`](#devices) names each device, its type, and its MIDI ports.
- [`widgets`](#widgets) names the controls the mappings use: here, all of the
  X-Touch's controls apart from its strips, by their own names, and its strips
  as `strip`.
- [`components`](#components) declares what the surface keeps track of itself:
  here, the folder whose tracks the strips show, which starts as the master
  track.
- [`mappings`](#mappings) connect controls to properties. The arrow is the way
  the value flows: `->` from the control into the property, `<-` from the
  property to the control, and `<->` both.
- A [`view`](#views) is a set of mappings with a subject, here the folder, and
  a list, here its child tracks. A view repeated over an array of strips shows
  one item of the list on each.

---

## The file

A config is a text file, conventionally with the extension `.jprsurf`. A `#`
starts a comment, to the end of the line. Line breaks are only spaces, so a
long statement can go on over several lines.

**Brackets** mean the same thing everywhere:
- `{ }` holds a **body**: a list of statements, each starting with a keyword,
  except in a `mappings` block, where each is a mapping.
- `( )` holds **parameters**, `name = value`, separated by commas: of a device,
  a component, a view, a list, a named `mappings` block, and a use. A
  parameter is given at most once, so what can be repeated is a statement in a
  body.
- `[ ]` holds a **list**: an enumerated value's values, the inputs and outputs
  a control override leaves out, and how a mapping reads its control. It also
  holds an index or a slice in a widget path.

**The version** comes first: `jprsurf 1`. This page describes version 1, the
only one. A version JPRSurf doesn't know is an error, and nothing else in the
file is read.

### Words

- A **name** is a C identifier: letters, digits, and underscores, not starting
  with a digit, such as `xtouch` or `track_strip`.
- A **name with a namespace** is a namespace and a name joined by `:`, such as
  `track:mute`, and may be followed by a field after a `.`, such as
  `state:master_track.volume`. Properties, references, modifiers, and
  declared components are named this way (see [Properties](#properties)).
- A **widget path** is a name, followed by any number of `.name` fields and
  `[index]` indices, such as `strip[3].fader` (see [Widgets](#widgets)). It
  never has a namespace, so it never reads as a property.
- A **value** is `true`, `false`, an integer (`8`), a number with a decimal
  point (`0.5`, `-1.0`), a string in double quotes (`"X-Touch"`), a color,
  `rgb(r, g, b)` with each part from 0 to 255, or a name, such as one of an
  enumerated value's values.

### Statements

The top level of a config is the surface's root view: it has what any view's
body has, and some statements that only the top level has.

| Statement                             | Where                                                        | Section                           |
| ------------------------------------- | ------------------------------------------------------------ | --------------------------------- |
| `jprsurf <version>`                   | Top level, first                                             | [The file](#the-file)             |
| `devices { ... }`                     | Top level, exactly once                                      | [Devices](#devices)               |
| `widgets { ... }`                     | Top level, exactly once                                      | [Widgets](#widgets)               |
| `settings { ... }`                    | Top level, at most once                                      | [Settings](#settings)             |
| `mappings <name>[(<params>)] { ... }` | Top level                                                    | [Named mappings](#named-mappings) |
| `components { ... }`                  | Top level, and views                                         | [Components](#components)         |
| `mappings { ... }`                    | Top level, and views                                         | [Mappings](#mappings)             |
| `view <name>[(<params>)] { ... }`     | Top level, and views                                         | [Views](#views)                   |
| `view <element> in <array> { ... }`   | Top level, and views                                         | [Showing a list](#showing-a-list) |
| `when <condition> { ... }`            | Top level, views, `mappings` blocks, and a mapping's options | [Conditions](#conditions)         |

Any statement but the version may come in any order, and blocks of the same
kind in a view (other than `devices`, `widgets`, and `settings`) add up. The
one exception is components, which may only name the components declared
before them.

---

## Devices

A device is one hardware unit. `devices` names each one, with its type and its
MIDI ports:

```
devices {
  xtouch = XTouch(input = "X-Touch", output = "X-Touch")
  xtouch_ext = XTouchExt(input = "X-Touch-Ext", output = "X-Touch-Ext",
                         optional = true) {
    without strip[2].fader [press]
  }
}
```

A device is `<name> = <type>(<parameters>)`, with an optional body of control
overrides:
- Its **name** is how widgets refer to its controls.
- Its **type** is one of the [device types](devices/README.md).
- `input` and `output` are its MIDI input and output ports, by the names REAPER
  lists them under. Both are required.
- `optional = true` makes it optional. Otherwise it is required.

### Required and optional

Devices are found when the config is loaded. A device whose ports aren't there
is **absent**.
- If a **required** device is absent, the config doesn't load, with an error
  naming the device, and the surface does nothing. A config written for a
  particular set of hardware never runs half working on a different set.
- If an **optional** device is absent, the config loads without it: its
  controls are absent, and so are the widgets defined over them (see
  [Absent devices](#absent-devices)).

### Control overrides

A control override leaves out inputs or outputs of one of the device's
controls that this unit doesn't have, for differences between units of the
same type, such as a fader that has lost its touch sensing:

```
without <control> [<inputs and outputs>]
```

The control is a path in the device's type, and the list names its inputs
(`value`, `delta`, and `press`) and outputs (`cvalue`, `dvalue`, `text`, and
`color`), as the device type's page lists them. The control must have each one.
The rest of the control works as usual: a motorized fader without a press
input, which is its touch sensing, can't tell when it is held, so it follows
REAPER again only once it stops moving.

An override doesn't change the control's shape (see [Shape](#shape)), so the
fader still matches the others in an array, and its press is just missing.

---

## Widgets

A **widget** is a named piece of the surface that mappings use, without saying
which device it is on. Widgets are the only way mappings refer to hardware, so
they are the only part of a config that knows which device a control is on. A
widget is one of:
- A **control**: one device control.
- A **struct**: named fields, each of which is a widget.
- An **array**: an ordered list of widgets that all have the same shape.

A device type's controls are described the same way, as a struct: the
X-Touch's has its buttons as fields, and a `strip` array of 8 structs, each
with its strip's controls. Each [device type's page](devices/README.md) lists
its widgets.

### Paths

A widget is named by its path. Indices start from 0, so `strip[3].fader` is
the `fader` field of the fourth element of the `strip` array. An index is an
address, not part of a name.

There are two kinds of path:
- In `widgets`, every path starts at a device, such as `xtouch.play` or
  `xtouch.strip[7]`, and addresses the device type's widgets, whatever is
  connected.
- In mappings, every path starts at one of the config's own widgets, such as
  `strip[3].fader`, which is how mappings never name a device. It addresses
  what is present, so `strip[15]` is nothing when the device with that strip
  is absent.

### Defining widgets

`widgets` is the config's top-level struct:

```
widgets {
  include xtouch { except strip }
  strip = join(xtouch_ext.strip, xtouch.strip)
  route_strip = join(xtouch_ext.strip, xtouch.strip[0..6])
  info_strip = xtouch.strip[7]
  cursor = {
    up = xtouch.up
    down = xtouch.down
  }
}
```

A struct's body holds its fields:
- **`include <path>`** adds every field of a struct, by its own names, with an
  optional body of **`except <field>`** for each field to leave out. The config
  above includes the X-Touch's buttons without naming each one.
- **`<name> = <path>`** is a field that is the widget at the path: a control, a
  struct, or an array.
- **`<name> = join(<slices>)`** is a field that is an array of the slices'
  elements, one slice after another. A slice is an array's path, optionally
  followed by `[first..last]`, or `[first..]` to its end. The config above
  joins the extender's strips and then the X-Touch's, their physical order from
  left to right.
- **`<name> = { ... }`** is a field that is a struct of its own.

A field defined twice, directly or by an include, is an error. Every path
starts at a device, so each definition stands alone: there is no order between
fields, and no clash between a device's name and a field's.

### Shape

Every element of an array must have the same shape, so that a mapping written
once for an element's `fader` is valid for every element:
- **Structs**: the same field names, with each field the same shape, all the
  way down.
- **Controls**: the same inputs, outputs, and output mode names, as their
  device types give them. Control overrides don't count.

### Absent devices

A device type's widgets never change, and an absent device's widgets are all
there, with nothing in them:
- A control on an absent device is absent, and mappings to it do nothing.
- A join leaves out the elements that come from an absent device's arrays, so
  its size depends on what is connected. Any other array of an absent device,
  reached by a path or an include, is empty, so a view repeated over it never
  shows anything on strips that aren't there.
- A struct keeps its fields, and the ones on an absent device are absent.

A config whose X-Touch Extender is optional, as above, has 16 elements in
`strip` with the extender, and 8 without it.

---

## Properties

A **property** is a named, typed value that mappings connect to controls, such
as a track's volume, or a REAPER action. The [scene's pages](scene/README.md)
list every built in property, by namespace, with its type, its access, and
what it does.

### Types

| Type              | Value                                                           |
| ----------------- | --------------------------------------------------------------- |
| action            | No value: writing it runs the action.                           |
| toggle            | On or off.                                                      |
| pan               | From -1.0 (full left) to 1.0 (full right), with 0.0 the center. |
| volume            | A gain, from 0.0 (silence), with 1.0 unity gain (0 dB).         |
| normalized        | From 0.0 to 1.0.                                                |
| text              | Text.                                                           |
| color             | A color.                                                        |
| timeline position | A position on REAPER's timeline.                                |
| enumerated        | One of a list of named values.                                  |

A property's **access** is what a mapping can do with it: **read** it (to write
a control's output), **write** it (from a control's input), or, for an action,
**trigger** it.

### Namespaces

Every property's name is a namespace and a name, joined by `:`, such as
`track:mute`. The namespace says what kind of property it is, and where its
name is looked up. The [scene's page](scene/README.md) lists the built in
namespaces, and `user:` is the config's own.
- A namespace of a view's own properties, or its subject's (as its page
  says), is looked up in the view the mapping, condition, or mode override is
  in. A `track:` property is the track that view shows, for instance.
- `user:` is looked up where it is declared: in that view, its ancestors, and
  the top level (see [Where a component is
  declared](#where-a-component-is-declared)).
- Every other namespace is global, and looked up anywhere.
- No name refers to another view's properties, as that view may be inactive.

A config's own names are in `user:`, and its modifiers in `mod:`, where a
declared modifier takes precedence over a built in one of the same name, so a
name JPRSurf adds later never breaks a config.

### References

A **reference** is a named subject, such as a track, or nothing. It isn't a
property itself, but its **fields** are: the properties of the subject it
refers to, named after a `.`, so `state:selected_track.has_routes` is the
`track:has_routes` of the track `state:selected_track` refers to.
- A reference's `exists` field is true while it refers to something. It is
  false while its track is deleted.
- Fields are one level deep: never a field of a field.
- **Built in** references are read-only. A namespace's page lists its
  references, with the namespace of their fields.
- **Declared** references (see [Components](#components)) can be changed, by
  navigation and picks, and views can show them.

Only a reference's name can be used where a reference is needed, so a property
can never be used as one.

---

## Components

A **component** is a piece of JPRSurf's behavior that a config declares, by
kind, with a name and parameters. It owns its state, provides properties, and
enforces its own rules, so a config can't use it wrong.

```
components {
  modifier mod:send_hold
  enumerated_value user:surface_mode(values = [track, send_receive])
  track_reference user:current_track(follow = state:last_touched_track)
}
```

A component is `<kind> <name>[(<parameters>)]`. The [components
page](scene/components.md) lists the kinds, each with what it provides, the
namespace of its name, where it may be declared, and its parameters. A
parameter that names a declared reference names one declared before it.

### Where a component is declared

Where a component is declared fixes its scope:
- **At the top level**: one instance, for the whole surface.
- **On a view**: one instance for each instance of the view, so each strip of
  a repeated view gets its own, under the same name. It is visible in the view
  and its descendants.

Declaring a name that is already visible where it is declared (in the same
view, an ancestor, or the top level) is an error, so a declaration never hides
another.

---

## Views

A **view** is a set of mappings, in a tree whose root is the top level of the
config. It has:
- Whether it is **enabled**: always, or while a condition is met (see
  [Conditions](#conditions)).
- Its **subject**: the REAPER thing its `track:` or `route:` properties refer
  to.
- Optionally, a **list** of subjects, which a repeated child view shows.
- Its **body**: `components` and `mappings` blocks, and child views.

A view is **active** while it is enabled and its parent is active, and only an
active view's mappings do anything. Views change between the surface's runs,
never while it is running them, so any mapping can change a view's condition.

```
view <name>[(subject = <reference>, list = <list>)] { ... }
view <element> in <array> { ... }
```

The first form is a named view, whose name is unique among its siblings. The
second is a repeated view, which has no name (see [Showing a
list](#showing-a-list)). Errors name a view by its path from the root, such as
`/track_mode/track_list/strip[]`, where `[]` is a repeated view.

### Subjects

A view's subject comes from one of:
- **Its parent**: the same subject as its parent's. This is the default, and
  the root has none.
- **A reference**, with `subject = <reference>`, such as
  `subject = state:master_track`, or a declared reference.
- **An item of its parent's list**, for a repeated view.

| Kind  | Namespace | Notes                                                                 |
| ----- | --------- | --------------------------------------------------------------------- |
| None  |           | The root, and any view under it without a subject.                    |
| Track | `track:`  |                                                                       |
| Route | `route:`  | A send or receive. `route:other_track` is the track at the other end. |

A view's subject is known without REAPER, so checking rejects a `route:`
property on a view whose subject is a track.

### Lists

A view with a track subject can have a **list**, with `list = <list>`: an
ordered list of subjects, with a **scroll position**.

| List           | Items                                                        | Parameters          |
| -------------- | ------------------------------------------------------------ | ------------------- |
| `child_tracks` | The child tracks of the view's track that are on the surface | `reveal` and `bank` |
| `routes`       | The sends or the receives of the view's track                | `type` and `bank`   |

A list without parameters can leave out its brackets: `list = routes`.
- **`bank`** is how far `view:bank_prev` and `view:bank_next` scroll. Without
  it, they scroll by the number of items shown.
- **`reveal`** is a track reference whose track the list keeps shown: when the
  track changes while the view is active, and when the view becomes active, the
  list scrolls so the track is shown, first moving the view's own reference to
  the track's folder if it is in another one.
- **`type`** is `sends` or `receives`: which routes the list shows whenever the
  view's track changes. Without it, the list shows sends, unless the track
  only has receives. `view:toggle_routes` switches it.

### Showing a list

A view with a list has exactly one repeated child view, which shows it:

```
view track_list(subject = user:folder, list = child_tracks(bank = 8)) {
  view strip in strip {
    mappings {
      strip.fader <-> track:ui_volume
    }
  }
}
```

`view <element> in <array>` repeats the view over the array widget, with one
instance for each element, in order. Instance *i* shows the item at the scroll
position plus *i*. In the view's mappings, `<element>` is that instance's
element of the array, so `strip.fader` is the fader of its own strip.
- The number of instances is the array's size, so an absent device changes it
  without changing the config. The hardware decides how many strips there are,
  and the list decides what is on them.
- A repeated view's parent must have a list. The parent can have other
  children too, which share its own subject.

### Scrolling and navigation

The [`view:` properties](scene/view.md) act on a view's list: they scroll it,
by one item or by its bank size, and move the reference it shows, such as up a
folder, or into an item's track. Each exists only on the views it can act on,
which its page says.
- The scroll position stays in range as the list changes, such as when tracks
  are deleted or hidden.
- An instance past the end of the list has an empty subject, whose `exists`
  property is false. An empty subject's values read as nothing where they can
  (no name, black, the fader down). A mapping whose value would still show
  something, such as a volume's text ("-inf dB"), needs a condition on
  `exists`, or a mode override that turns the control off.

### Modes

A mode isn't a concept of its own. It is:
- An `enumerated_value` whose values are the modes, such as
  `user:surface_mode` with `track` and `send_receive`. It is always exactly
  one of them.
- A view for each mode, enabled by a condition on its value.
- Mappings for the mode buttons: a press `set`s the value, and each mode's view
  lights the buttons, so the current mode can blink, and another be lit while
  it is available.

```
mappings {
  assign_track -> user:surface_mode { set track }
}

when user:surface_mode = track {
  view track_mode {
    mappings {
      assign_track <- true { mode blink }
    }
  }
}
```

A mode that shows a particular subject shows a declared reference. It is
entered by two mappings on the same press, with the same condition that the new
subject can be shown: a `track_pick`, which sets the reference, and a mapping
that sets the mode.

---

## Mappings

A **mapping** connects one control widget to one property, in one view:

```
<control> [<how it is read>] <arrow> <property or value> { <options> }
```

The brackets and the options are optional.

```
mappings {
  play -> cmd:40073
  play <- cmd:1007
  record <-> cmd:1013
  undo [mod:shift] -> cmd:40030
  solo [long] -> cmd:40340
  shift [held] <-> mod:shift
  save <- state:project_dirty { mode blink }
  assign_track -> user:surface_mode { set track }
}
```

The **arrow** is the way the value flows:
- `->` **reads** the control into the property: pressing a button runs an
  action, or moving a fader sets a volume.
- `<-` **writes** the property to the control: a light, a display, or a
  motorized fader shows it.
- `<->` does both.

Reading needs a property that can be written or triggered, and writing one
that can be read, which the property's access says.

### Inputs and outputs

A mapping reads the best input its control has for the property's type, and
writes the best output, in this order:

| Type                                | Inputs it reads                     | Outputs it writes           |
| ----------------------------------- | ----------------------------------- | --------------------------- |
| action                              | press                               | none                        |
| toggle                              | press, delta, value                 | dvalue, cvalue, text, color |
| pan, volume, normalized, enumerated | value, delta, press                 | dvalue, cvalue, text, color |
| color                               | value, delta, press                 | color, text, cvalue, dvalue |
| text                                | value, or press with a text `range` | text                        |
| timeline position                   | delta                               | text                        |

The inputs are a `value` (a fader's position), a `delta` (a knob's turn), and a
`press` (a button, with its release). The outputs are a `cvalue` (a continuous
value, such as a fader's position or a meter), a `dvalue` (a discrete value,
such as a light), `text`, and `color`.

### How it is read

The brackets before the arrow say how a read mapping reads its control, in any
order:
- **An input**, `value`, `delta`, or `press`, reads only that one.
- **A press behavior**:
  - `long`: a press held for 350ms, which fires as soon as it is held that
    long.
  - `double`: two presses in quick succession.
  - `tap`: a press released before it is a long press. It never delays the
    control's other presses, so a button can be held for one thing and tapped
    for another.

  Without one, a mapping reads a plain press, which waits for the release if
  the control also has a `long` or `double` mapping.
- **`held`**: the property is on while the button is held, and off when it is
  released. With `long`, it is on from when the long press fires. It reads
  only a press, and never a pan or a volume.
- **`touch`**: reads whether a hand is on the control, as a press, whatever
  inputs it has. With `held`, the property is on while the control is touched,
  such as `track:volume_touched` while a fader is.
- **Modifiers**, by name, which must be on for the mapping to fire (see
  [Modifier sets](#modifier-sets)).

### The property

After the arrow is a property's name, or in a template, a property parameter
(see [Named mappings](#named-mappings)). A write mapping can instead write a
**fixed value**, such as a light that is always on in a view, or a label:

| Value                | Written as         |
| -------------------- | ------------------ |
| `true` or `false`    | A toggle           |
| A number from 0 to 1 | A normalized value |
| A string             | Text               |
| `rgb(r, g, b)`       | A color            |

### Options

The braces after the property hold its options. A read mapping's range:
- **`set <value>`**: a press sets the value, such as `set 0.0` for a pot button
  that centers a pan, or `set track` for an enumerated value.
- **`range <min> .. <max>`**: the values the control's range maps to. A value
  input maps linearly to it, and a delta is kept within it. A press toggles
  between its ends: a pan, volume, or normalized value at one end goes to the
  other, and one between them to the nearer end, and text or a color goes to
  the max unless it is already there. On an enumerated value, a press steps
  through the range instead, wrapping around.
- **`toggle <min> .. <max>`**: on an enumerated value, a press sets the max,
  or the min if it already is the max. Nudge and Marker in JPRSurf's config use
  it, so turning one on turns the other off.

A mapping has at most one of these. Each end is a value of the property's
type: a number with a decimal point for a pan, volume, or normalized value,
text, a color, or one of an enumerated value's names. An action, a toggle, and
a timeline position have no range. Without one, a pan's range is -1.0 to 1.0,
and a normalized value's 0.0 to 1.0.

A write mapping's output mode:
- **`mode <name>`**: the output mode, by its name on the control's device type
  page, such as `blink` for a light. Without one, it is the control's first
  mode. A timeline position written to text names its format instead:
  `beats`, `time`, `frames`, or `samples`, or without one, REAPER's ruler's.
- **`when <condition> { mode <name> }`**: a mode override, which uses the mode
  while the condition is met. Overrides are checked in order, and the first one
  met picks the mode:

```
strip.pot <-> track:ui_pan {
  mode center
  when track:exists = false { mode off }
  when track:is_folder { mode center_edges }
}
```

### Sharing a control

Any number of mappings can use one control. These keep them from fighting:
- **Conditions** (see [Conditions](#conditions)). A read mapping keeps its
  input, and checks its condition when the input arrives, so switching it
  never loses a pending long or double press. While the condition isn't met,
  the input does nothing, but it still counts in the control's modifier sets
  and press behaviors, so it doesn't fall through to another mapping. A held
  mapping still applies the release of a press it applied, so what it holds
  can't stick on.
- **Modifier sets**, for reads that depend on buttons held on the surface.
- **Views**, for whole modes.
- **Press behaviors**: a press, a long press, a double press, and a tap on one
  control are different gestures.

A control that no active mapping writes clears itself (light off, ring off,
display blank), so a mode doesn't have to map the controls it doesn't use.

### Modifier sets

A read mapping can require a set of modifiers. Among all the modifiers that the
read mappings on a control require, a mapping fires only when exactly its own
set is on. Modifiers that no mapping on the control requires are ignored:

```
enter -> cmd:40214               # Insert new MIDI item.
enter [mod:shift] -> cmd:40142   # Insert empty item.
enter [mod:ctrl] -> cmd:40013    # Insert click source.
```

Pressing Enter with Shift and Ctrl both held does nothing, as no mapping
requires exactly both. Holding Alt, which no mapping on Enter requires, is
ignored, so Alt and Shift insert an empty item. A mapping meant to fire with a modifier on, whatever else
is on, needs a copy for each combination of the other modifiers its control's
mappings require.

---

## Conditions

A **condition** is that a property equals a value:

```
when <property> = <value> { ... }
when <property> { ... }
```

The second form is short for `= true`. The value is read as the property's
type would be, so `when user:transport_step = beat` compares an enumerated
value with one of its names, and `when track:exists = false` a toggle.

`when` means "applies while", and goes around what it applies to:
- **Around views**, at the top level or in a view's body, it enables them. It
  holds only views, and is outside them, so its condition is looked up in the
  parent view.
- **Around mappings**, in a `mappings` block, each of them acts only while it
  is met. A write mapping writes, and holds its control, only then.
- **Around a mode**, in a mapping's options, it is a mode override.

Each view, mapping, and mode override has one condition, so a `when` directly
in another `when` is an error, and so is one around a use of named mappings.

A condition names exactly one property, so combining states is a component's
job, or a built in property's made for it. Conditions and modifier sets are
the only logic in a config.

### Where conditions go

- **A view's condition** switches a whole set of mappings, such as a mode.
- **A mapping's condition** decides whether that mapping acts right now. For a
  write, it picks which property drives the control. For a read, it decides
  whether the input does anything.

So a condition that says whether a button does something goes on its mapping.
When one press does two things, such as picking a track and entering a mode,
each is its own mapping with the same condition.

---

## Named mappings

A `mappings` block with a name, at the top level, is a **template**: a set of
mappings that any view can use, with parameters for the widgets and properties
it is given:

```
mappings track_strip(widget strip) {
  strip.mute <-> track:ui_mute
  strip.fader <-> track:ui_volume
}

mappings auto_mode_button(widget button, property command,
                          property selected_state, property override_state,
                          property override_light = override_state) {
  ...
}
```

- **`widget <name>`** is a widget parameter, which the template's paths can
  start at.
- **`property <name> [= <default>]`** is a property parameter, which the
  template names, without a namespace, wherever a property's name can go: a
  mapping's property, a condition, or a mode override. Its default is a
  property's name, or an earlier property parameter.

A view's `mappings` block uses a template with `use`, giving each parameter
without a default:

```
mappings {
  use track_strip(strip = info_strip)
}
```

A use adds the template's mappings to the view, as if they were written there,
so their `track:`, `route:`, `view:`, and `user:` names are looked up in that
view. A template's paths start only at its widget parameters and the config's
top-level widgets, so it means the same wherever it is used. A template can use
other templates, but not itself.

---

## REAPER commands

The [`cmd:` namespace](scene/cmd.md) maps any REAPER command by its ID, as
REAPER's action list shows it, which is how a config gets behavior JPRSurf
doesn't have. Its page says how a command is named, and what type of property
it is.
- A command acts on REAPER's own state, such as its selection and its last
  touched track, not on a view's subject.
- A command REAPER doesn't have is an error, so the config doesn't load.
  Extensions and scripts register their commands when REAPER starts, before
  the config is loaded.

---

## Settings

`settings` holds values for the whole surface:

```
settings {
  track_filter mixer
}
```

**`track_filter`** is which tracks the surface shows, and so which are on it:
`mixer`, the tracks REAPER's mixer shows (the default), `track_panel`, the
tracks its track panel shows, or `all`.

---

## Checking and errors

A config is checked as a whole when it is loaded, and one with an error does
nothing: the surface stays dark until it is fixed. Every error is reported,
each starting with the file and the line it is on, then what it is about, such
as a view and a control:

```
C:\Users\me\Documents\My surface.jprsurf: line 61: /track_mode/track_list/strip[]: strip.mut: names nothing
```

A syntax error stops the reading, so only the first is reported. Otherwise,
checking reports every error it finds. It checks that:
- Every device type, control, input, output, and output mode exists in the
  device types.
- Every widget definition is valid, and every array's elements have the same
  shape.
- Every property is built in, declared where it is used, or a well formed
  command ID, and suits its subject.
- Each mapping's property can be read or written as its arrow says, through
  its control's inputs and outputs, and its options suit its property's type.
- Every condition names a property, and a value it can be compared with.
- Every view's subject, list, and repeated child fit together, and every
  reference it shows is the right kind.
- Every use gives its template's parameters.
- Each component is declared where its kind allows, hides no other name, and
  has valid parameters.

Then loading finds the devices, stops if a required one is absent, and checks
that REAPER has every command.

A command's type is only known from REAPER, so a mapping of a command that
doesn't suit its control, such as an action written to a light, isn't an
error: it just does nothing.

---

## Names

These are JPRSurf's conventions for the names it defines, and for its own
config. Nothing enforces the case of a name: a config's names only need to be
C identifiers.
- Every word is lower case, with underscores between words: properties,
  controls, struct fields, output modes, components, and parameters.
- Device types are the exception: each word is capitalized, with nothing
  between words (`XTouch`, `XTouchExt`), so a type reads differently from the
  devices a config names, such as `xtouch`.
- A property name is a namespace and a name, joined by `:`. Widget paths never
  have a namespace, so a property name never looks like a widget path, even
  though both can contain `.`.
- Indices start from 0, and are never part of a name.
- A name says what a thing is, not how it is implemented.
