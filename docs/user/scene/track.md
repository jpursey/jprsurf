<!-- Generated from the scene's property catalog by the update_user_docs target. Don't edit. -->

# `track:` properties

The properties of the track a view shows, and the fields of a track reference, such as "state:master_track.volume" for "track:volume".

## Properties

| Property | Type | Access | Description |
| --- | --- | --- | --- |
| `track:name` | text | read, write | The track's name. Writing it renames the track. |
| `track:color` | color | read | The track's color. |
| `track:selected` | toggle | read, write | True while the track is selected. Writing it selects or unselects this track alone. |
| `track:mute` | toggle | read, write | True while the track is muted. Writing it mutes or unmutes this track alone. |
| `track:solo` | toggle | read, write | True while the track is soloed. Writing it solos or unsolos this track alone. |
| `track:rec_arm` | toggle | read, write | True while the track is armed for recording. Writing it arms or disarms this track alone. |
| `track:rec_mode` | enumerated | read, write | The track's record mode, which decides what it records while armed, named for REAPER's "Set track record mode to" actions: "none" records nothing, and only monitors the input. REAPER's forced input formats, which no action sets, read as "input". Writing it sets this track's mode alone, but never the master track's, which has none. Values: `input`, `output_stereo`, `none`, `output_stereo_compensated`, `midi_output`, `output_mono`, `output_mono_compensated`, `midi_overdub`, `midi_replace`, `midi_touch_replace`, `output_multichannel`, `output_multichannel_compensated`, `midi_latch_replace`. |
| `track:monitor_mode` | enumerated | read, write | The track's record monitoring: "auto" is REAPER's tape auto style. Writing it sets this track's monitoring alone. The master track's always reads as "off", and is never written. Values: `off`, `on`, `auto`. |
| `track:pan` | pan | read, write | The track's pan. Writing it pans this track alone. |
| `track:volume` | volume | read, write | The track's volume. Writing it sets this track's volume alone. |
| `track:meter` | normalized | read | The track's peak meter level. |
| `track:ui_selected` | toggle | read, write | The same as "track:selected", but writing it runs the surface's select action, as clicking the track in REAPER does, with the surface's modifiers and anchors. |
| `track:ui_mute` | toggle | read, write | The same as "track:mute", but writing it runs the surface's mute action, as clicking mute in REAPER does, with the surface's modifiers, grouping, and anchors. |
| `track:ui_solo` | toggle | read, write | The same as "track:solo", but writing it runs the surface's solo action, as clicking solo in REAPER does, with the surface's modifiers, grouping, and anchors. |
| `track:ui_rec_arm` | toggle | read, write | The same as "track:rec_arm", but writing it runs the surface's record arm action, as clicking record arm in REAPER does, with the surface's modifiers, grouping, and anchors. |
| `track:ui_rec_mode` | enumerated | read, write | The same as "track:rec_mode", but a press or a write runs the surface's action for an enumerated value, with the surface's modifiers and grouping. Its grouping reaches the tracks a grouped record arm does. Values: `input`, `output_stereo`, `none`, `output_stereo_compensated`, `midi_output`, `output_mono`, `output_mono_compensated`, `midi_overdub`, `midi_replace`, `midi_touch_replace`, `output_multichannel`, `output_multichannel_compensated`, `midi_latch_replace`. |
| `track:ui_monitor_mode` | enumerated | read, write | The same as "track:monitor_mode", but a press or a write runs the surface's action for an enumerated value, with the surface's modifiers and grouping. Its grouping reaches the selected tracks, if the track is selected, as REAPER's groups have no monitoring. Values: `off`, `on`, `auto`. |
| `track:ui_pan` | pan | read, write | The same as "track:pan", but writing it runs the surface's pan action, as panning in REAPER does, with the surface's modifiers and grouping. |
| `track:ui_volume` | volume | read, write | The same as "track:volume", but writing it runs the surface's volume action, as moving the fader in REAPER does, with the surface's modifiers and grouping. |
| `track:is_folder` | toggle | read | True if the track has child tracks on the surface. |
| `track:has_parent` | toggle | read | True if the track has a parent track, which is every track except the master track. |
| `track:exists` | toggle | read | True if there is a track, as what shows a track may show none. |
| `track:has_routes` | toggle | read | True if the track has any sends or receives. |
| `track:volume_touched` | toggle | read, write | True while anything touches the track's volume, for REAPER's Touch and Latch automation. Writing true touches it until false is written, and the touch moves with the property to another track. |
| `track:pan_touched` | toggle | read, write | True while anything touches the track's pan, for REAPER's Touch and Latch automation. Writing true touches it until false is written, and the touch moves with the property to another track. |
