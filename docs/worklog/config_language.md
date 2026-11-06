# Config file language and loader

JPRSurf's surface comes from a config file, written in a small language of its
own, rather than from C++. A surface's config string in REAPER names the file.
Reading a file fills in the same `SurfaceSpec` that `GetDefaultSpec()` builds
today, and `CreateScene()` builds the scene from it as before, so the scene
changes only by template parameter defaults.

The language is bespoke, parsed with Game Bits' `gb/parse`, rather than
`gb::ReadConfigFromText()`, because a `gb::Config` map is unordered, keeps no
source locations for errors, and would make each mapping an object of its own,
where a mapping should be one dense line.

## Behavior

- **JPRSurf's own surface is a config file**, `config/jprsurf.jprsurf`, which
  the build deploys beside the DLL, in REAPER's `UserPlugins` folder. It is the
  only definition of JPRSurf's surface: `GetDefaultSpec()` goes away, and the
  surface tests read the file from the source tree.
- **The config string is the file's path.** Until REAPER's dialog can pick one
  (the last CL), it is set by editing `reaper.ini` (`csurf_0=JPRSurf <path>`).
  An empty config string, or `null` (what REAPER holds for JPRSurf today), is
  the deployed `jprsurf.jprsurf`.
- **A config with an error runs nothing.** Every error is logged, with the
  file and line it is on, and the whole config is rejected: the surface does
  nothing until it is fixed, as a config never runs half working.
- **REAPER's control surface dialog picks the file** (the last CL): its JPRSurf
  panel shows the path, with a Browse button.

## Design

### The language

Brackets say what kind of thing is inside them, everywhere in the file:
- `{ }` holds a **body**: a list of statements, each starting with a keyword,
  except in a `mappings` block, where each is a mapping.
- `( )` holds **parameters**, `name = value`, separated by commas: of a device,
  a component, a view, a named `mappings` block, a use, and a list. A
  parameter is given at most once, so what can be repeated is a statement in
  a body.
- `[ ]` holds a **list**: an enumerated value's values, the inputs and outputs
  a control override leaves out, and how a mapping reads its control.

Every statement at the top level and in a view's body starts with a keyword.
`#` starts a comment, to the end of the line. The first statement is the
language's version, `jprsurf 1`, and a version the reader doesn't know is an
error.

**The top level is the scene's root view**, which has no name, condition,
subject, or list, so it is written with no `view` statement of its own. It
has what any view's body has: `components` and `mappings` blocks, child
views, and `when`. Beside them are what only the top level has: exactly one
`devices` and one `widgets`, at most one `settings`, and any number of named
`mappings` blocks (templates). Comments set the scene apart from the hardware
and the templates. The top level's components are the surface's
(`SurfaceSpec::components`), seen everywhere, and are the kinds the catalog
allows at the top level, such as modifiers and enumerated values. A view's are
its own, seen in it and below it, and are the kinds allowed on a view, anchors
and picks.

**A view's subject and list are parameters**, as each is given once:
`view track_list(subject = user:folder, list = child_tracks(bank = 8))`. Its
statements are what it may have any number of: `components` and `mappings`
blocks, which lets each group sit with a comment heading it (and components
beside the mappings that use them), and child views. A view's `components`
blocks are one list, and its `mappings` blocks another.

**Order doesn't matter**: the top level's blocks, a view's mappings, its
uses, and its blocks may be written in any order, and mean the same. Only a
view's components are in order, across its blocks, as a component names
those declared before it.

**Conditions go around what they guard**: `when <condition> { ... }` holds
statements of the scope it is in, each of which applies only while the
condition is met. Around a `view`, it is the view's condition; around
mappings, each mapping's; and in a mapping's options, around a `mode`, a mode
override (see Conditions, below).

JPRSurf's surface, which is `GetDefaultSpec()` today:

