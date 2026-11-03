<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `view:` properties

The properties of the view a mapping is in, which act on the view's list, or its parent's. Each is on only the views it needs.

## Properties

| Property | Type | Access | Views | Description |
| --- | --- | --- | --- | --- |
| `view:child_dec` | action | trigger | Lists | Scrolls the view's list one item back. The scroll position is kept in range. |
| `view:child_inc` | action | trigger | Lists | Scrolls the view's list one item forward. The scroll position is kept in range. |
| `view:bank_dec` | action | trigger | Lists | Scrolls the view's list back by its bank size. The scroll position is kept in range. |
| `view:bank_inc` | action | trigger | Lists | Scrolls the view's list forward by its bank size. The scroll position is kept in range. |
| `view:track_parent` | action | trigger | Child tracks lists bound to a writable reference | Sets the view's reference to its track's parent track, navigating up one level, with the track that was left centered in the list if possible. This does nothing if the track has no parent track. |
| `view:track_root` | action | trigger | Child tracks lists bound to a writable reference | Sets the view's reference to the master track. |
| `view:parent_track_child` | action | trigger | Item views of a child tracks list bound to a writable reference | Sets the parent view's reference to this view's track, navigating into it, if it has child tracks on the surface. |
| `view:parent_track_parent` | action | trigger | Item views of a child tracks list bound to a writable reference | The same as "view:track_parent" on the parent view. |
| `view:parent_track_root` | action | trigger | Item views of a child tracks list bound to a writable reference | The same as "view:track_root" on the parent view. |
| `view:parent_route_other_track` | action | trigger | Item views of a routes list bound to a writable reference | Sets the parent view's reference to the track at the other end of this view's route, showing the other route type: from a send to the destination track's receives, or from a receive to the source track's sends. The list is scrolled so the route back to the original track is shown. This does nothing if this view has no route. |
| `view:child_route_toggle` | action | trigger | Routes lists | Switches the view's routes list between sends and receives, from the start of the list. This does nothing if the track has no routes of the other type. |
| `view:child_route_type_name` | text | read | Routes lists | The route type the view's routes list shows: "Send" or "Recv". |
