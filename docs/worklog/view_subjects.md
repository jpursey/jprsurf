# View Subjects, Lists, and References

A view's track and child context are replaced by the subjects, lists, and
references of the [config model](../config_model.md) (References, Subjects,
Lists). Before this, the plugin positioned views itself. It set the master
fader's track, re-pointed the track list when the track list changed, revealed
the last touched track, and picked the Send/Receive track and its route type.
Now a view is bound to a reference or shows an item of its parent's list, and
references and lists keep themselves current. `RefreshTrackViews()`,
`EnsureTrackIsVisible()`, `SetSendReceiveTrack()`, `OnLastTouchedTrackChanged()`,
and the `View` setters they used are gone.

The Send/Receive track is `user:current_track`, which follows the last touched
track in every mode, and which the track list reveals.

## Behavior

The surface looks and works as it did, except:
- **Re-entering Send/Receive mode for the track it last showed** keeps the route
  type and scroll position it had. Before, entering always showed sends (or
  receives, if the track only has receives) from the first route. A list keeps
  its position while its view is inactive, and only resets when its track
  changes. As `user:current_track` also follows the last touched track in Track
  mode, this only happens when that is still the track it last showed.
- **The surface's own touches count.** The scene's track actions set the last
  touched track when a strip is pressed, which REAPER doesn't report, so before
  they moved nothing. Now they are followed like any other touch, but the
  touched track is on a strip already, so revealing it moves nothing. In
  Send/Receive mode only the Info strip touches a track, and that is already
  the Send/Receive track.
- **Touching the last touched track again in REAPER moves nothing.** Before,
  Track mode scrolled back to it if the user had banked away, and Send/Receive
  mode returned to it after crossing a route. References only act on a change,
  so now another track must be touched first.

These are the same, but moved from the plugin to the scene:
- After a track list or visibility change, lists stay in range and show the
  new tracks, and a deleted folder returns the track list to the master track.
  This happens in the scene's run, rather than in `OnTracksChanged()` before
  it.
- A new last touched track is followed and revealed in the next run, rather
  than in REAPER's notification.

## REAPER facts

- REAPER reports the last touched track again when the same track is touched
  again, such as clicking an already selected track (10 of 18 reports in a
  test were the same track).
- REAPER doesn't report the last touched track when an extension acts on a
  track (the scene's track actions set `TrackCache`'s last touched track
  themselves).

## Design decisions

- **References aren't properties.** A reference is its own kind of object,
  looked up by name, whose fields are properties. Binding, following, and
  falling back take a reference, so they can't be given an ordinary property,
  and the `ViewProperty` interface (value, type, writes) isn't stretched over
  something that has no value of its own. Whether a reference refers to
  anything is its `exists` field.
- **Views notice changes, rather than being told.** A view compares its subject
  reference's version and the track list's version with the ones it last laid
  out for, when it syncs and when it becomes active, and lays out its list
  again if either changed. There are no subscriptions, so no
  Subscribe/Unsubscribe pairs to keep. Navigation is the exception: it lays out
  the list at once, so a strip never lags a button press.
- **Versions, not pointers.** Anything that watches a reference compares its
  version, which changes whenever what it refers to does. Only compared for
  equality.
- **The scene updates references at the start of each run,** before view
  conditions and mappings, so everything that reads one in a run sees the same
  value.
- **A view's subject and list are fixed when it is created** (`View::Config`),
  so the kind of every view's subject is known before any mapping is added.
  That lets lookups be checked: a `track:` property on a route item, a `route:`
  property on a track item, or a `view:` property that doesn't apply to the
  view is not found, and adding the mapping fails with an error.
