<!-- Generated from the XTouch catalog by the update_device_docs target. Don't edit. -->

# XTouch

The Behringer X-Touch, in Mackie Control (MC) mode, whose MIDI ports show as `X-Touch`. It has 8 channel strips, and a master section with the master fader, the timecode display, and the transport and navigation buttons. Every button has a light, apart from the pots'.

## Widgets

| Widget | Inputs | Outputs | Modes | Description |
| --- | --- | --- | --- | --- |
| `assign_track` | press | dvalue | `solid`, `blink` | The TRACK button. |
| `assign_send` | press | dvalue | `solid`, `blink` | The SEND button. |
| `assign_pan` | press | dvalue | `solid`, `blink` | The PAN/SURROUND button. |
| `assign_plugin` | press | dvalue | `solid`, `blink` | The PLUG-IN button. |
| `assign_eq` | press | dvalue | `solid`, `blink` | The EQ button. |
| `assign_inst` | press | dvalue | `solid`, `blink` | The INST button. |
| `bank_left` | press | dvalue | `solid`, `blink` | The left BANK button. |
| `bank_right` | press | dvalue | `solid`, `blink` | The right BANK button. |
| `channel_left` | press | dvalue | `solid`, `blink` | The left CHANNEL button. |
| `channel_right` | press | dvalue | `solid`, `blink` | The right CHANNEL button. |
| `flip` | press | dvalue | `solid`, `blink` | The FLIP button. |
| `global` | press | dvalue | `solid`, `blink` | The GLOBAL VIEW button. |
| `show_name_value` | press | dvalue | `solid`, `blink` | The NAME/VALUE button. |
| `show_time_beats` | press | dvalue | `solid`, `blink` | The SMPTE/BEATS button. |
| `view_midi` | press | dvalue | `solid`, `blink` | The MIDI TRACKS button. |
| `view_inputs` | press | dvalue | `solid`, `blink` | The INPUTS button. |
| `view_audio` | press | dvalue | `solid`, `blink` | The AUDIO TRACKS button. |
| `view_inst` | press | dvalue | `solid`, `blink` | The AUDIO INST button. |
| `view_aux` | press | dvalue | `solid`, `blink` | The AUX button. |
| `view_buses` | press | dvalue | `solid`, `blink` | The BUSES button. |
| `view_outputs` | press | dvalue | `solid`, `blink` | The OUTPUTS button. |
| `view_user` | press | dvalue | `solid`, `blink` | The USER button. |
| `shift` | press | dvalue | `solid`, `blink` | The SHIFT button. |
| `option` | press | dvalue | `solid`, `blink` | The OPTION button. |
| `control` | press | dvalue | `solid`, `blink` | The CONTROL button. |
| `alt` | press | dvalue | `solid`, `blink` | The ALT button. |
| `auto_read` | press | dvalue | `solid`, `blink` | The READ/OFF button. |
| `auto_write` | press | dvalue | `solid`, `blink` | The WRITE button. |
| `auto_trim` | press | dvalue | `solid`, `blink` | The TRIM button. |
| `auto_touch` | press | dvalue | `solid`, `blink` | The TOUCH button. |
| `auto_latch` | press | dvalue | `solid`, `blink` | The LATCH button. |
| `auto_group` | press | dvalue | `solid`, `blink` | The GROUP button. |
| `save` | press | dvalue | `solid`, `blink` | The SAVE button. |
| `undo` | press | dvalue | `solid`, `blink` | The UNDO button. |
| `cancel` | press | dvalue | `solid`, `blink` | The CANCEL button. |
| `enter` | press | dvalue | `solid`, `blink` | The ENTER button. |
| `marker` | press | dvalue | `solid`, `blink` | The MARKER button. |
| `nudge` | press | dvalue | `solid`, `blink` | The NUDGE button. |
| `cycle` | press | dvalue | `solid`, `blink` | The CYCLE button. |
| `drop` | press | dvalue | `solid`, `blink` | The DROP button. |
| `replace` | press | dvalue | `solid`, `blink` | The REPLACE button. |
| `click` | press | dvalue | `solid`, `blink` | The CLICK button. |
| `solo` | press | dvalue | `solid`, `blink` | The SOLO button by the transport. |
| `rewind` | press | dvalue | `solid`, `blink` | The rewind button. |
| `forward` | press | dvalue | `solid`, `blink` | The fast forward button. |
| `stop` | press | dvalue | `solid`, `blink` | The stop button. |
| `play` | press | dvalue | `solid`, `blink` | The play button. |
| `record` | press | dvalue | `solid`, `blink` | The record button. |
| `up` | press | dvalue | `solid`, `blink` | The up arrow button. |
| `down` | press | dvalue | `solid`, `blink` | The down arrow button. |
| `scrub` | press | dvalue | `solid`, `blink` | The SCRUB button. |
| `zoom` | press | dvalue | `solid`, `blink` | The button in the middle of the arrows. |
| `left` | press | dvalue | `solid`, `blink` | The left arrow button. |
| `right` | press | dvalue | `solid`, `blink` | The right arrow button. |
| `function[0..7]` | press | dvalue | `solid`, `blink` | The function keys, F1 to F8, from the left. |
| `master_fader` | value, press | cvalue |  | The master fader, the same as a strip's. |
| `timecode` |  | text |  | The 10 digit timecode display, in fields of 3, 2, 2, and 3 digits. |
| `smpte_light` |  | dvalue | `solid`, `blink` | The SMPTE light. |
| `beats_light` |  | dvalue | `solid`, `blink` | The BEATS light. |
| `solo_light` |  | dvalue | `solid`, `blink` | The SOLO light by the display. |
| `strip[0..7]` |  |  |  | The 8 channel strips, from the left. |
| `strip[i].rec` | press | dvalue | `solid`, `blink` | The REC button. |
| `strip[i].solo` | press | dvalue | `solid`, `blink` | The SOLO button. |
| `strip[i].mute` | press | dvalue | `solid`, `blink` | The MUTE button. |
| `strip[i].select` | press | dvalue | `solid`, `blink` | The SELECT button. |
| `strip[i].pot_button` | press |  |  | Pressing the pot, which has no light. |
| `strip[i].fader` | value, press | cvalue |  | The motorized fader. Its value is its position, from 0 at the bottom to 1 at the top, and touching it presses it. |
| `strip[i].pot` | delta | dvalue | `dot`, `center`, `fill`, `spread`, `dot_edges`, `center_edges`, `fill_edges`, `spread_edges`, `off` | The rotary encoder above the scribble strip, with no end stops, which turns by a delta, and its ring of 13 lights. |
| `strip[i].meter` |  | cvalue |  | The level meter. Its value is a linear peak level, where 1 (0 dBFS) or more lights its clip light. |
| `strip[i].scribble_top` |  | text |  | The top line of the scribble strip, 7 characters. |
| `strip[i].scribble_bottom` |  | text |  | The bottom line of the scribble strip, 7 characters. |
| `strip[i].scribble_color` |  | color |  | The scribble strip's backlight, the nearest of its 7 colors, or off for black. |

