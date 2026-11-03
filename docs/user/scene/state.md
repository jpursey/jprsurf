<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `state:` properties

REAPER's global state, and the scene's built in references, whose fields are their subjects' properties, such as "state:master_track.volume".

## Properties

| Property | Type | Access | Description |
| --- | --- | --- | --- |
| `state:any_track_solo` | toggle | read | True while any track is soloed. |
| `state:can_redo` | toggle | read | True while the current project has anything to redo. |
| `state:project_dirty` | toggle | read | True while the current project has unsaved changes. This is always false if "undo/prompt to save" is disabled in REAPER's preferences. |
| `state:any_item_selected` | toggle | read | True while any media items are selected in the current project. |
| `state:selected_auto_trim_read` | toggle | read | True while any selected track, including the master track, is in Trim/Read automation mode. |
| `state:selected_auto_read` | toggle | read | True while any selected track, including the master track, is in Read automation mode. |
| `state:selected_auto_touch` | toggle | read | True while any selected track, including the master track, is in Touch automation mode. |
| `state:selected_auto_write` | toggle | read | True while any selected track, including the master track, is in Write automation mode. |
| `state:selected_auto_latch` | toggle | read | True while any selected track, including the master track, is in Latch automation mode. |
| `state:selected_auto_latch_preview` | toggle | read | True while any selected track, including the master track, is in Latch Preview automation mode. |
| `state:selected_auto_mixed` | toggle | read | True while the selected tracks, including the master track, are in more than one automation mode. |
| `state:auto_override_active` | toggle | read, write | True while any global automation override is on. Turning it on restores the last override it saw on, or Bypass if there hasn't been one since REAPER started. Turning it off removes the override. |
| `state:auto_override_trim_read` | toggle | read, write | True while the global automation override is Trim/Read. Turning it on sets the override to Trim/Read, and turning it off sets it to Bypass. |
| `state:auto_override_read` | toggle | read, write | True while the global automation override is Read. Turning it on sets the override to Read, and turning it off sets it to Bypass. |
| `state:auto_override_touch` | toggle | read, write | True while the global automation override is Touch. Turning it on sets the override to Touch, and turning it off sets it to Bypass. |
| `state:auto_override_write` | toggle | read, write | True while the global automation override is Write. Turning it on sets the override to Write, and turning it off sets it to Bypass. |
| `state:auto_override_latch` | toggle | read, write | True while the global automation override is Latch. Turning it on sets the override to Latch, and turning it off sets it to Bypass. |
| `state:auto_override_latch_preview` | toggle | read, write | True while the global automation override is Latch Preview. Turning it on sets the override to Latch Preview, and turning it off sets it to Bypass. |
| `state:auto_override_any_latch` | toggle | read | True while the global automation override is Latch or Latch Preview. |
| `state:auto_override_bypass` | toggle | read, write | True while the global automation override is Bypass. Turning it on sets Bypass, and turning it off removes the override. |
| `state:timeline_position` | timeline_position | read | The playback position while playing or paused, and otherwise the edit cursor's position. |
| `state:playback_position` | timeline_position | read | The playback position. |
| `state:edit_position` | timeline_position | read | The edit cursor's position. |
| `state:ruler_mode` | enumerated | read, write | The primary ruler's mode: 0 for beats, 1 for time, 2 for frames, and 3 for samples. |
| `state:ruler_beats` | toggle | read, write | True while the primary ruler is in beats. Turning it on sets the primary ruler to beats, and turning it off does nothing, as the primary ruler always has a mode. |
| `state:ruler_time` | toggle | read, write | True while the primary ruler is in time. Turning it on sets the primary ruler to time, and turning it off does nothing. |
| `state:ruler_frames` | toggle | read, write | True while the primary ruler is in frames. Turning it on sets the primary ruler to frames, and turning it off does nothing. |
| `state:ruler_samples` | toggle | read, write | True while the primary ruler is in samples. Turning it on sets the primary ruler to samples, and turning it off does nothing. |
| `state:secondary_ruler_mode` | enumerated | read, write | The secondary ruler's mode: 0 for none, 1 for time, 2 for frames, and 3 for samples. |
| `state:secondary_ruler_time` | toggle | read, write | True while the secondary ruler is in time. Turning it on sets the secondary ruler to time, and turning it off clears the secondary ruler. |
| `state:secondary_ruler_frames` | toggle | read, write | True while the secondary ruler is in frames. Turning it on sets the secondary ruler to frames, and turning it off clears the secondary ruler. |
| `state:secondary_ruler_samples` | toggle | read, write | True while the secondary ruler is in samples. Turning it on sets the secondary ruler to samples, and turning it off clears the secondary ruler. |

## References

| Reference | Fields | Description |
| --- | --- | --- |
| `state:master_track` | [`track:`](track.md) | The master track. |
| `state:last_touched_track` | [`track:`](track.md) | The last touched track, which may be the master track, or a track that isn't on the surface. |
| `state:selected_track` | [`track:`](track.md) | The selected track, if exactly one track is selected, and it is on the surface. Otherwise it refers to nothing. |