- **A view's subject is always a reference, or nothing.** So `track:x` and
  `route:x` always mean field `x` of the view's subject reference, and `View`
  holds no `TrackProperties` or `RouteProperties` of its own. A new kind of
  subject (such as a track's FX) is a new `ViewReference` class, not a change to
  lookups or binding.
- **Views don't scope names.** Property names never contain a view path:
  `track:`, `route:`, and `view:` resolve against the view a mapping is in, and
  nothing can name another view's properties, which may not be current while
  that view is inactive. The `view:parent_*` properties are the one controlled
  exception: a child acting on its parent, which is always active while the
  child is.
- **The stub track stays** underneath a subject of nothing, so a view bound to a
  reference to nothing, or an item past the end of its list, has mappings that
  show nothing.

## Names

- **Version**: a number that changes whenever something may have changed, only
  compared for equality (`TrackCache::GetTrackListVersion()`,
  `GetSelectionVersion()`, and `ViewReference::GetVersion()`).
- **Reference** (`ViewReference`): a reference to a subject, or nothing, whose
  fields are properties. A `TrackReference`'s subject is a track, and a
  `RouteReference`'s a route.
- **Writable reference**: a `user:` track reference, added with
  `Scene::AddTrackReference()`, which whoever is given it may set (navigation,
  revealing, and entering Send/Receive mode), with optional rules to change by
  itself (a fallback and a reference to follow). Every other reference is only
  changed by its owner: computed from REAPER (the built in ones), or from what
  owns it (a route's other track, and list items).
- **Built in references**: `state:master_track`, `state:last_touched_track`,
  and `state:selected_track`.
- **Field**: a property of a reference's subject, named `<reference>.<name>`,
  such as `state:selected_track.has_routes` or `route:other_track.name`.
  `track:name` in a view and `<reference>.name` are the same object when the
  view's subject is that reference: the first says "this view's subject", so
  one template works on any track view, and the second says "that reference's
  track", from anywhere. A `.` in a name always means a property of a named
  thing, one level deep.
- **Subject kind** (`SubjectKind`): none, track, or route.
- **List** (`View::ListConfig`, with `View::ChildTracks` or `View::Routes`),
  **list item** (a child view showing one item of its parent's list), and
  **scroll position** (the index of the item the first list item shows).
  **Length** is how many items the list has in REAPER, and **item count** how
  many list items show it.
- **Route type**: sends or receives (`TrackRouteType`). The config model calls
  it the list's direction. `View::RouteTypeRule` (`kSends`, `kReceives`,
  `kSendsUnlessOnlyReceives`) picks it when the list's track changes.
- **Reveal**: scroll a list so a track is shown, moving the view's reference to
  the track's parent first if it has to.

## Structure

### common: TrackCache versions (track_cache.h)

- `GetTrackListVersion()` changes on every `Refresh()`, and on
  `RefreshVisibility()` when anything changed. `GetSelectionVersion()` changes
  on `OnSelectionChanged()`.
- These let anything below `plugin` react to track list and selection changes
  without the plugin forwarding them, so a scene built later (a reloaded
  config) needs no extra plumbing. The last touched track needs no version: it
  is a cached pointer, compared directly.

### scene: References (view_reference.h, track_reference.h/.cc, route_reference.h/.cc)

- **`ViewReference`**: `GetName()`, `GetKind()`, `GetVersion()`,
  `GetField(name)` (the field with the name, without its namespace, which
  follows the reference to whatever it refers to), and `Update()`, which its
  owner calls once per run while anything may show it.
- **`TrackReference`** owns a `TrackProperties`, which it points at its track
  (the stub track for nothing).
  - `Set(track)` takes effect at once, so a change from a mapping is seen by
    every other mapping in the same run. Null or a deleted track refers to the
    fallback's track, or nothing.
  - `Config::fallback`: a reference whose track it starts as, and returns to
    whenever it refers to nothing or its track is deleted. A hidden track is
    kept.
  - `Config::follow`: a reference to follow. Whenever that reference's version
    changes to a track on the surface (it exists, isn't the master, and the
    scene's filter includes it), this one is set to it too.
  - `Update()` applies the follow, then the fallback, then refreshes the track
    from REAPER while any field is watched, and its meter while the meter field
    is (`TrackProperties::IsWatched()` and `IsMeterWatched()`).
- **`RouteReference`** owns a `RouteProperties`: `Set(track, type, index)`,
  `GetRoute()`, and fields that include `other_track.<name>`, the track at the
  other end of the route, itself a `TrackReference` that `RouteProperties` owns
  and updates whenever the route changes.
- **`Scene`**:
  - Creates the built in references. `state:master_track` is set when the
    track list version changes, `state:last_touched_track` is compared every
    run, and `state:selected_track` is the only selected track if it is on the
    surface, set when the selection or track list version changes.
  - `AddTrackReference(name, config)` adds a writable reference. It fails if
    the name isn't in `user:`, contains a `.`, or is used by a property or
    reference, or if the fallback or follow isn't a track reference.
  - `GetReference(name)` returns const access to any reference. Only `View` can
    get non-const access to a writable one (`GetWritableTrackReference()`), for
    the list of a view bound to it.
  - `GetProperty()` splits a name at its first `.` to find a reference's field,
    so `state:master_track.volume` is one property object, whatever the master
    track is.
  - At the start of `OnRun()`, it sets the built in references, then updates
    every reference in the order they were added. A reference's fallback and
    follow must exist when it is added, so they are always updated first.

**Brittleness:** nothing is paired. References are owned by the scene (or their
view), updated by their owner in a fixed order, and nothing subscribes to them.
Changing a reference takes non-const access, which only its owner has and gives
out only on purpose.

### scene: View config and subjects (view.h/.cc)

- **`View::Config`**, given to `AddChildView(name, config)`:
  - `condition`: the view is only active while it is met.
  - `subject`: `ParentSubject` (the default: the parent's subject, and the root
    has none), `ReferenceSubject{name}` (bound to a scene reference), or
    `ListItemSubject` (an item of the parent's list, whose reference the view
    owns and the list sets).
  - `list`: a `ListConfig` of `ChildTracks` or `Routes`, and an optional bank
    size.
- `AddChildView()` returns null, logging why, if the name is used, the
  reference doesn't exist, a list item's parent has no list, a list is given
  to a view without a track subject, a list's reveal isn't valid, or the
  condition's property doesn't exist.
- `GetSubject()` returns the reference holding the view's subject, or null.
  `GetTrack()` returns its track, or the stub track. `GetProperty()` is public,
  so the plugin can run a `view:` action.
- **Lookups**: `track:x` and `route:x` are field `x` of the view's subject, if
  the namespace matches its kind, and otherwise not found.
- **Anchors**: a view releases its anchor when its subject changes: a list
  item at once when its list gives it a new subject, and any other view at its
  next sync, or when it becomes active.

**Brittleness:** the subject and list can't change after creation, so a view's
kind can't change under mappings that were checked against it. The one ordering
left is inherent: a list item must be added to a parent that already has its
list, which is checked. The subject and the list's options are variants, so a
reference name can't be given to a view that isn't bound, or an option to a
list it doesn't apply to.

### scene: Lists (view_list.h/.cc)

- **`ViewList`**, internal to `View`, is created from the view's `ListConfig`,
  with the view's writable reference if it is bound to one. It lays out the
  view's list items, and adds the `view:` properties that act on the list, to
  the view and to each item as it is added. Its kinds, `ChildTrackList` and
  `RouteList`, are private to `view_list.cc`.
- **Items**: a list's items are kept apart from the view's other children, in
  the order they were added. Item *i* shows the list's item at the scroll
  position plus *i*, and items past the end show nothing (the stub track, or a
  route with `route:exists` false). The scroll position goes up to the length
  minus the item count.
- **Keeping current** (`ViewList::Update()`): when the view syncs, becomes
  active, or before a list action runs, the list lays out again if:
  - Its subject changed: a routes list picks its route type by its rule, and
    the scroll position returns to 0.
  - The track list version changed: the scroll position is clamped to the new
    length, so hiding or deleting tracks never leaves the strips blank.
- **Per run**, while the view is active: each list item updates its reference,
  and a routes list polls its track's routes (`Track::RefreshRoutes()`), as
  REAPER doesn't reliably report route changes.
- **Navigation** sets the bound reference and the scroll position together, and
  lays out at once, so noticing the subject change later doesn't reset it.
  Each `view:` property only exists where it applies:

  | Property                                         | Applies to                                                     | Does                                                                        |
  | ------------------------------------------------ | -------------------------------------------------------------- | --------------------------------------------------------------------------- |
  | `child_inc`, `child_dec`, `bank_inc`, `bank_dec` | A view with a list                                             | Scrolls by one, or by the bank size (defaulting to the item count)          |
  | `track_parent`, `track_root`                     | A view with a child tracks list, bound to a writable reference | Moves the reference to the parent track (centering the one left), or master |
  | `parent_track_child`                             | A track item of such a view                                    | Moves the parent's reference to this track, if it has children              |
  | `parent_track_parent`, `parent_track_root`       | A track item of such a view                                    | As `track_parent` and `track_root` on the parent                            |
  | `parent_route_other_track`                       | A route item of a routes list bound to a writable reference    | Moves the parent's reference across the route, with the other route type    |
  | `child_route_toggle`, `child_route_type_name`    | A view with a routes list                                      | Toggles and names the route type                                            |

- **Reveal** (`ChildTracks::reveal`): the name of a track reference, whose
  track the list reveals when it changes while the view is active (compared by
  version in `Sync()`), and when the view becomes active (`OnActivated()`).
  The view must be bound to a writable reference other than the one it
  reveals.
  - A track with no place in the filter (hidden, the master, or nothing) is
    ignored.
  - For a track in the list's folder, it scrolls as little as it can.
  - For one in another folder, it sets the bound reference to that folder,
    scrolled so the track is the last item.

**Brittleness:** every way a list's position changes (navigation, a subject
change, a track list change) goes through the list, so nothing can leave a list
out of range. A change to a reference from outside the view (a follow, a
fallback, or another view's mapping) is laid out at the view's next sync, at
most one frame later.

### plugin: PluginSurface (plugin_surface.cc)

- `user:folder` falls back to `state:master_track`, and `user:current_track`
  follows `state:last_touched_track`.
- The master fader view is bound to `state:master_track`.
- The track list is bound to `user:folder`, lists its child tracks with a bank
  size of 8, and reveals `user:current_track`. Its strips are track items.
- The Send/Receive mode view is bound to `user:current_track`, and lists its
  routes (sends unless it only has receives), banking by its item count. Its
  route strips are route items, showing `route:other_track.name` and `.color`.
- Entering Send/Receive mode sets `user:current_track`, and returning to Track
  mode relies on the track list revealing it when it becomes active. The Send
  tap runs the Send/Receive view's `view:child_route_toggle`.
- `OnTracksChanged()` returns to Track mode when the Send/Receive track is
  deleted. It runs before the scene's run updates the reference, so it checks
  whether the reference's track exists rather than whether it has one.

## Building blocks

- **Track references (scene/track_reference.h):** a named, writable track with
  a fallback and a follow, added with `Scene::AddTrackReference()`. Anything
  that should point at "the track the user is working with" is one, and views
  bind to it.
- **Fields (`<reference>.<name>`):** any reference's track or route properties,
  from anywhere, such as `state:selected_track.has_routes`.
- **View configs (scene/view.h):** a view's subject and list in one struct,
  checked when the view is added. Strips are list items, and navigation, bank
  size, the route type rule, and reveal are list options, not plugin code.
- **Versions:** `TrackCache`'s track list and selection versions, and every
  reference's version, for anything that needs to notice a change by
  comparison.

## Performance

- Per run: a pointer and two version comparisons for the built in references,
  a version comparison per follow and per list, and a refresh per watched
  reference, which replaced the refresh views did before.
- Layout only happens on a change, in the scene's run, so it shows in the
  `Run()` log line's max. On an 81-track project, a mode change takes ~90µs,
  and a track list refresh ~115–140µs.
- The steady state `Run()` average is unchanged, at ~30–43µs.
