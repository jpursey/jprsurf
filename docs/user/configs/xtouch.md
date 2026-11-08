# X-Touch Example Config

[`xtouch.jprsurf`](xtouch.jprsurf) is an example config for a Behringer X-Touch,
with an optional X-Touch Extender. It is a complete surface: the transport,
the utility and automation buttons, the timecode display, and two modes for the
channel strips, one for the project's tracks, and one for a track's sends and
receives. This page describes what it does, and [how it is put
together](#how-it-is-put-together), so you can use it as it is, or as the
start of your own.

---

## Contents

- [Using it](#using-it)
- [Concepts](#concepts)
- [Always available](#always-available)
- [Track mode](#track-mode)
- [Send mode](#send-mode)
- [Cheat sheet](#cheat-sheet)
- [How it is put together](#how-it-is-put-together)

---

## Using it

1. Set up JPRSurf as the [user guide](../user_guide.md) describes.
2. Copy `xtouch.jprsurf` somewhere of your own, such as `%APPDATA%\REAPER`, to
   edit as you like.
3. Pick it as the surface's config file (see [Choosing the config
   file](../user_guide.md#choosing-the-config-file)).

### Hardware

The config needs one X-Touch, and can use one X-Touch Extender. The extender is
optional, but the X-Touch isn't: with only an extender connected, the surface
does nothing.

**An extender must be physically to the left of the X-Touch.** The channel
strips are laid out in that order: the extender's 8 strips are channels 1-8,
and the X-Touch's 8 strips are channels 9-16. Putting it on the right is a
change to the config's widgets (see [Widgets](#widgets)).

Both units must be in Mackie Control (MC) mode, not HUI or standard MIDI mode,
and show in Windows under their default MIDI port names, `X-Touch` and
`X-Touch-Ext`. Under other names, change them in the config's `devices` (see
[Devices](#devices)).

### If the Save light doesn't work

If everything works except the **Save** button's light, check REAPER's undo
settings. REAPER only tracks whether a project has unsaved changes when
"undo/prompt to save" is enabled, and the light has nothing to report without
it. This is on by default, so it only comes up if you have turned it off.

---

## Concepts

### Modes

The surface is always in exactly one mode. The mode decides what the channel
strips show and what their controls do. Everything outside the strips - the
transport, the utility buttons, the timecode display, and the master fader -
works the same way in every mode.

There are two modes:

| Mode      | Shows                                                        |
| --------- | ------------------------------------------------------------ |
| **Track** | The tracks of your project, one per strip                    |
| **Send**  | The sends or receives of a single track, one route per strip |

The **Track** and **Send** buttons in the assign section choose the mode. Their
lights tell you both which mode you are in and which modes you can reach:

| Light    | Meaning                              |
| -------- | ------------------------------------ |
| Off      | This mode is not available right now |
| Solid    | This mode is available               |
| Blinking | This is the current mode             |

Track mode is always available, and the surface starts there. Send mode is
available when exactly one track is selected in REAPER, that track is visible
on the surface, and it has at least one send or receive.

The current mode keeps blinking even when it is no longer available. If you are
in Send mode and change the track selection, the Send button stays lit until
you leave.

### Modifiers

**Shift**, **Ctrl**, **Alt**, and **Option** in the Modify section are
modifiers. Hold one down and press something else; they never do anything on
their own. They can be combined, and several combinations have their own
meaning - Shift + Ctrl and Alt + Ctrl both do something different from either
one alone.

What a modifier does depends on what you press with it, so they are documented
with the controls they affect. Most of them are in
[Modifiers on track controls](#modifiers-on-track-controls). Shift also picks
the alternate action on each of the utility buttons.

Some ordinary buttons also act as modifiers while you hold them. Holding
**Send** in Track mode, for example, changes both what the select buttons do
and what their lights show. These are documented with the mode they belong to.

### Undo

Where REAPER allows it, one gesture on the surface is one undo point. Setting
mute across a range of twenty tracks is a single undo step, not twenty. Send
and receive volume and pan moves are collected into one undo point once you
stop moving the control.

REAPER holds a track fader or pan move open until the next undo point, so a
change made within about half a second of one (such as a mute, or a send
fader) joins the same undo step.

---

## Always available

Everything in this section works the same way in both modes.

### Transport

These are the buttons in the transport section, at the bottom right of the
X-Touch.

| Button      | Short press                             | Long press         | Lit when                 |
| ----------- | --------------------------------------- | ------------------ | ------------------------ |
| **Rewind**  | Previous measure (see Nudge and Marker) |                    |                          |
| **Forward** | Next measure (see Nudge and Marker)     |                    |                          |
| **Stop**    | Stop                                    |                    |                          |
| **Play**    | Play / pause                            |                    | The transport is rolling |
| **Record**  | Start or stop recording                 |                    | Recording                |
| **Nudge**   | Move by beat instead of measure         |                    | Turned on                |
| **Marker**  | Move by marker instead of measure       |                    | Turned on                |
| **Cycle**   | Toggle repeat                           |                    | Repeat is on             |
| **Click**   | Toggle the metronome                    |                    | The metronome is on      |
| **Solo**    | Toggle solo in front                    | Unsolo every track | Solo in front is on      |

**Record** follows REAPER's own transport buttons. It starts recording, and
starts playback as well if the transport was stopped or paused. Pressing it
again while recording turns recording off and leaves the transport rolling.
Stopping or pausing also turns recording off.

**Nudge** and **Marker** change what Rewind and Forward do. They are not held
down - press one to turn it on, and press it again to turn it off. Each is lit
while it is on. Only one can be on at a time, so turning one on turns the
other off.

**Solo** here is the standalone button above the transport, not a channel strip
solo. A long press unsolos every track in the project.

### Timecode display

The display shows the play position, in whichever unit REAPER's ruler is set
to.

The **SMPTE/Beats** button steps through the four ruler modes, in this order:

| Mode        | Display shows       | Light |
| ----------- | ------------------- | ----- |
| **Beats**   | Bars and beats      | Beats |
| **Time**    | Minutes and seconds | None  |
| **Frames**  | SMPTE timecode      | SMPTE |
| **Samples** | Samples             | None  |

Pressing it again from Samples returns to Beats. The **SMPTE** and **Beats**
lights only cover two of the four modes, so when neither is lit the ruler is
in Time or Samples - read the display itself to tell which.

The **Solo** indicator light beside them lights whenever any track in the
project is soloed. It is an indicator only, not a button.

### Master fader

The X-Touch's master fader always controls the master track's volume, in every
mode. It is never re-purposed.

### Utility buttons

| Button     | Short press          | Shift + press                         | Ctrl + press        | Lit when                     |
| ---------- | -------------------- | ------------------------------------- | ------------------- | ---------------------------- |
| **Undo**   | Undo                 | Redo                                  |                     | There is something to redo   |
| **Save**   | Save project         | Save a new version of the project     |                     | Blinking: unsaved changes    |
| **Cancel** | Unselect all items   | Remove time selection and loop points |                     | Any media items are selected |
| **Enter**  | Insert new MIDI item | Insert empty item                     | Insert click source |                              |

The Cancel light follows the item selection only, which is what a plain press
clears. Shift + Cancel doesn't affect it.

All of these lights follow the current project when you switch project tabs.

### Automation buttons

The automation buttons set the automation mode of every selected track, or
REAPER's global automation override while **Group** is on (see below):

| Button    | Sets the selected tracks to |
| --------- | --------------------------- |
| **Trim**  | Trim/Read                   |
| **Read**  | Read                        |
| **Touch** | Touch                       |
| **Write** | Write                       |
| **Latch** | Latch                       |

Each press is a single undo step. With no tracks selected, a press does
nothing. The master track counts as a selected track like any other.

Each button shows how many of the selected tracks are in its mode:

| Light    | Selected tracks in the mode |
| -------- | --------------------------- |
| Off      | None                        |
| Solid    | All of them                 |
| Blinking | Some of them                |

So with a single track selected, exactly one button is solid. When the selected
tracks are in different modes, each of their modes blinks, and pressing any of
the buttons puts them all in that mode. With no tracks selected, all are off.

REAPER also has a Latch Preview mode, which has no button. Tracks in Latch
Preview light nothing, but still count as selected tracks, so a mix of Latch
and Latch Preview tracks blinks Latch.

The lights follow the current project when you switch project tabs.

#### Global automation override

REAPER's global automation override (the automation control on its transport)
makes every track behave as if it were in one mode, without changing the
tracks' own modes. It can also be **Bypass**, which ignores all automation. It
is saved with each project, and isn't an undo step.

**Group** turns the override on and off, and is lit while it is on. Turning it
on restores the last override, set from either the surface or REAPER, or
Bypass if there hasn't been one since REAPER started.

While the override is on, each mode button is lit while the override is its
mode. Pressing it sets the override to its mode, or back to Bypass if it
already was. REAPER's Latch Preview override, which has no button, blinks
Latch, and pressing Latch then sets Latch.

---

## Track mode

Track mode is the default. Each channel strip shows one track of your project.

### The channel strip

| Control             | Shows / does        |
| ------------------- | ------------------- |
| **Fader**           | Track volume        |
| **Pot**             | Track pan           |
| **Pot button**      | Reset pan to center |
| **Mute**            | Mute                |
| **Solo**            | Solo                |
| **Rec**             | Monitor or record   |
| **Meter**           | Track level         |
| **Scribble top**    | Track name          |
| **Scribble bottom** | Track volume, in dB |
| **Scribble color**  | Track color         |

The pot ring tells you what kind of track you are looking at. A normal track
shows its pan as a single lit position. A **folder track** lights the
far-left and far-right ring LEDs together instead, which is how the X-Touch
stands in for the dedicated indicator on the original Mackie surface. A strip
with **no track** has its ring completely off.

### Rec

Rec arms a track either to **monitor** its input, so you can hear it without
recording it, or to **record** it. Its light shows which:

| Action       | Disarmed | Monitoring | Recording |
| ------------ | -------- | ---------- | --------- |
| Press        | Monitor  | Disarm     | Monitor   |
| Double press | Record   | Record     | Monitor   |
| Light        | Off      | Solid      | Blinking  |

- Monitoring arms the track with REAPER's record mode "Record: disable (input
  monitoring only)". You hear the input only if the track's monitoring is on
  (or auto), which Rec leaves as it is.
- Recording puts back the record mode monitoring replaced, such as MIDI
  overdub, or "Record: input" if there is none to put back (the mode was set to
  disable in REAPER, or REAPER has restarted since).
- Disarming leaves the record mode alone, so a track armed in REAPER honors it,
  and its light shows which stage that is.
- A press acts once you let go, or 150ms into a hold, as it waits to see
  whether it is a double press.

A press reaches the tracks REAPER's own grouped record arm does, and sets their
record mode too, so a group monitors or records together.

### Select

| Action               | Result                                                          |
| -------------------- | --------------------------------------------------------------- |
| Short press          | Select only this track (or unselect it, if it was the only one) |
| Double press         | Navigate into this track, if it is a folder                     |
| Long press           | Select the track, and anchor it for a range (see below)         |
| Ctrl + press         | Add or remove this track from the selection                     |
| Shift +  press       | Select the range from the last track you touched                |
| Shift + Ctrl + press | The same, but crossing folder boundaries                        |

A plain Shift range only covers tracks with the same parent as the starting
track. This differs from REAPER's default, and is usually what you want on a
surface - an expanded folder in the middle of the range doesn't drag its
children in. Add Ctrl for REAPER's "everything in between" behavior.

### Navigating the track list

| Button                    | Short press            | Long press                | Lit when                     |
| ------------------------- | ---------------------- | ------------------------- | ---------------------------- |
| **Channel Left/Right**    | Move by one track      |                           |                              |
| **Fader Bank Left/Right** | Move by 8 tracks       |                           |                              |
| **Global View**           | Go up one folder level | Go all the way to the top | There is a level to go up to |

A fader bank is always 8 tracks, whether or not you have an extender.

Going into a folder with a double press of select, and back out with Global
View, is how you move through a project with nested folders. Global View
centers the view on the folder you just left, so you don't lose your place. It
is dark at the top level, where there is nowhere further to go.

### Modifiers on track controls

Held while pressing **mute**, **solo**, or **Rec**:

| Modifier         | Result                                                      |
| ---------------- | ----------------------------------------------------------- |
| *(none)*         | Toggle this track, following grouping                       |
| **Ctrl**         | Toggle this track only, ignoring grouping and ganging       |
| **Shift**        | Set the range from the last track you touched to this track |
| **Shift + Ctrl** | The same, crossing folder boundaries                        |
| **Alt**          | Clear it on every track in the project                      |
| **Alt + Ctrl**   | Clear everywhere, then set this track only                  |
| **Alt + Shift**  | Clear everywhere, then set this track, following grouping   |
| **Option**       | Toggle this track and every selected track                  |

Alt clears every track in the whole project, not just the ones on the surface,
so a mute you can neither see nor reach cannot be left behind. Alt + Ctrl is
the quickest way to solo exactly one track from anywhere.

Rec follows the same rules with its stages. A press toggles between disarmed
and monitoring, and a double press between monitoring and recording (see
[Rec](#rec)), so "clear" is the first of the two, and "set" the second:
- With a press, **Alt** disarms every track, and **Alt + Ctrl** then arms this
  one to monitor.
- With a double press, **Alt** moves every recording track back to monitoring,
  leaving it armed, and **Alt + Ctrl** then makes this the only track that
  records, without disarming the ones you monitor.
- **Option** does the press to each track on its own, as the table in
  [Rec](#rec) says.

Held while moving a **fader** or **pot**:

| Modifier | Result                                 |
| -------- | -------------------------------------- |
| **Ctrl** | Move this track only, ignoring ganging |

### Ranged actions: hold and press

Hold **select**, **mute**, **solo**, or **Rec** on one track, and press the
same button on another track. Everything between the two is affected:

- **Select** selects exactly that range, and unselects everything else.
- **Mute**, **solo**, and **Rec** set the whole range to the held track's
  current value. Holding mute on a muted track and pressing another mute mutes
  the range; holding it on an unmuted track unmutes the range. Rec gives the
  range the held track's stage: disarmed, monitoring, or recording.

Mute and solo anchor as soon as you press them, and the press still does its
normal job. Rec anchors once its press acts (see [Rec](#rec)), so hold it a
moment before pressing the other end. To set a range to record, double press
the first Rec and hold the second press, which anchors at once. Select anchors
after a short hold, because a quick press of two selects in a row has to stay a
double press. While Select or Rec is held as an anchor, the other buttons of
its kind act at once, with no double press.

Each button has its own anchor, so holding mute on one track and select on
another gives you two independent ranges. Keep holding, and each further press
redoes the range from the same anchor. A range only covers tracks with the same
parent as the anchor track, and is a single undo point. Modifiers are ignored
while an anchor is held.

This is the same idea as Shift, with both ends under your fingers. Shift is
still useful when the two ends aren't on the surface at the same time.

### Holding Send

Hold the **Send** button while in Track mode and the surface turns into a map
of your routing:

- The **select lights** stop showing REAPER's selection and instead light for
  every track that has a send or a receive.
- **Pressing select** on one of those tracks jumps straight into Send mode for
  it.

Holding Send never changes your track selection, so you can hold it to look and
release it without anything happening. Releasing Send only switches mode if you
pressed it briefly.

---

## Send mode

Send mode shows the sends or receives of a **single track**. The folder
hierarchy has no meaning here; you are looking at one track's routing.

### Getting in and out

There are three ways in:

| How                                      | Result                                      |
| ---------------------------------------- | ------------------------------------------- |
| Short press **Send**                     | Show the selected track's routing           |
| Hold **Send**, press a **select** button | Show that track's routing                   |
| Select a different track in REAPER       | The view follows to the newly touched track |

Entering shows the track's **sends**, unless it has only receives, in which
case it shows those.

Press **Track** to leave. You return to Track mode with the Send mode track
visible among its siblings, wherever it lives in the project.

Master track routing and hardware outputs are not shown.

### Route strips

Every strip except the rightmost X-Touch strip shows one send or receive:

| Control             | Shows / does                                    |
| ------------------- | ----------------------------------------------- |
| **Fader**           | Route volume                                    |
| **Pot**             | Route pan                                       |
| **Pot button**      | Reset route pan to center                       |
| **Mute**            | Mute the route                                  |
| **Select**          | Jump to the track at the other end of the route |
| **Scribble top**    | The other track's name                          |
| **Scribble bottom** | Route volume, in dB                             |
| **Scribble color**  | The other track's color                         |

**Select walks the routing graph.** On a send, it takes you to the destination
track and shows its receives; on a receive, it takes you to the source track
and shows its sends. Either way you arrive at the other end of the route you
were looking at, and can keep going. This never changes REAPER's track
selection.

Rec and Solo are unused on route strips and stay dark. Strips past the last
route are empty, with their pot ring off.

**Channel Left/Right** moves by one route, and **Fader Bank Left/Right** pages
through a full screen of them at a time.

### The Info strip

The rightmost strip on the X-Touch (strip 8, or strip 16 with an extender) does
not show a route. It shows the **track whose routing you are looking at**, with
the same controls as a Track mode strip: fader, pot, pot button, mute, solo,
Rec, meter, name, and color. Its buttons don't anchor ranges, as there is no
other track to range to.

Its bottom scribble line shows **Send** or **Recv**, so you always know which
you are looking at. Its select button is unused.

This lets you watch and adjust the track itself without leaving the mode.

### Sends or receives

A short press of **Send** while in Send mode toggles between the track's sends
and its receives, starting from the first one. If the track only has one or the
other, it does nothing.

---

## Cheat sheet

### Global buttons

| Button      | Short press                | Shift + press         | Ctrl + press        | Long press                    |
| ----------- | -------------------------- | --------------------- | ------------------- | ----------------------------- |
| Undo        | Undo                       | Redo                  |                     |                               |
| Save        | Save                       | Save new version      |                     |                               |
| Cancel      | Unselect all items         | Remove time selection |                     |                               |
| Enter       | Insert MIDI item           | Insert empty item     | Insert click source |                               |
| Trim        | Selected tracks: trim/read |                       |                     |                               |
| Read        | Selected tracks: read      |                       |                     |                               |
| Touch       | Selected tracks: touch     |                       |                     |                               |
| Write       | Selected tracks: write     |                       |                     |                               |
| Latch       | Selected tracks: latch     |                       |                     |                               |
| Group       | Toggle automation override |                       |                     |                               |
| Rewind      | Previous measure           |                       |                     |                               |
| Forward     | Next measure               |                       |                     |                               |
| Nudge       | Toggle moving by beat      |                       |                     |                               |
| Marker      | Toggle moving by marker    |                       |                     |                               |
| Stop        | Stop                       |                       |                     |                               |
| Play        | Play / pause               |                       |                     |                               |
| Record      | Start or stop recording    |                       |                     |                               |
| Cycle       | Toggle repeat              |                       |                     |                               |
| Click       | Toggle metronome           |                       |                     |                               |
| Solo        | Solo in front              |                       |                     | Unsolo all tracks             |
| SMPTE/Beats | Cycle the ruler mode       |                       |                     |                               |
| Track       | Track mode                 |                       |                     |                               |
| Send        | Send mode                  |                       |                     | Show routing on select lights |

### Channel strip, by mode

| Control         | Track mode                    | Send mode route strip | Send mode Info strip |
| --------------- | ----------------------------- | --------------------- | -------------------- |
| Fader           | Track volume                  | Route volume          | Track volume         |
| Pot             | Track pan                     | Route pan             | Track pan            |
| Pot button      | Center pan                    | Center route pan      | Center pan           |
| Mute            | Track mute                    | Route mute            | Track mute           |
| Solo            | Track solo                    | -                     | Track solo           |
| Rec             | Monitor / record / anchor     | -                     | Monitor / record     |
| Select          | Select / into folder / anchor | Go to the other end   | -                    |
| Scribble top    | Track name                    | Other track's name    | Track name           |
| Scribble bottom | Volume                        | Route volume          | Send / Recv          |
| Meter           | Track level                   | -                     | Track level          |

### Navigation, by mode

| Button                | Track mode                   | Send mode           |
| --------------------- | ---------------------------- | ------------------- |
| Channel Left/Right    | Move by one track            | Move by one route   |
| Fader Bank Left/Right | Move by 8 tracks             | Page through routes |
| Global View           | Up a level (long press: top) | -                   |

---

## How it is put together

Everything this page describes comes from `xtouch.jprsurf`, written in the
language the [config spec](../config_spec.md) defines. This section shows how
it is put together, so you can find the part that does something, and change
it. The file itself has a comment on each part.

### Devices

```
devices {
  xtouch = XTouch(input = "X-Touch", output = "X-Touch")
  xtouch_ext = XTouchExt(input = "X-Touch-Ext", output = "X-Touch-Ext",
                         optional = true)
}
```

- The X-Touch is required, so the surface does nothing without it, and the
  extender is optional.
- `input` and `output` are the MIDI port names JPRSurf finds each unit by.
  Change them if Windows shows your units under other names.
- A control of one unit that doesn't work can be left out with a control
  override. For a fader that has lost its touch sensing, such as the
  extender's third, add a body to the device, so the fader follows REAPER only
  once it stops moving, rather than fighting your hand:

  ```
  xtouch_ext = XTouchExt(input = "X-Touch-Ext", output = "X-Touch-Ext",
                         optional = true) {
    without strip[2].fader [press]
  }
  ```

See [Devices](../config_spec.md#devices) in the spec.

### Widgets

```
widgets {
  include xtouch { except strip }
  strip = join(xtouch_ext.strip, xtouch.strip)
  route_strip = join(xtouch_ext.strip, xtouch.strip[0..6])
  info_strip = xtouch.strip[7]
}
```

These are the names the mappings use:
- Every X-Touch control but its strips, by the names on the [X-Touch's
  page](../devices/XTouch.md), such as `play` and `assign_send`.
- `strip`: every channel strip, left to right, the extender's first. Without
  the extender, it is the X-Touch's 8. To put the extender on the right, swap
  the two: `join(xtouch.strip, xtouch_ext.strip)`, and the same for
  `route_strip`.
- `route_strip`: the strips that show routes in Send mode, which are all but
  the X-Touch's last.
- `info_strip`: the X-Touch's last strip, which is Send mode's Info strip.

The X-Touch's last strip is in both `strip` and `info_strip`, and the mode
decides which one is in use. See [Widgets](../config_spec.md#widgets) in the
spec.

### Components

What the surface keeps track of itself (see
[Components](../config_spec.md#components) in the spec):

| Component                  | Kind               | What it is                                                                                                                                        |
| -------------------------- | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `mod:send_hold`            | `modifier`         | On while Send is held, which changes the select buttons in Track mode.                                                                            |
| `mod:select_anchor`        | `modifier`         | On while a select button is held as a range's anchor.                                                                                             |
| `mod:rec_anchor`           | `modifier`         | On while a Rec button is held as a range's anchor.                                                                                                |
| `user:transport_step`      | `enumerated_value` | What Rewind and Forward move by: `measure`, `beat` (Nudge), or `marker` (Marker).                                                                 |
| `user:surface_mode`        | `enumerated_value` | The mode: `track` or `send_receive`.                                                                                                              |
| `user:folder`              | `track_reference`  | The folder whose tracks Track mode shows, which starts as the master track.                                                                       |
| `user:current_track`       | `track_reference`  | The track whose routes Send mode shows. It follows the last touched track, and Track mode keeps it on the strips.                                 |
| `user:pick_selected_track` | `track_pick`       | Picks the selected track as the current track, for a tap of Send.                                                                                 |
| `user:anchor_*`            | `track_anchor`     | On each Track mode strip, the anchors held by its select, mute, solo, and Rec buttons (see [Ranged actions](#ranged-actions-hold-and-press)).     |
| `user:pick_track`          | `track_pick`       | On each Track mode strip, picks its track as the current track, for select while Send is held.                                                    |

### Named mappings

Mappings used in more than one place (see [Named
mappings](../config_spec.md#named-mappings) in the spec):
- `track_strip`: a strip showing a track the same way in every mode, which
  every Track mode strip and the Info strip use. Its select button and bottom
  scribble line are left to each mode.
- `auto_mode_button`: one automation button, with and without the global
  override, used once for each of the five.
- `list_navigation`: Channel and Bank, which scroll the list, in both modes.

### Views

The rest of the file is its views (see [Views](../config_spec.md#views) in the
spec), which follow the sections of this page:

```
(top level)          Always available: modifiers, the timecode display, the
│                    utility, automation, and transport buttons, and Track
├── master_fader     The master track's volume
├── track_mode       While user:surface_mode is track: Send's tap and light
│   └── track_list   user:folder's child tracks: Global, Channel, and Bank
│       └── strip    Repeated over strip: track_strip, select, the anchors,
│                    and select while Send is held
└── send_receive_mode  While user:surface_mode is send_receive:
    │                user:current_track's routes, the Info strip, and Send's
    │                tap and light
    └── strip        Repeated over route_strip: each route
```

A change to one mode's behavior is in that mode's view, and a change to a
button that works the same in every mode is at the top level. For instance, to
make Shift + Undo do something else, change the `undo [mod:shift]` mapping
under `# Utility buttons`, to any REAPER action's command ID (see [REAPER
commands](../config_spec.md#reaper-commands) in the spec).
