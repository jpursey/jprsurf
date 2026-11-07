<!-- Generated from the scene's property catalog and component kinds by the update_user_docs target. Don't edit. -->

# The scene

What the scene provides to a config: the built in properties of each namespace, and the kinds of component a config can declare.

## Properties

| Namespace | Description |
| --- | --- |
| [`cmd:`](cmd.md) | REAPER's commands, by command id: a number, such as "cmd:40029", or a named command id registered by an extension or script, such as "cmd:_SWS_ABOUT". A command with a toggle state is a toggle, which writing runs if the value changes, and any other command is an action. A command REAPER doesn't have is no property. |
| [`state:`](state.md) | REAPER's global state, and the scene's built in references, whose fields are their subjects' properties, such as "state:master_track.volume". |
| [`mod:`](mod.md) | The surface's modifiers, which modifier sets require. A config may declare more. |
| [`track:`](track.md) | The properties of the track a view shows, and the fields of a track reference, such as "state:master_track.volume" for "track:volume". |
| [`route:`](route.md) | The properties of the send or receive a view shows, and the track at its other end, whose fields are track properties, such as "route:other_track.name". |
| [`view:`](view.md) | The properties of the view a mapping is in, which act on the view's list, or its parent's. Each is on only the views it needs. One without a prefix acts on the view it is in, and one starting with "parent_" acts on the parent view, from an item of its list. A "to_" property names the track it moves the view's reference to, as seen from the view it is in. |

## Components

The [kinds of component](components.md) a config can declare: `modifier`, `enumerated_value`, `track_reference`, `track_anchor`, `track_pick`.
