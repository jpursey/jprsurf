<!-- Generated from the scene's component kinds by the update_user_docs target. Don't edit. -->

# Components

The kinds of component a config can declare. Every parameter is written as text, and its Value is what text it takes.

## `modifier`

A "mod:" toggle backed by a modifier bit, which modifier sets can require. A scene has up to 60 besides the built in ones.

Declared at the top level.

It takes no parameters.

## `enumerated_value`

A "user:" enumerated property, which is always exactly one of its values, and whose text is its value's name. Mappings set it, and conditions on its values enable views and mappings.

Declared at the top level.

| Parameter | Value | Given | Description |
| --- | --- | --- | --- |
| `values` | A list of new names | Required | Its values, in order. |
| `initial` | One of the names in `values` | Optional | The value it starts as. Without one, it starts as the first. |

## `track_reference`

A writable "user:" reference to a track, or to nothing, which views can be bound to, and which navigation and picks set.

Declared at the top level.

| Parameter | Value | Given | Description |
| --- | --- | --- | --- |
| `fallback` | A track reference: [built in](state.md#references), or declared before it | Optional | The reference whose track it starts as, and returns to whenever it refers to nothing, or its track is deleted. Without one, it starts as, and returns to, nothing. |
| `follow` | A track reference: [built in](state.md#references), or declared before it | Optional | The reference it follows: whenever that reference's track changes to one on the surface, this one changes to it too. |

## `track_anchor`

A "user:" toggle, for a held mapping. While it is on, it holds its action's anchor on the view's track, so the action on another track acts on the range between them. The anchor is released with the button, or when the view's subject changes.

Declared on a view.

| Parameter | Value | Given | Description |
| --- | --- | --- | --- |
| `action` | One of `selected`, `mute`, `solo`, `rec_arm`, `rec_stage` | Required | The track action whose anchor it holds. |
| `modifier` | A [modifier](mod.md): built in, or declared | Optional | A modifier that is on while it is held. |

## `track_pick`

A "user:" action that sets a track reference to the source's track, if it has one.

Declared at the top level, or on a view.

| Parameter | Value | Given | Description |
| --- | --- | --- | --- |
| `reference` | A track reference: [built in](state.md#references), or declared before it | Required | The reference it sets, which must be one the config declares, as only those are writable. |
| `source` | A track reference: [built in](state.md#references), or declared before it | Required at the top level, and not given on a view | The reference whose track it picks. On a view, it picks the view's track. |