```
jprsurf 1

#------------------------------------------------------------------------------
# The hardware
#------------------------------------------------------------------------------

# An X-Touch, and an X-Touch Extender to its left, if there is one.
devices {
  xtouch = XTouch(input = "X-Touch", output = "X-Touch")
  xtouch_ext = XTouchExt(input = "X-Touch-Ext", output = "X-Touch-Ext",
                         optional = true) {
    without strip[2].fader [press]  # This unit's touch sensing is broken.
  }
}

# The X-Touch's controls apart from its strips, and the strips, left to right:
# every strip, the strips that show routes in Send/Receive mode, and the strip
# that shows its track.
widgets {
  include xtouch { except strip }
  strip = join(xtouch_ext.strip, xtouch.strip)
  route_strip = join(xtouch_ext.strip, xtouch.strip[0..6])
  info_strip = xtouch.strip[7]
}

#------------------------------------------------------------------------------
# Templates: mappings the scene uses in several places
#------------------------------------------------------------------------------

# A strip that shows a track, the same in every mode. Select and the bottom
# scribble line are left to the view.
mappings track_strip(widget strip) {
  strip.mute <-> track:ui_mute
  strip.solo <-> track:ui_solo
  strip.rec <-> track:ui_rec_arm
  strip.pot <-> track:ui_pan {
    mode center
    when track:exists = false { mode off }
    when track:is_folder { mode center_edges }
  }
  strip.pot_button -> track:ui_pan { set 0.0 }
  strip.pot [touch, held] -> track:pan_touched
  strip.fader <-> track:ui_volume
  strip.fader [touch, held] -> track:volume_touched
  strip.scribble_top <- track:name
  strip.scribble_color <- track:color
  strip.meter <- track:meter
}

# An automation mode button: without the override, it sets the selected
# tracks' mode, and with it, the override's.
mappings auto_mode_button(widget button, property command,
                          property selected_state, property override_state,
                          property override_light = override_state) {
  when state:auto_override_active = false {
    button -> command
    button <- selected_state {
      when state:selected_auto_mixed { mode blink }
    }
  }
  when state:auto_override_active {
    button -> override_state
    button <- override_light {
      when state:auto_override_latch_preview { mode blink }
    }
  }
}

# Scrolls the view's list: Channel by one item, and Bank by its bank size.
mappings list_navigation {
  channel_left -> view:child_dec
  channel_right -> view:child_inc
  bank_left -> view:bank_dec
  bank_right -> view:bank_inc
}

#------------------------------------------------------------------------------
# The scene: the components and mappings that are always there, and the views
# below them.
#------------------------------------------------------------------------------

components {
  modifier mod:send_hold
  modifier mod:select_anchor
  enumerated_value user:transport_step(values = [measure, beat, marker])
  enumerated_value user:surface_mode(values = [track, send_receive])
  track_reference user:folder(fallback = state:master_track)
  track_reference user:current_track(follow = state:last_touched_track)
  track_pick user:pick_selected_track(reference = user:current_track,
                                      source = state:selected_track)
}

# Modifiers
mappings {
  shift [held] <-> mod:shift
  control [held] <-> mod:ctrl
  alt [held] <-> mod:alt
  option [held] <-> mod:opt
}

# Timecode display
mappings {
  timecode <- state:timeline_position
  smpte_light <- state:ruler_frames
  beats_light <- state:ruler_beats
  show_time_beats -> state:ruler_mode
  solo_light <- state:any_track_solo
}

# Utility buttons
mappings {
  undo -> cmd:40029
  undo [mod:shift] -> cmd:40030
  undo <- state:can_redo
  save -> cmd:40026
  save [mod:shift] -> cmd:41895
  save <- state:project_dirty { mode blink }
  cancel -> cmd:40289
  cancel <- state:any_item_selected
  cancel [mod:shift] -> cmd:40020
  enter -> cmd:40214
  enter [mod:shift] -> cmd:40142
  enter [mod:ctrl] -> cmd:40013
}

# Automation
mappings {
  auto_group <-> state:auto_override_active
  use auto_mode_button(button = auto_trim, command = cmd:40400,
                       selected_state = state:selected_auto_trim_read,
                       override_state = state:auto_override_trim_read)
  use auto_mode_button(button = auto_read, command = cmd:40401,
                       selected_state = state:selected_auto_read,
                       override_state = state:auto_override_read)
  use auto_mode_button(button = auto_touch, command = cmd:40402,
                       selected_state = state:selected_auto_touch,
                       override_state = state:auto_override_touch)
  use auto_mode_button(button = auto_write, command = cmd:40403,
                       selected_state = state:selected_auto_write,
                       override_state = state:auto_override_write)
  use auto_mode_button(button = auto_latch, command = cmd:40404,
                       selected_state = state:selected_auto_latch,
                       override_state = state:auto_override_latch,
                       override_light = state:auto_override_any_latch)
}

# Misc buttons and transport
mappings {
  cycle <-> cmd:1068
  click <-> cmd:40364
  solo <-> cmd:40745
  solo [long] -> cmd:40340
  stop -> cmd:1016
  play -> cmd:40073
  play <- cmd:1007
  record <-> cmd:1013
}

# Rewind and Forward move by the transport step, which Marker and Nudge pick.
mappings {
  nudge -> user:transport_step { toggle measure .. beat }
  marker -> user:transport_step { toggle measure .. marker }
  when user:transport_step = measure {
    rewind -> cmd:41041
    forward -> cmd:41040
  }
  when user:transport_step = beat {
    rewind -> cmd:40230
    forward -> cmd:40231
    nudge <- true
  }
  when user:transport_step = marker {
    rewind -> cmd:40172
    forward -> cmd:40173
    marker <- true
  }
}

# Surface modes
mappings {
  assign_track -> user:surface_mode { set track }
  assign_send [held] -> mod:send_hold
}

view master_fader(subject = state:master_track) {
  mappings {
    master_fader <-> track:volume
    master_fader [touch, held] -> track:volume_touched
  }
}

when user:surface_mode = track {
  view track_mode {
    mappings {
      when state:selected_track.has_routes {
        assign_send [tap] -> user:pick_selected_track
        assign_send [tap] -> user:surface_mode { set send_receive }
      }
      assign_track <- true { mode blink }
      assign_send <- state:selected_track.has_routes
    }

    view track_list(subject = user:folder,
                    list = child_tracks(reveal = user:current_track,
                                        bank = 8)) {
      mappings {
        use list_navigation
        global -> view:track_parent
        global [long] -> view:track_root
        global <- track:has_parent
      }

      view strip in strip {
        components {
          track_anchor user:anchor_select(action = selected,
                                          modifier = mod:select_anchor)
          track_anchor user:anchor_mute(action = mute)
          track_anchor user:anchor_solo(action = solo)
          track_anchor user:anchor_rec_arm(action = rec_arm)
          track_pick user:pick_track(reference = user:current_track)
        }

        mappings {
          use track_strip(strip = strip)
          strip.mute [held] -> user:anchor_mute
          strip.solo [held] -> user:anchor_solo
          strip.rec [held] -> user:anchor_rec_arm
          when track:exists { strip.scribble_bottom <- track:ui_volume }
        }

        # Select selects, double press navigates into the track, and holding
        # it anchors a range.
        mappings {
          strip.select -> track:ui_selected
          strip.select [double] -> view:parent_track_child
          strip.select [long] -> track:ui_selected
          strip.select [long, held] -> user:anchor_select
          strip.select [mod:select_anchor] -> track:ui_selected
          when mod:send_hold = false { strip.select <- track:ui_selected }
        }

        # While Send is held, select enters Send/Receive mode for the track,
        # and is lit if it has routes.
        mappings {
          when track:has_routes {
            strip.select [mod:send_hold] -> user:pick_track
            strip.select [mod:send_hold] -> user:surface_mode {
              set send_receive
            }
            strip.select [mod:send_hold, mod:select_anchor] -> user:pick_track
            strip.select [mod:send_hold, mod:select_anchor]
                -> user:surface_mode { set send_receive }
          }
          when mod:send_hold { strip.select <- track:has_routes }
        }
      }
    }
  }
}

when user:surface_mode = send_receive {
  view send_receive_mode(subject = user:current_track, list = routes) {
    mappings {
      use list_navigation
      use track_strip(strip = info_strip)
      when track:exists {
        info_strip.scribble_bottom <- view:child_route_type_name
      }
      assign_send [tap] -> view:child_route_toggle
      assign_send <- true { mode blink }
      assign_track <- true
    }

    view strip in route_strip {
      mappings {
        strip.select -> view:parent_route_other_track
        strip.mute <-> route:mute
        strip.pot <-> route:pan {
          mode center
          when route:exists = false { mode off }
        }
        strip.pot_button -> route:pan { set 0.0 }
        strip.fader <-> route:volume
        strip.scribble_top <- route:other_track.name
        strip.scribble_color <- route:other_track.color
        when route:exists { strip.scribble_bottom <- route:volume }
      }
    }
  }
}
```

