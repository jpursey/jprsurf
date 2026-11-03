<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `route:` properties

The properties of the send or receive a view shows, and the track at its other end, whose fields are track properties, such as "route:other_track.name".

## Properties

| Property | Type | Access | Description |
| --- | --- | --- | --- |
| `route:volume` | volume | read, write | The route's volume. |
| `route:pan` | pan | read, write | The route's pan. |
| `route:mute` | toggle | read, write | True while the route is muted. |
| `route:exists` | toggle | read | True if there is a route, as what shows a route may show none. |

## References

| Reference | Fields | Description |
| --- | --- | --- |
| `route:other_track` | [`track:`](track.md) | The track at the other end of the route: the track a send goes to, or a receive comes from. It refers to nothing if there is no route. |