## Modes

### `solid`, `blink`

The modes of `assign_track`, `assign_send`, `assign_pan`, `assign_plugin`, `assign_eq`, `assign_inst`, `bank_left`, `bank_right`, `channel_left`, `channel_right`, `flip`, `global`, `show_name_value`, `show_time_beats`, `view_midi`, `view_inputs`, `view_audio`, `view_inst`, `view_aux`, `view_buses`, `view_outputs`, `view_user`, `shift`, `option`, `control`, `alt`, `auto_read`, `auto_write`, `auto_trim`, `auto_touch`, `auto_latch`, `auto_group`, `save`, `undo`, `cancel`, `enter`, `marker`, `nudge`, `cycle`, `drop`, `replace`, `click`, `solo`, `rewind`, `forward`, `stop`, `play`, `record`, `up`, `down`, `scrub`, `zoom`, `left`, `right`, `function[i]`, `smpte_light`, `beats_light`, `solo_light`, `strip[i].rec`, `strip[i].solo`, `strip[i].mute`, `strip[i].select`.

| Mode | Description |
| --- | --- |
| `solid` | Lit when on. |
| `blink` | Blinks when on. |

### `dot`, `center`, `fill`, `spread`, `dot_edges`, `center_edges`, `fill_edges`, `spread_edges`, `off`

The modes of `strip[i].pot`.

| Mode | Description |
| --- | --- |
| `dot` | One light at the value, from left to right. |
| `center` | The lights from the center to the value. |
| `fill` | The lights from the left to the value. |
| `spread` | The lights from the center out to both sides, wider as the value rises. |
| `dot_edges` | As dot, with the outermost lights always lit. |
| `center_edges` | As center, with the outermost lights always lit. |
| `fill_edges` | As fill, with the outermost lights always lit. |
| `spread_edges` | As spread, with the outermost lights always lit. |
| `off` | Every light off, whatever the value. |