Line breaks are only whitespace, so a long mapping can continue on the next
line.

#### Statements

| Where | Statement | Spec |
| --- | --- | --- |
| Top level, first | `jprsurf <version>` | Checked by the reader only |
| Top level, once | `devices { ... }` | `SurfaceSpec::devices` |
| Top level, once | `widgets { ... }` | `SurfaceSpec::widgets` |
| Top level, at most once | `settings { track_filter <mixer \| track_panel \| all> }` | `SurfaceSpec::track_filter` |
| Top level | `mappings <name>[(<params>)] { ... }` | `TemplateSpec` |
| Top level and views | `components { ... }` | At the top level, `SurfaceSpec::components`; in a view, `ViewSpec::components` |
| Top level and views | `mappings { ... }` | `MappingSpec`s and `TemplateUse`s |
| Top level and views | `view <name>[(subject = <reference>, list = <list>)] { ... }` | A child `ViewSpec` (at the top level, of `SurfaceSpec::root`), with `View::ReferenceSubject` and `View::ListConfig` |
| Top level and views | `view <element> in <array> { ... }` | A repeated child `ViewSpec` |
| Top level and views, `mappings`, a mapping's options | `when <condition> { ... }` | Each view's `View::Config::condition`, each mapping's `ViewMapping::Config::condition`, or a `ModeOverride` |
| `devices` | `<name> = <type>(input = "<port>", output = "<port>"[, optional = true])`, with an optional body of `without <control> [<inputs and outputs>]` | `DeviceSpec` |
| `widgets` | `include <path>`, with an optional body of `except <field>`, and `<name> = <path>`, `join(<slices>)`, or `{ ... }` for a nested struct. A slice is an array path, with `[first..last]` or `[first..]` | `WidgetStructDefinition` |
| `components` | `<kind> <name>[(<param> = <value>, ...)]` | `ComponentSpec` |
| `mappings` | `use <name>[(<param> = <argument>, ...)]` | `TemplateUse` |
| `mappings` | A mapping (below) | `MappingSpec` |

