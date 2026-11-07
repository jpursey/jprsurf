<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `view:` properties

The properties of the view a mapping is in, which act on the view's list, or its parent's. Each is on only the views it needs. One without a prefix acts on the view it is in, and one starting with "parent_" acts on the parent view, from an item of its list. A "to_" property names the track it moves the view's reference to, as seen from the view it is in.

## Properties

| Property | Type | Access | Views | Description |
| --- | --- | --- | --- | --- |
| `view:scroll_prev` | action | trigger | Lists | Scrolls the view's list one item back. The scroll position is kept in range. |
| `view:scroll_next` | action | trigger | Lists | Scrolls the view's list one item forward. The scroll position is kept in range. |
| `view:bank_prev` | action | trigger | Lists | Scrolls the view's list back by its bank size. The scroll position is kept in range. |
| `view:bank_next` | action | trigger | Lists | Scrolls the view's list forward by its bank size. The scroll position is kept in range. |
| `view:to_parent_track` | action | trigger | Child tracks lists bound to a writable reference | Sets the view's reference to its track's parent track, navigating up one level, with the track that was left centered in the list if possible. This does nothing if the track has no parent track. |
| `view:to_master_track` | action | trigger | Child tracks lists bound to a writable reference | Sets the view's reference to the master track. |
| `view:parent_to_track` | action | trigger | Item views of a child tracks list bound to a writable reference | Sets the parent view's reference to this view's track, navigating into it, if it has child tracks on the surface. |
| `view:parent_to_grandparent_track` | action | trigger | Item views of a child tracks list bound to a writable reference | Sets the parent view's reference to this view's grandparent track, navigating up one level: the same as "view:to_parent_track" on the parent view. |
| `view:parent_to_master_track` | action | trigger | Item views of a child tracks list bound to a writable reference | Sets the parent view's reference to the master track: the same as "view:to_master_track" on the parent view. |
| `view:parent_to_other_track` | action | trigger | Item views of a routes list bound to a writable reference | Sets the parent view's reference to the track at the other end of this view's route, showing the other route type: from a send to the destination track's receives, or from a receive to the source track's sends. The list is scrolled so the route back to the original track is shown. This does nothing if this view has no route. |
| `view:toggle_routes` | action | trigger | Routes lists | Switches the view's routes list between sends and receives, from the start of the list. This does nothing if the track has no routes of the other type. |
| `view:route_type_name` | text | read | Routes lists | The route type the view's routes list shows: "Send" or "Recv". |
