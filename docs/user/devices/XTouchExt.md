<!-- Generated from the XTouchExt catalog by the update_user_docs target. Don't edit. -->

# XTouchExt

The Behringer X-Touch Extender, in Mackie Control (MC) mode, whose MIDI ports show as `X-Touch-Ext`. It has 8 channel strips, the same as the X-Touch's.

## Widgets

| Widget | Inputs | Outputs | Modes | Description |
| --- | --- | --- | --- | --- |
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

The modes of `strip[i].rec`, `strip[i].solo`, `strip[i].mute`, `strip[i].select`.

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