**Named mappings** are a template: a `mappings` block with a name, at the top
level, is a set of mappings that any view can use, as if they were written
there, and does nothing until one does. Its parameters are each
`widget <name>` or `property <name> [= <default>]`, where a default is a
property name or an earlier property parameter. Its body is the same as any
`mappings` block's. Its properties are found where it is used, as `track:` is
the subject of the view using `track_strip`, so it may name any component the
views using it can see: those of the top level, which every view can, and
those of the view a use is in or above it. A use where a name isn't visible is
an error at that use, named through the template, as checking already names
it (`/track_mode/track_list/strip[]: track_strip: strip.mute`). Its widgets are only its widget parameters and the top-level
widgets, so it means the same wherever it is used.

**A view's list** is `child_tracks[(reveal = <reference>, bank = <size>)]`,
the child tracks of its subject, or `routes[(type = sends | receives, bank =
<size>)]`, its subject's routes. The name `tracks` is left for a list of the
project's tracks, should one come.

**A repeated view** is named by its element, as its errors already name it
(`/track_mode/track_list/strip[]`), and its instances by its array's elements.

**A device** names both of its ports, as every device type uses both today.

Components are read from the catalog (`GetComponentKinds()`): a kind's name is
its keyword, and its parameters' names are its parameters', so a new component
kind needs no change to the grammar, only a few lines that fill in its
`ComponentSpec` (see Brittleness).

#### Mappings

```
<control> [[<how it is read>]] -> | <- | <-> <property or value> [{ <options> }]
```

- **The arrow** is the direction the value flows: `->` reads the control into
  the property, `<-` writes the property to the control, and `<->` does both.
- **How the control is read**, in brackets before the arrow: an input (`press`,
  `value`, or `delta`) where the property's type doesn't pick the right one, a
  press behavior (`long`, `double`, or `tap`), `held` (on while held, off on
  release), `touch` (reads the hand on the control), and the required
  modifiers, by name (`[mod:send_hold, mod:select_anchor]`).
- **The property** is a property name, or a fixed value (`true`, a number, a
  quoted string, or `rgb(r, g, b)`), for which the scene adds a `const:`
  property: `true` is a toggle, a number normalized, a string text, and
  `rgb()` a color. In named mappings, a property parameter is written by its
  name, with no namespace.
- **Options,** in a body after the property, one statement each:
  - `set <value>`: a press sets the value (a range whose ends are the same).
  - `range <min> .. <max>`: the property values the control's range maps to,
    which a press steps through on an enumerated property.
  - `toggle <min> .. <max>`: a press toggles between the ends (press toggles).
  - `mode <mode>`: the output mode.
  - `when <condition> { mode <mode> }`: a mode override, which picks the mode
    while its condition is met. The first override met wins, in the order
    written, and the plain `mode` otherwise: each is a `ModeOverride` with one
    value.

#### Conditions

A condition is `<property>`, which is `= true`, or `<property> = <value>`. A
value is `true`, `false`, a number, a quoted string, or a name, such as one of
an enumerated value's values.

- **`when`** means "applies while", whatever it holds. Around a `view`, the
  view is enabled while it is met, which is how a mode is written. Around
  mappings, each mapping acts only while it is met. Around a `mode`, the mode
  is used while it is met. The first two differ as the config model's "Where
  conditions go" says: a view that isn't enabled has no mappings, while a
  mapping whose condition isn't met keeps its control's input.
