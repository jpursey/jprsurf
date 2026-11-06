# Config files

Everything JPRSurf's surface does comes from a config file: which devices it
uses, what each control on them is mapped to, and the modes that change those
mappings. JPRSurf comes with one, `jprsurf.jprsurf`, which is the surface the
[user guide](user_guide.md) describes. You can change what the surface does by
editing a copy of it, or by writing your own.

---

## Contents

- [Which file the surface reads](#which-file-the-surface-reads)
- [The language by example](#the-language-by-example)
- [Errors](#errors)

---

## Which file the surface reads

JPRSurf's own config, `jprsurf.jprsurf`, goes beside `reaper_jprsurf.dll` in
REAPER's user plugins folder (`%APPDATA%\REAPER\UserPlugins`). A surface reads
it unless it is given another file.

To use another file, give the surface its path:

1. In **Preferences > Control/OSC/web**, select the surface, and click
   **Edit**.
2. Click **Browse...**, and pick the file. You can also type or paste its full
   path into **Config file**.
3. Click **OK**.

An empty **Config file** is JPRSurf's own config, so clearing it goes back to
that. So is `null`, which `reaper.ini` holds for a surface added before JPRSurf
had config files.

The file is read when REAPER starts, and when you click OK in that dialog, even
if the path didn't change. So after editing the file, click **Edit**, then
**OK**, rather than restarting REAPER.

The path is kept in `reaper.ini`, REAPER's own settings file, in
`%APPDATA%\REAPER`, on the surface's line, after `JPRSurf`
(`csurf_0=JPRSurf C:\Users\me\Documents\My surface.jprsurf`). It can be edited
there too, while REAPER is closed, as REAPER rewrites `reaper.ini` when it
exits.

Copy `jprsurf.jprsurf` somewhere else before changing it, rather than editing
the one beside the DLL, as installing JPRSurf again replaces that one.

---

## The language by example

These examples come from `jprsurf.jprsurf`, which is the best place to see
the whole language at work. A `#` starts a comment, to the end of the line, and
line breaks are only spaces, so a long statement can go on over several lines.

Brackets mean the same thing everywhere: `{ }` holds statements, `( )` holds
parameters (`name = value`, each given at most once), and `[ ]` holds a list.

### The version

A config starts with the version of the language it is written in:

```
jprsurf 1
```

### Devices and widgets

`devices` names each device, its type, and its MIDI ports. A device that isn't
`optional` must be there for the surface to do anything. A control that
doesn't work on one unit can be overridden, here to leave out a fader's touch
sensing:

```
devices {
  xtouch = XTouch(input = "X-Touch", output = "X-Touch")
  xtouch_ext = XTouchExt(input = "X-Touch-Ext", output = "X-Touch-Ext",
                         optional = true) {
    without strip[2].fader [press]
  }
}
```

`widgets` names the controls the mappings use, from the devices' own: all of
one device's (`include`), one control, or arrays of strips joined across
devices, left to right. Each [device type's page](devices/README.md) lists its
controls by name.

```
widgets {
  include xtouch { except strip }
  strip = join(xtouch_ext.strip, xtouch.strip)
  route_strip = join(xtouch_ext.strip, xtouch.strip[0..6])
  info_strip = xtouch.strip[7]
}
```

### Mappings

A mapping connects a control to a property: what REAPER, a track, or the
surface itself has, such as a track's volume, or a REAPER action
([the scene's pages](scene/README.md) list every property). The arrow is the
way the value flows:

```
mappings {
  play -> cmd:40073                # Pressing Play runs the action.
  play <- cmd:1007                 # Its light shows whether REAPER is playing.
  record <-> cmd:1013              # Both.
  undo [mod:shift] -> cmd:40030    # Only while Shift is held.
  solo [long] -> cmd:40340         # A long press.
  shift [held] <-> mod:shift       # On while it is held.
  save <- state:project_dirty { mode blink }
  assign_track -> user:surface_mode { set track }
}
```

In brackets before the arrow is how the control is read: the modifiers that
must be held, a `long`, `double`, or `tap` press, `held`, and `touch` (a
fader's or knob's touch sensing). The braces after it hold options: a value a
press `set`s, a `range`, a press that `toggle`s between two values, or the
light's `mode`.

### Components

`components` declares what the surface keeps track of itself, such as its own
modifiers, a value that picks a mode, or a track the surface follows. The
[components page](scene/components.md) lists each kind and its parameters.

```
components {
  modifier mod:send_hold
  enumerated_value user:surface_mode(values = [track, send_receive])
  track_reference user:current_track(follow = state:last_touched_track)
}
```

### Conditions

`when` goes around what it applies to: mappings that act only while it holds,
a light's mode, or a view (below):

```
mappings {
  when user:transport_step = beat {
    rewind -> cmd:40230
    forward -> cmd:40231
  }
  strip.pot <-> track:ui_pan {
    mode center
    when track:exists = false { mode off }
  }
}
```

### Views

A view is a group of mappings with a subject, such as a track. A `when` around
a view turns all of it on and off, which is how a mode is written. A view can
show a list, such as a folder's child tracks, with a view repeated over an
array of strips, one track on each:

```
when user:surface_mode = track {
  view track_mode {
    view track_list(subject = user:folder,
                    list = child_tracks(reveal = user:current_track,
                                        bank = 8)) {
      view strip in strip {
        mappings {
          strip.fader <-> track:ui_volume
        }
      }
    }
  }
}
```

### Named mappings

A `mappings` block with a name is a set of mappings that any view can `use`,
with parameters for the widgets and properties it is given:

```
mappings track_strip(widget strip) {
  strip.mute <-> track:ui_mute
  strip.fader <-> track:ui_volume
}

view strip in strip {
  mappings {
    use track_strip(strip = strip)
  }
}
```

---

## Errors

A config with an error does nothing: the surface stays dark until it is fixed,
rather than running half working. JPRSurf writes every error it finds to its
log, `%APPDATA%\jprsurf.log`, each starting with the file and the line it is
on, then what it is about, such as a view and a control:

```
C:\Users\me\Documents\My surface.jprsurf: line 61: /track_mode/track_list/strip[]: strip.mut: names nothing
```

Fix the errors, then reload the file (see above). A missing device that isn't
`optional` is reported the same way, as the surface can't run without it.