- **Only `mode` is conditional** among a mapping's options: a `set`, `range`,
  or `toggle` in a `when` is an error.
- **Only views are conditional** at the top level and in a view's body:
  conditional mappings are written in a `mappings` block, so a `when` around a
  `components` or `mappings` block, or anything only the top level has, is an
  error.
- **One condition each.** A view, a mapping, and a mode override each have
  one, so a `when` directly inside another `when` is an error ("combine
  states in a component or a state row"), as is a `when` around a `use`, as a
  use has no condition. A `when` around a view, one around mappings in that
  view, and one around a mode in one of those mappings, are conditions on
  three different things, and are fine.
- A `when` around a view is outside it, so its condition sees the parent's
  names, not the view's own components, as the scene creates it.

### Errors

- **Every error starts with where it is**: the file, then `line <n>: `, then
  what it is about and what is wrong, as `ValidateSurfaceSpec()` words it
  today, such as `jprsurf.jprsurf: line 61: /track_mode/track_list/strip[]:
  track_strip: strip.mute: ...`. A mapping in named mappings is on its own
  line, and the view path says where it was used.
- **Where each part of the spec was written** is kept in the spec itself: each
  `DeviceSpec`, control override, widget include and field, component,
  template, use, view, and mapping has a `SpecLine` (`device/spec_line.h`), the
  line it was written on, counting from 1, which checking adds to the start of
  its errors (`GetSpecError()`). A spec written in C++ has line 0, which
  errors leave out. A line always compares equal, as it isn't part of what the
  spec says.
- **Only the plugin knows the file.** It opens the file, so it adds the file's
  path to the start of every error it logs, from reading and checking alike.
  Neither the spec nor `config` holds a file name, as only a config needs one,
  and the plugin has it.
- **Reading reports every error it can.** A syntax error stops reading, as
  `gb::Parser` doesn't recover from one, but the rest (a component kind or
  parameter that doesn't exist, a parameter given twice, a block missing or
  given more often than it may be, a statement where it can't be (such as
  named mappings in a view), a value of the wrong form, a condition in a condition,
  an unknown version) are all reported, and
  then checking reports everything it finds.
- **Any error, from reading or checking, rejects the whole config**:
  `PluginSurface` logs each error, and runs no surface.

### config (new library)

A new library, `jpr_config`, between `scene` and `plugin`, so the order
becomes `common` → `device` → `scene` → `config` → `plugin`. It owns the
language, and keeps `gb_parse` out of `scene`.

```
// Reads a config's text into a spec, which is null if there is an error, each
// added to `errors` (if provided), starting with the line it is on (see
// GetSpecError()). The spec isn't checked (see ValidateSurfaceSpec()), but each
// part of it has its line, so checking's errors start with it too.
std::optional<SurfaceSpec> ReadSurfaceConfig(std::string text,
                                             std::vector<std::string>* errors);
```

- The grammar is a `gb::ParserProgram`, with a user token for a property name
  (`<namespace>:<name>`, with an optional `.<field>`), so a property never
  reads as a widget path.
- A `when` fills in the condition of each view or mapping it holds, or adds a
  mode override, so it isn't in the spec as a block.
- A view's `mappings` blocks are read into its one list of mappings and its
  one list of uses, as order doesn't matter.
- Everything runs once, when the surface is created. Nothing changes per run.

### plugin

- `PluginSurface` reads its config string: empty (or `null`) is
  `jprsurf.jprsurf` in the folder the DLL is in, and anything else a path. It
  reads the file with `gb/file`, then creates the scene, logging every error
  with the file's path in front (`<path>: line 61: ...`).
- `GetConfig()` returns the config string it was given.
- `GetDefaultSpec()` and `default_spec.cc` go. `DefaultSpecTest` becomes a test
  that `config/jprsurf.jprsurf` reads and is valid, and the surface tests build
  their scene from it, by its path in the source tree.
- The deploy copies `config/jprsurf.jprsurf` beside the DLL, as it does the
  DLL (`JPR_DEPLOY_TO_REAPER`).
- The file groups mappings by condition, so a view's mappings are in a
  different order than `GetDefaultSpec()` adds them. This changes no behavior,
  but may move a few cheap calls in the call count bounds (see *Mapping order
  changes REAPER calls* in the backlog), which the CL that switches to the
  file updates.

### common

`ControlSurface::ShowConfig()` shows a panel in REAPER's control surface
dialog, with the config file's path and a Browse button, when the surface type
asks for one (a file filter in `ControlSurface::Type`). It is Win32, in a
`win_` file, behind a small interface.

### scene

- **Template parameter defaults:** a property parameter may have a default, a
  property name or an earlier property parameter. A use gives every parameter
  without one, and may give one with one. `ValidateSurfaceSpec()` checks a
  default as it checks an argument, and `CreateScene()` uses it where a use
  gives none. `GetDefaultSpec()` uses it for `override_light`.
- **`operator==`** on every spec struct (and on `View::Config`,
  `ViewMapping::Config`, `ViewCondition::Config`, and the device and widget
  specs they hold), for the reader's tests and the parity test.

### Names

| New | Candidates | Recommendation, and what it avoids |
| --- | --- | --- |
| The library | `config`, `spec`, `surface_file` | `config`: it is what the docs call the file. `spec` would suggest `SurfaceSpec` lives there. It collides only with Game Bits' `gb_config`, whose one use (the old config string) goes away. |
| The file extension | `.jprsurf`, `.surf`, `.jprs` | `.jprsurf`: unmistakably JPRSurf's, and matches the version statement. |
| The reader | `ReadSurfaceConfig()`, `ParseSurfaceConfig()`, `LoadSurfaceConfig()` | `ReadSurfaceConfig()`, as Game Bits' `ReadConfigFromText()`. "Load" suggests a file, which it doesn't open. |
| Where a part was written | `SpecLine`, `SpecLocation`, `SourceLine` | `SpecLine`: a part of a spec, as `*Spec` is, and only its line, as only the plugin needs the file. `SpecLocation` would suggest the file too. |
| A template parameter's default | `default_value`, `fallback`, `default_arg` | `default_value`: plain, and `default` alone is a C++ keyword. `fallback` is already a track reference's parameter. |
| Track filter values | `mixer`, `track_panel`, `all`; or `mcp`, `tcp`, `all` | `mixer` and `track_panel`: REAPER's own words for the panels, rather than its abbreviations. |

Keywords are the config model's words where it has one: `when` (condition),
`mode`, `held`, `long`, `double`, `tap`, `touch`, `range`, `subject`, `list`,
`child_tracks`, `routes`, `bank`, `reveal`. New ones: `devices`, `widgets`,
`components`, `mappings`, `settings`, `view ... in` (a repeated view), `set`,
`toggle` (press toggles), `without` (a control override), `join`, `include`,
`except`, `use`, `input` and `output` (ports), and `widget` and `property`
(parameters).

**Brittleness:**
- Each component kind needs a few lines in the reader that fill in its
  `ComponentSpec` from its parameters' text. A test reads one of each kind in
  `GetComponentKinds()`, with every parameter, so a kind added without them
  fails.
- A line that always compares equal is easy to forget when debugging a
  parity failure, but the alternative (clearing lines before comparing) is a
  step every comparison has to remember.
- A named `mappings` block that nothing uses does nothing, silently, like a
  template today.

### To confirm

Checked in the user's REAPER, with temporary code, before the CLs that rely on
them:
1. **Paths in the config string:** a path with spaces, written into
   `reaper.ini` by hand (`csurf_0=JPRSurf <path>`), reaches the surface whole.
   Needed before CL8.
   - **Confirmed** (2026-11-05, CL1): `csurf_0=JPRSurf E:\...\my  test
     surface.jprsurf`, with spaces after it, reached `Create()` as the path
     alone, unquoted, its double space kept and the spaces after it dropped,
     so the reader trims nothing. On exit, REAPER wrote the same line back to
     `reaper.ini` without calling `GetConfigString()`, so that is only for the
     dialog (3, below). Both are in "Seen in traces" in
     `docs/testing_and_profiling.md`. The fake has no `reaper.ini`, and tests
     give the config string directly, so nothing changes in it.
2. **The dialog:** REAPER shows the window `ShowConfig()` returns in its
   control surface dialog, and on OK sends it `WM_USER + 1024` (`wParam` the
   buffer's size, `lParam` the buffer) for the config string, as the SDK's
   `reaper_csurf` examples handle. Needed before CL10.
3. **Recreating the surface:** on OK, REAPER destroys the surface and creates
   it with the new config string, and whether it does so when the string
   hasn't changed (which decides whether picking the same file again reloads
   it). Needed before CL10.

## Decisions

- **Arrows for mappings**, control first.
- **An empty config string**, or `null`, is the deployed `jprsurf.jprsurf`, so
  nothing breaks before `reaper.ini` is edited, and a new surface works
  without one.
- **Not compiled into the DLL**: the file is deployed beside it, and
  `reaper.ini` is edited by hand until the dialog picks one.
- **A config with an error** is logged, and rejected whole.
- **A spec keeps lines, not files:** each part has the line it was written on
  (0 if it wasn't read from a config), and only the plugin, which opens the
  file, names it in errors, so a file name isn't pushed into every spec.
- **Order doesn't matter**, for uses as for mappings, so the spec keeps its two
  lists. The few cheap calls the order moves are *Mapping order changes
  REAPER calls*, low in the backlog (reworded from *Keep a view's uses and
  mappings in the order written*).
- **Template parameter defaults**, in the spec.
- **The file picker** is the last CL, as the UI REAPER asks for.
- **Structure:** the top level is the root view, with no `view` statement, as
  one only added an indent and braces around most of the file; beside its
  components, mappings, and views are the hardware and the templates, which
  only the top level has, set apart by comments; every statement has a leading keyword; templates are named `mappings`
  blocks; a view's subject and list are its parameters, and its `components`
  and `mappings` blocks may be several; a repeated view is
  `view <element> in <array>`; a device
  has an input and an output port, as parameters; an include's exceptions are
  `except` statements in its body; and conditions, of views, mappings, and
  modes alike, are `when` blocks around what they guard.

### Not in this feature

For the backlog: including other files; template parameters that are values
(which the transport steps would use, rather than being written out); a
`when` around a use; and a device type with only an input or an output, which
would leave the other port out. Already there: *Reload the config without
restarting REAPER*, and *Public config spec and user guide split*.

`when` blocks give *Share a condition across mappings* its ergonomics (a
condition written once), which the backlog item is narrowed to the rest of:
watching it once.

## CLs

### CL1 [x] plugin: Check paths in REAPER's config string

Depends on: nothing.

- Temporary logging of the config string the surface is created with, while
  the user puts a path with spaces in `reaper.ini`.
- Commits only the finding, in To confirm.

**Verify**
- The user edits `reaper.ini`, runs REAPER, and reports what the log shows.

### CL2 [x] device: Where device and widget specs were written

Depends on: nothing.

- `SpecLine` (`device`), which always compares equal, and `GetSpecError()`,
  which writes an error as `line <n>: <subject>: <message>`, leaving out what
  is missing.
- `DeviceSpec`, `ControlOverride`, the widget includes and fields, and
  `WidgetDevice` gain one, and the specs `operator==`. Their errors start with
  the line, where there is one.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- Tests that an error starts with its part's line, and that a spec with none
  reads as today.

### CL3 [x] scene: Where spec parts were written

Depends on: CL2.

- Components, templates, uses, views, and mappings gain a `SpecLine`, and
  every spec struct (and the configs they hold) `operator==`.
  `ValidateSurfaceSpec()` and `CreateScene()` start each error with the line
  of the part it is about (`GetSpecError()`).
- `SpecPart` (`device`), a line and the text an error starts with, which
  `GetSpecError()` takes, so the device, widget, and scene checks share it.

**Verify**
- Standard checks.
- Tests of errors about a mapping, a mapping in a template, a component, a
  view, and a use, with and without lines.

### CL4 [x] scene: Template parameter defaults

Depends on: CL3.

- A property parameter's default, checked and used as Scene above says.
  `GetDefaultSpec()`'s `auto_mode_button` uses it for `override_light`, so
  only Latch passes one.

**Verify**
- Standard checks, and the surface tests, unchanged.
- Tests of a use that leaves out a parameter with a default, and one that
  gives it; a default naming an earlier parameter, a later one, and a
  property that doesn't exist; and a use that leaves out one without a
  default.

### CL5 [x] config: Reading devices, widgets, settings, and components

Depends on: CL3.

- The `jpr_config` library, with `gb_parse`: the grammar, the version, the
  top-level blocks but named mappings, and the top level's `components`
  blocks. CLAUDE.md's library order and directory structure name it.
- `ReadSurfaceConfig()`, with every error at its line, written by
  `GetSpecError()`, so `gb_parse`'s errors read as checking's do. Its lines
  count from 0, so the reader adds 1.
- Widget paths are read with `device`'s `kWidgetPathRules`, so there is one
  path grammar.
- Reading checks only what a spec can't hold: a parameter left out is found
  by checking the spec, which knows where each component is, apart from a
  required choice (a track anchor's action). So `CreateWidgetDevices()` now
  reports a device with no input or output port, and `ValidateWidgets()` a
  slice that ends before it starts, as it is empty.

**Verify**
- Standard checks.
- Tests of each statement, each component kind with every parameter, and each
  error: syntax, an unknown kind or parameter, a parameter given twice, a
  block missing or given twice, an unknown version.

### CL6 [x] config: Reading mappings and views

Depends on: CL4, CL5.

- Named mappings and their parameters, uses, the top level's mappings and
  views, repeated views, a view's components, mappings, and `when` around views, mappings, and modes.
- A use's arguments are sorted into widgets and properties by its template's
  parameters, which reading reads first, as a use may come before its
  template. An argument the template doesn't have, or of a template that
  doesn't exist, goes by how it is written, for checking to report, as it
  does an argument its parameter can't be (a path for a property).
  `ValidateSurfaceSpec()` now reports a widget and a property parameter with
  the same name, which a use couldn't tell apart, and a modifier a mapping
  requires twice, which the spec can hold.
- `true` and `false` are keywords. A plain name as a mapping's property is a
  template's property parameter, which checking finds isn't one outside a
  template. A number keeps its type (an integer or a float), as checking
  reports one of the wrong type, except as a fixed value, which is
  normalized. Floats may be negative (`set -0.5`), and integers may not, as
  they are widget path indexes.
- Errors are about the view's path, as checking's are (`GetViewPath()`), and a
  mapping's about its control too (`/track_mode: assign_send: ...`).
- The design's whole config, above, reads and passes checking.

**Verify**
- Standard checks.
- Tests of each mapping form, fixed values of each type, each view parameter
  and statement, components in several blocks, `when` around a view, a
  mapping, several mappings, and a mode, and the errors of each: a `when` in
  a `when`, a `when` around a use, one around an option other than `mode`,
  and one around a block. A config with a checking error reports it at the mapping's line.

### CL7 [x] config, plugin: JPRSurf's config as a file

Depends on: CL6.

- `config/jprsurf.jprsurf`, the design's config with `GetDefaultSpec()`'s
  comments, which CLAUDE.md's directory structure names.
- A parity test: the file, read, equals `GetDefaultSpec()`, compared part by
  part, and each view's mappings and uses regardless of order, so a difference
  names the view and the mapping (`line 287: /track_mode/track_list/strip[]:
  strip.rec -> user:anchor_rec_arm is in the file, but not GetDefaultSpec()`).
  `jpr_plugin_test` finds the file by its path in the source tree
  (`JPR_CONFIG_FILE`).
- The file reads with Windows line endings (as git checks it out), but a UTF-8
  byte order mark is a syntax error at line 1.

**Verify**
- Standard checks.
- The parity test.

### CL8 [ ] plugin: The surface from its config file

Depends on: CL1, CL7.

- `PluginSurface` reads its config string as a path (empty or `null` for the
  deployed file), reads the file, and creates the scene from it, logging every
  error. `GetConfig()` returns the config string.
- The deploy copies the file beside the DLL.
- `GetDefaultSpec()` goes; `DefaultSpecTest` and the surface tests use
  `config/jprsurf.jprsurf`. The parity test goes with it. Any call count bound
  the file's order moves is updated, saying why.
- The user guide gets a page on config files (`docs/user/config_files.md`):
  setting one, the language by example, and errors.

**Verify**
- Standard checks, and the surface tests, unchanged.
- Tests of a config string that is empty, `null`, a file, a missing file, and
  a file with errors (which logs them, and creates no scene).
- Load check.

### CL9 [ ] plugin: Check REAPER's config dialog

Depends on: nothing.

- Temporary code in the user's REAPER for the dialog facts in To confirm: a
  `ShowConfig()` that shows a window and logs what REAPER sends it, and
  logging in the surface's constructor and destructor.
- Commits only the findings, in To confirm and in "Seen in traces" in
  `docs/testing_and_profiling.md`.

**Verify**
- The user opens the dialog, picks OK with and without a changed string, and
  reports what the log shows.

### CL10 [ ] common: Picking the config file in REAPER's dialog

Depends on: CL8, CL9.

- `ControlSurface::ShowConfig()` shows the path and a Browse button when the
  type gives a file filter, and answers `WM_USER + 1024` with the path.

**Verify**
- Standard checks.
- In the user's REAPER: picking a file, and going back to the default.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation, and
  move anything left undone into the backlog, in the same change.

## Checks in REAPER

- The surface works as before, with the config string REAPER already has
  (`null`), from the deployed file.
- Point `reaper.ini` (and later, the dialog) at a copy of
  `config/jprsurf.jprsurf` with one mapping changed: the change shows on the
  hardware.
- Break the copy (an unknown property): the log has the error and its line,
  and the surface does nothing. Fix it: it works.
- REAPER loads the extension with no new errors in `jprsurf.log`.
