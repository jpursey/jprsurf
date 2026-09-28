# View Subjects, Lists, and References

Replaces the view's track and child context with the subjects, lists, and
references of the [config model](../config_model.md) (References, Subjects,
Lists). Today the plugin positions views itself: it sets the master fader's
track, re-points the track list when the track list changes, reveals the last
touched track, and picks the Send/Receive track and its route type. Afterwards
a view is bound to a reference or shows an item of its parent's list, and
references and lists keep themselves current. `RefreshTrackViews()`,
`EnsureTrackIsVisible()`, `SetSendReceiveTrack()`, and
`OnLastTouchedTrackChanged()` go away, along with the `View` setters they use.

The Send/Receive track becomes `user:current_track`, which follows the last
touched track in every mode, and which the track list reveals. This should look
the same as today, apart from the cases under Behavior.

## Behavior

Everything should look and work as it does today, except:
- **Re-entering Send/Receive mode for the track it last showed** keeps the route
  type and scroll position it had. Today entering always shows sends (or
  receives, if the track only has receives) from the first route. A list keeps
  its position while its view is inactive, and only resets when its track
  changes. As the Send/Receive track now follows the last touched track in
  Track mode too, this only happens when that is still the track it last
  showed. If this turns out to be unwelcome, entering the mode can reset the
  list explicitly.
- **The surface's own touches count.** The scene's track actions set the last
  touched track when a strip is pressed, which REAPER's notification doesn't
  report, so today they move nothing. Now they are followed like any other
  touch, but the touched track is on a strip already, so revealing it moves
  nothing. In Send/Receive mode only the Info strip touches a track, and that
  is already the Send/Receive track.

These are the same, but move from the plugin to the scene:
- After a track list or visibility change, lists stay in range and show the
  new tracks, and a deleted folder returns the track list to the master track.
  This happens in the scene's run, rather than in `OnTracksChanged()` before
  it, in the same run.
- A new last touched track is followed and revealed in the next run, rather
  than in REAPER's notification.

## Design

### Decisions

- **References aren't properties.** A reference is its own kind of object,
  looked up by name, whose fields are properties. Binding, following, and
  falling back take a reference, so they can't be given an ordinary property,
  and the `ViewProperty` interface (value, type, writes) isn't stretched over
  something that has no value of its own. Whether a reference refers to
  anything is its `exists` field.
- **Views notice changes, rather than being told.** A view compares its subject
  and the track list's version with the ones it last laid out for, when it
  syncs and when it becomes active, and lays out its list again if either
  changed. That costs two comparisons per active view per run, and needs no
  subscriptions, so there are no Subscribe/Unsubscribe pairs to keep. The one
  exception is navigation, which lays out the list at once, as today, so a
  strip never lags a button press.
- **The scene updates references at the start of each run,** before view
  conditions and mappings, so everything that reads one in a run sees the same
  value. Built in references react to `TrackCache` versions (below), so they
  cost nothing between REAPER events.
- **A view's subject and list are fixed when it is created** (`View::Config`),
  so the kind of every view's subject is known before any mapping is added.
  This is what lets lookups be checked: a `track:` property on a route item, a
  `route:` property on a track item, or a `view:` property that doesn't apply
  to the view is not found, and adding the mapping fails with an error.
- **A view's subject is always a reference,** or nothing. A bound view borrows
  a scene reference, a list item owns one its parent's list sets, and a
  `kParent` view uses its parent's. So `track:x` and `route:x` always mean field
  `x` of the view's subject reference, and `View` holds no `TrackProperties` or
  `RouteProperties` of its own. A `TrackReference` and a `RouteReference` are
  both a general `ViewReference`, so an FX reference later is a new class, not
  a change to lookups or binding.
- **Views don't scope names.** A view's name only identifies the view. Property
  names never contain a view path: `track:`, `route:`, and `view:` resolve
  against the view a mapping is in, and nothing can name another view's
  properties, which may not be current while that view is inactive. The
  `view:parent_*` properties are the one controlled exception: a child acting
  on its parent, which is always active while the child is.
- **The stub track stays** underneath a subject of nothing, so a view bound to
  a reference to nothing, or an item past the end of its list, has mappings
  that show nothing, as today.
- **The plugin keeps its mode state** until *Modes from exclusive groups and
  picks*. In particular, the Send/Receive mode button's light stays on
  `TrackCache::GetOnlySelectedTrack()`, because `UpdateModeButtons()` runs
  before the scene's run updates `state:selected_track`, so it would read the
  previous selection. That item replaces it with a mapping on
  `state:selected_track.has_routes`, which has no such ordering.

### Names

- **Version** (`TrackCache::GetTrackListVersion()`, `GetSelectionVersion()`):
  a number that changes whenever the track list (or visibility), or the
  selection, may have changed. Only compared for equality.
- **`ViewReference`**: a reference to a subject, or nothing, whose fields are
  properties. **`TrackReference`** is one whose subject is a track, and
  **`RouteReference`** one whose subject is a route.
- **`WritableTrackReference`**: a `TrackReference` that holds a track, which
  anything may set (navigation, revealing, and entering Send/Receive mode),
  with optional rules for changing by itself (a fallback and a reference to
  follow). Every other reference is read-only: computed from REAPER (the built
  in ones), or from what owns it (a route's other track, and list items). The
  config model calls these declared references, which are in `user:`.
- **Built in references**: `state:master_track`, `state:last_touched_track`,
  and `state:selected_track`.
- **`route:other_track`**: a field of a route reference that is itself a
  reference, to the track at the other end of the route. It replaces the route
  view's track.
- **Field**: a property of a reference's subject, named `<reference>.<name>`,
  such as `state:selected_track.has_routes` for its `track:has_routes`, or
  `route:other_track.name`. A reference's fields are its own `TrackProperties`
  or `RouteProperties`. `track:name` in a view and `<reference>.name` are the
  same object when the view's subject is that reference: the first says "this
  view's subject", so one template works on any track view, and the second
  says "that reference's track", from anywhere.
- **`.`** in a name always means a property of a named thing: the part before
  it names the thing, which is asked for the part after it. Here that is a
  reference's field, and later a component's property
  (`user:surface_mode.send_receive`). Only one level: `route:other_track.name`
  names the `other_track` reference in `route:`, and its field.
- **`SubjectKind`**: none, track, or route. **`View::SubjectSource`**: the
  parent, a reference, or an item of the parent's list.
- **List** (`View::ListConfig`, with `View::ChildTracks` or `View::Routes`),
  **list item** (a child view showing one item of its parent's list), and
  **scroll position** (the index of the item the first list item shows). These
  replace the child context, its type, and its index.
- **Length** (how many items the list has in REAPER) and **item count** (how
  many list item views show it).
- **Route type**: sends or receives (`TrackRouteType`). The config model calls
  it the list's direction, but the code and the existing
  `view:child_route_type_name` already say route type. **`View::RouteTypeRule`**
  (`kSends`, `kReceives`, `kSendsUnlessOnlyReceives`) picks it when the list's
  track changes.
- **Reveal**: scroll a list so a track is shown, moving the view's reference to
  the track's parent first if it has to.
- **Watched**: a property is watched while a flag is registered with it (by an
  active mapping or a watched condition). A reference is watched while any of
  its fields is (`TrackProperties::IsWatched()`). Route values need no
  watching, as the routes list polls them.

### Ownership, lifetime, and cost

The view tree is static once built: views and references are only ever added,
until the scene is destroyed, and reloading a config builds a new scene. What
changes at run time is only which track or route an object refers to. A
mapping looks up its property objects once, when it is added, and keeps
pointers to them. The objects stay put, and re-point and notify when their
subject changes, as a list item's `TrackProperties` does today. Here, that
means a reference's fields, which follow it to whatever it refers to.

**Lifetime:** a mapping or condition only points at objects owned by its own
view, an ancestor, or the scene, and all of those outlive it. The scene
declares its references before its root view, so views are destroyed first.

**Owners:**
- **The scene** owns the global properties, and the built in and `user:`
  references with their fields.
- **A view** owns its `view:` properties, its list's state, and if it is a
  list item, its subject reference (a `TrackReference`, or a `RouteReference`
  with its `route:other_track`).
- **Neither**: a bound view borrows the scene's reference, and a `kParent` view
  its parent's.

Since nothing is ever removed, where an object lives is for scope and cost, not
correctness. Existing costs nothing: what costs is keeping it current, and the
aim is that the cost follows what is active. There are two ways, and each suits
an owner:
- **A view's own sync** only runs while it is active, so work on what the view
  owns stops when it isn't: laying out its list, refreshing its list items'
  tracks, polling its routes, and releasing its anchor. Its state is kept while
  it is inactive, but not updated, so it must catch up when it becomes active
  again. Noticing changes by comparison does that for free, as activation runs
  the same comparison.
- **Watched flags** gate what the scene owns, as no one view decides its cost.
  An inactive view's mappings unregister their flags, so a polled state row, or
  a reference's track, is only refreshed while an active mapping or condition
  uses it.

The one cost that doesn't follow activity is state that must stay correct while
nothing shows it: a reference's value, such as `user:current_track` following
the last touched track while Send/Receive mode is inactive. That lives in the
scene, and is updated every run, so it must be event driven: a comparison of a
version or a pointer.

So, as a rule:
- **The scene** owns global names, anything shared across views, and anything
  that must stay correct whatever is active. Its per-run work is event driven,
  or gated by watched flags.
- **A view** owns what is scoped to it and only matters while it is active. It
  catches up when it becomes active.

### common: TrackCache versions

```
class TrackCache {
  // Changes whenever the track list, or any track's visibility, may have
  // changed: on every Refresh(), and on RefreshVisibility() when anything
  // changed.
  int64_t GetTrackListVersion() const;

  // Changes whenever the selection may have changed (OnSelectionChanged()).
  int64_t GetSelectionVersion() const;
};
```

- The last touched track needs no version: it is a cached pointer, so it can be
  compared directly.
- This lets anything below `plugin` react to track list and selection changes
  without the plugin forwarding them, so a scene built later (a reloaded
  config) needs no extra plumbing.
- Cost: an increment per notification, and a comparison per reader per run.

### scene: References

```
enum class SubjectKind { kNone, kTrack, kRoute };

// A reference to a subject, or nothing. It isn't a property itself, but its
// fields are: the properties of whatever subject it refers to. References in
// the scene, and a route's other track, have names. A list item's has none, as
// nothing looks it up.
class ViewReference {
 public:
  std::string_view GetName() const;
  SubjectKind GetKind() const;

  // The field with the name (such as "name" for a track's track:name), which
  // follows the reference to whatever it refers to. Null if there is no such
  // field.
  virtual ViewProperty* GetField(std::string_view name) const = 0;
};

class TrackReference : public ViewReference {
 public:
  // The track it refers to, or null.
  Track* GetTrack() const;

  ViewProperty* GetField(std::string_view name) const override;

  // Refreshes its track (and meter) from REAPER, while any field is watched.
  // Its owner calls this once per run.
  void RefreshTrack();

 protected:
  TrackReference(std::string_view name, TrackActions* actions);
  void SetTrack(Track* track);
};

class RouteReference : public ViewReference {
 public:
  // The route it refers to (see RouteProperties), and the reference to the
  // track at its other end, which is its other_track field.
  const TrackRoute* GetRoute() const;
  const TrackReference& GetOtherTrack() const;

  // Handles other_track, and other_track.<name> as the other track's fields.
  ViewProperty* GetField(std::string_view name) const override;

  // Refreshes its other track, as TrackReference::RefreshTrack().
  void RefreshTrack();

 protected:
  RouteReference(std::string_view name, TrackActions* actions);
  void SetRoute(Track* track, TrackRouteType type, int index);
};

// A track reference that holds a track, which anything may set.
class WritableTrackReference final : public TrackReference {
 public:
  struct Config {
    // A reference whose track this one starts as, and returns to if its own
    // track is deleted, or it is set to null. Without one, it starts as, and
    // returns to, nothing.
    std::string fallback;

    // A reference to follow: whenever the track it refers to changes to one
    // that exists, is on the surface, and isn't the master, this one changes
    // to it too.
    std::string follow;
  };

  // Refers to the track, or if it is null, to the fallback's track. For
  // changes from outside the reference's own rules: navigation, revealing,
  // and entering Send/Receive mode (later, picks).
  void Set(Track* track);
};

class Scene {
  // Adds a writable track reference. Returns null if the name isn't in user:,
  // is already used by a property or reference, or the fallback or follow
  // isn't a track reference.
  WritableTrackReference* AddTrackReference(
      std::string_view name, WritableTrackReference::Config config = {});

  // Returns the reference with the name, built in or added, or null.
  ViewReference* GetReference(std::string_view name) const;
};
```

- **Fields** are the reference's own `TrackProperties` or `RouteProperties`,
  which it points at its subject. `Scene::GetProperty()` splits a name at the
  first `.`, looks up the reference before it, and asks it for the field after
  it, so `state:master_track.volume` is one property object, whatever the
  master track is. A mapping on a field follows the reference with no work of
  its own. `user:` names are unique across properties and references.
- **Built in references** are created with the scene:
  - `state:master_track`: `TrackCache::GetMasterTrack()`, updated when the
    track list version changes.
  - `state:last_touched_track`: `TrackCache::GetLastTouchedTrack()`, compared
    every run.
  - `state:selected_track`: `TrackCache::GetOnlySelectedTrack()` if it exists
    and is on the surface (the scene's filter), updated when the selection or
    track list version changes. That is two REAPER calls per change, however
    many tracks REAPER reports.
- **Writable references**:
  - When the track list version changes, a reference whose track no longer
    exists returns to its fallback's track, or nothing. A hidden track is kept,
    as the track list keeps a hidden folder today.
  - Following compares the followed reference's track with the one it last
    saw, every run.
  - `Set()` takes effect at once, so a change from a mapping (navigation, and
    later picks) is seen by every other mapping in the same run.
- **Updating**: at the start of `Scene::OnRun()`, before view conditions, the
  scene updates the built in references, then the writable ones in the order
  they were added. A fallback or followed reference must exist when the
  reference is added, so it is always updated first.
- **Route references**: a `RouteReference` owns its `RouteProperties` (the
  route, by track, type, and index, and its lazily created properties), as a
  `TrackReference` owns its `TrackProperties`. The other track is a property of
  the route, so `RouteProperties` owns the `route:other_track`
  `TrackReference`, and updates it whenever the route changes (its existing
  route listeners). That lets route views use `route:other_track` (CL2) before
  their subject becomes a `RouteReference` (CL5).
- **Refreshing**: a reference refreshes its track while any of its fields is
  watched. Its owner calls `RefreshTrack()` each run: the scene for its own
  references, and a list item view for its reference, while it is active. That
  covers every view that shows a reference's track, so views bound to a
  reference don't refresh it themselves, and a field that is only used in a
  condition is still current. The scene's track actions rely on shown tracks
  being refreshed each run, which this keeps.
- **Performance**: per run, a pointer and two version comparisons for the
  built in references, a pointer comparison per follow, and a refresh per
  watched reference, which replaces the refresh views do today.

**Brittleness:** nothing is paired: references are owned by the scene (or
their view), updated by their owner in a fixed order, and nothing subscribes to
them. Everything that needs a reference looks one up as a reference, so it
can't be given a property by mistake. A reference can refer to a track that
exists but is hidden, which is deliberate (see above). `SetTrack()` and
`SetRoute()` are protected, so each owner changes its references through its
own subclass: the scene's built in ones, `WritableTrackReference::Set()`, a
route's other track, and a list's item references (private to the
list).

### scene: View config and subjects

```
class View {
 public:
  enum class SubjectSource {
    kParent,     // The parent's subject. The root has none.
    kReference,  // A reference the view is bound to.
    kListItem,   // An item of the parent's list.
  };
  enum class RouteTypeRule { kSends, kReceives, kSendsUnlessOnlyReceives };

  // A list of the child tracks of the view's track that are on the surface.
  struct ChildTracks {
    // A track reference to reveal, when the track it refers to changes while
    // the view is active, and when the view becomes active. The view must be
    // bound to a writable reference, which revealing may move.
    std::string reveal;
  };

  // A list of the sends or receives of the view's track.
  struct Routes {
    // The route type it shows whenever its track changes.
    RouteTypeRule route_type_rule = RouteTypeRule::kSendsUnlessOnlyReceives;
  };

  struct ListConfig {
    std::variant<ChildTracks, Routes> items;

    // How far view:bank_inc and view:bank_dec scroll. Defaults to the item
    // count.
    std::optional<int> bank_size;
  };

  struct Config {
    std::optional<ViewCondition::Config> condition;
    SubjectSource subject = SubjectSource::kParent;
    std::string reference;  // For kReference.
    std::optional<ListConfig> list;
  };

  // Returns null (logging why) if the name is used, the condition's property
  // doesn't exist, the reference doesn't exist, a list item's parent has no
  // list, or the list doesn't suit the subject.
  View* AddChildView(std::string_view name, Config config = {});

  // The reference holding the view's subject, or null if it has none. Its
  // kind is the view's subject kind.
  const ViewReference* GetSubject() const;

  // The view's track: its subject, or the stub if it has none or it isn't a
  // track.
  Track* GetTrack() const;
};
```

- **Kinds** are fixed when a view is created, by its source: `kParent` takes
  the parent's kind (the root has none), `kReference` the reference's, and
  `kListItem` the kind of its parent's list's items (a track for child tracks,
  and a route for routes). So a view has a subject only if it, or an ancestor
  it takes its subject from, is bound or is a list item.
- **Subjects**: a view points at the reference holding its subject: the
  scene's (`kReference`), its parent's (`kParent`), or its own (`kListItem`),
  which the view owns and its parent's list sets.
- **Lookups**: `track:x` and `route:x` are field `x` of the view's subject
  reference, if the namespace matches its kind, and otherwise not found. So a
  route item has no `track:` properties, and the route strip shows
  `route:other_track.name` rather than `track:name`.
- **Anchors**: a view releases its anchor when its subject changes. A list
  item's parent releases it when it gives the item a new subject, as today.
  Other views notice at their next sync, or when they become active.
- **Checks** on creation, each logged: the reference exists, a list item's
  parent has a list, a list's view has a track subject, a reveal is on a view
  bound to a writable reference, and the reference to reveal is a track
  reference.

**Brittleness:** the subject and list can't change after creation, so a view's
kind can't change under mappings that were checked against it, and there is
nothing to set up in the right order. The one ordering left is inherent: a list
item must be added to a parent that already has its list, which the check
catches. The list's options are a variant, so an option can't be given to a
list it doesn't apply to.

### scene: Lists

- **Items**: a view with a list keeps its list item views in their own vector,
  in the order they were added, apart from its other children. Item *i* shows
  the list's item at the scroll position plus *i*, so an item doesn't need to
  know its index. The scroll position goes up to the length minus the item
  count, and the bank size defaults to the item count. Other children aren't
  counted, and share the view's own subject (the Send/Receive Info strip could
  be one, but stays on the mode view itself for now).
- **Implementation**: the view owns a small object per kind of list, which
  lays out its items and owns that kind's `view:` properties. So
  `view:child_route_toggle` only exists on a routes list, and the other
  "only where it applies" rules below are the list's, not checks spread
  through `View`.
- **Layout**: items past the end of the list show nothing (the stub track, or
  a route with `route:exists` false), as today.
- **Keeping current**: when an active view syncs, and when it becomes active,
  it lays its list out again if either:
  - Its subject changed: a routes list picks its route type by its rule, and
    the scroll position returns to 0.
  - The track list version changed: the scroll position is clamped to the new
    length, so hiding or deleting tracks never leaves the strips blank.
- **Routes**: the view polls its track's routes each sync (`RefreshRoutes()`),
  as today, and each route item refreshes its `route:other_track`.
- **Navigation** sets the view's bound reference and the list position
  together, and lays out the list at once, so noticing the subject change
  later doesn't reset it. Each `view:` property is only found on a view where
  it applies:

  | Property                                         | Applies to                                                             | Does                                                                        |
  | ------------------------------------------------ | ---------------------------------------------------------------------- | --------------------------------------------------------------------------- |
  | `child_inc`, `child_dec`, `bank_inc`, `bank_dec` | A view with a list                                                     | Scrolls, as today                                                           |
  | `track_parent`, `track_root`                     | A view with a child tracks list, bound to a writable reference         | Moves the reference to the parent track (centering the one left), or master |
  | `parent_track_child`                             | A track item of such a view                                            | Moves the parent's reference to this track, if it has children              |
  | `parent_track_parent`, `parent_track_root`       | Any child of such a view                                               | As `track_parent` and `track_root` on the parent                            |
  | `parent_route_other_track`                       | A route item of a routes list bound to a writable reference            | Moves the parent's reference across the route, with the other route type    |
  | `child_route_toggle`, `child_route_type_name`    | A view with a routes list                                              | Toggles and names the route type, as today                                  |

- **Reveal**: the reference to reveal is fixed, but the track it refers to
  isn't. `user:current_track` changes whenever the last touched track does, or
  something sets it. The list watches that track, and reveals it when it
  changes while the view is active, and when the view becomes active. Those
  are the two places the plugin calls `EnsureTrackIsVisible()` today:
  `OnLastTouchedTrackChanged()`, and `EnterTrackMode()`. Revealing is
  `EnsureTrackIsVisible()`, moved into the child tracks list:
  - It does nothing for a track with no place in the filter (hidden, the
    master, or nothing).
  - For a track in the list's folder, it scrolls as little as it can.
  - For one in another folder, it sets the bound reference to that folder,
    scrolled so the track is the last item.
  - It works on an inactive view, which lays out when it becomes active, as
    `EnterTrackMode()` relies on today.
- **Performance**: an active view with a list compares its subject and the
  track list version each sync. Layout is only on a change, and costs what
  `RefreshTrackViews()` does today, in the scene's run rather than just before
  it, so it shows in the `Run()` log line's max rather than in the TrackCache
  refresh line.

**Brittleness:** every way a list's position changes (navigation, a subject
change from elsewhere, a track list change) goes through the view, so the
plugin can no longer leave a list out of range. A change to a reference from
outside the view (a follow, a fallback, or a write from another view's
mapping) is laid out at the view's next sync: later in the same run, or the
next one if the view already synced. That is at most one frame, and only for
changes that don't come from the view's own buttons.

### plugin

JPRSurf's surface, as in the config model:
- `user:folder`, falling back to `state:master_track`, and `user:current_track`,
  following `state:last_touched_track`.
- The master fader view is bound to `state:master_track`.
- The track list is bound to `user:folder`, lists its child tracks with a bank
  size of 8, and reveals `user:current_track`. Its strips are track items.
- The Send/Receive mode view is bound to `user:current_track`, and lists its
  routes (sends unless it only has receives), banking by its item count. Its
  route strips are route items, showing `route:other_track.name` and `.color`.

Goes away: `RefreshTrackViews()`, `EnsureTrackIsVisible()`,
`SetSendReceiveTrack()`, `OnLastTouchedTrackChanged()`, `master_track_view_`,
and the `SetChildContext()` and `SetBankSize()` calls. Entering Send/Receive
mode sets `user:current_track`, and returning to Track mode relies on the
track list revealing it when it becomes active.

Stays until *Modes from exclusive groups and picks*: `mode_`, the mode buttons
and their availability (`OnSelectionChanged()`, `IsModeAvailable()`), the
Send press handling, which calls `View::ToggleChildRouteType()`, and
`OnTracksChanged()` returning to Track mode when the Send/Receive track is
deleted. That check runs before the scene's run updates the reference, so it
checks whether the reference's track exists rather than whether it has one.

### To confirm

- **Whether REAPER reports the last touched track again when the same track is
  touched.** If it does, today's surface acts on each report: Track mode
  scrolls back to a track the user banked away from, and Send/Receive mode
  returns to it after crossing a route. References only act on a change, so
  they wouldn't. CL2 checks this with temporary logging. If REAPER does
  re-report, decide before CL7 whether to keep today's behavior, with a last
  touched version counted on every report and followed as a change.
- **Watched references stay current.** A field or bound view that is only
  shown through a reference (the master fader, the Info strip's meter and
  name, a route strip's other track) updates when the value changes in REAPER.
  CL2 checks this with a temporary mapping, and CL4 and CL5 on the surface.
- **The surface's own touches move nothing** (see Behavior). CL7 checks this
  with every strip button in Track mode, including ranges.

## CLs

### CL1 [ ] common: Track list and selection versions

Depends on: nothing.

- `TrackCache::GetTrackListVersion()` and `GetSelectionVersion()`.
- Unused, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).

### CL2 [ ] scene: References

Depends on: CL1.

- `view_reference.h/.cc`: `SubjectKind`, `ViewReference`, `TrackReference`,
  and `WritableTrackReference`.
- `TrackProperties::IsWatched()`, counted by `TrackProperty` when its first
  flag registers and its last unregisters.
- `Scene`: the built in references, `AddTrackReference()`,
  `GetReference()`, field names in `GetProperty()`, `user:` names unique
  across properties and references, and updating and refreshing references at
  the start of `OnRun()`.
- `RouteProperties`: the `route:other_track` reference and its fields, and a
  method to refresh it. `RouteProperties` takes the scene's `TrackActions` for
  the reference's fields. `RouteReference` waits for CL5, which is the first to
  need it.
- Unused, so no visible change.

**Verify**
- Standard checks.
- Temporary, removed before commit: map `state:selected_track.name`,
  `state:last_touched_track.name`, and the name of a test reference that
  follows the last touched track (falling back to the master) to scribble
  lines, and log each reference change. Check:
  - Selecting one track shows its name, and selecting none, two, or a hidden
    track shows nothing.
  - Touching tracks in REAPER, and pressing strip buttons, updates the last
    touched track, and the test reference follows it except to the master or
    a hidden track.
  - Renaming a track shown only through a reference updates the scribble.
  - Deleting the test reference's track returns it to the master.
  - Touching the same track twice: record whether REAPER reports it again
    (see To confirm).
- Performance: the `Run()` log line's avg is unchanged on a 150-track project.

### CL3 [ ] scene: View config and bound views

Depends on: CL2.

- `View::Config` and `AddChildView(name, config)`, with the condition and a
  reference to bind to (`SubjectSource::kReference`). The plugin's two
  `AddChildView()` calls with conditions change to the new signature.
- A bound view points at its reference: its `track:` properties are the
  reference's fields, and it doesn't refresh the track itself. It notices a
  subject change when it syncs or becomes active: it releases its anchor, and
  resets and lays out its child context. Unbound views keep their own
  `TrackProperties` and `RouteProperties` until CL5.
- For now, `SetTrack()` on a bound view sets a writable reference (and does
  nothing for a built in one), so the plugin's code that positions views keeps
  working. CL5 removes it.
- Unused (no view is bound yet), so no visible change.

**Verify**
- Standard checks.

### CL4 [ ] plugin: Bind views to references

Depends on: CL3.

- Add `user:folder` (falling back to `state:master_track`) and
  `user:current_track` (following nothing yet).
- Bind the master fader view to `state:master_track`, the track list to
  `user:folder`, and the Send/Receive mode view to `user:current_track`.
- Route strips show `route:other_track.name` and `.color`.
- `RefreshTrackViews()` no longer sets the master fader's track or returns a
  deleted folder to the master, and `master_track_view_` goes away.
- `OnTracksChanged()` checks the reference's track for a deleted Send/Receive
  track.

**Verify**
- Standard checks.
- The master fader follows the master volume in REAPER, and moves it.
- Delete the folder the track list is in: it returns to the master track, at
  the start of the list. Hide it instead: the strips go blank, and come back
  when it is shown.
- Delete the Send/Receive track: the surface returns to Track mode.
- In Send/Receive mode, rename and recolor a route's other track in REAPER:
  the strip follows. The Info strip's meter, name, and controls work.

### CL5 [ ] scene: Lists and list items

Depends on: CL4.

- `View::ListConfig` (`ChildTracks` or `Routes`) and
  `SubjectSource::kListItem`: the item vector, the bank size (defaulting to the
  item count), the route type rule, and layout, replacing the child context.
  Each kind of list is its own object, owning its `view:` properties.
- `RouteReference`. A list item owns its subject reference (a `TrackReference`
  or a `RouteReference`), which its parent's list sets, and `View` no longer
  has a `TrackProperties` or `RouteProperties` of its own.
- Subject kinds, and lookups that only find a view's own kind of properties,
  with `kParent` the default subject. Route items have no track.
- Lists keep current: on a subject change (route type rule, and scroll 0), and
  on a track list version change (clamp).
- Navigation sets the bound reference and the list position, and each `view:`
  property is only found where it applies.
- `View::Reveal(Track*)`, public for now, is `EnsureTrackIsVisible()`.
- Removed: `ChildContextType`, `SetChildContext()`, `ClearChildContext()`,
  `SetChildContextIndex()`, `GetChildContextIndex()`,
  `GetMaxChildContextIndex()`, `RefreshChildContext()`, `SetBankSize()`,
  `SetTrack()`, and `SetRoute()`.
- The plugin changes to match, as the API it uses is replaced: its lists and
  items are in their views' configs, `RefreshTrackViews()` goes away,
  `SetSendReceiveTrack()` only sets `user:current_track`, and
  `EnsureTrackIsVisible()` calls `Reveal()`. This is where re-entering
  Send/Receive mode for the same track first keeps its position (see
  Behavior).

**Verify**
- Standard checks.
- Every navigation button in both modes: Global (press and hold), Bank and
  Channel, a strip's double press into a folder, a route strip's select
  across a route (showing the route back), and Send toggling the route type.
- Hide and delete tracks in the shown folder, including while scrolled to the
  end: the strips stay full. Add and delete routes of the Send/Receive track
  while scrolled to the end.
- A held anchor is released by banking, and by navigating into a folder.
- Temporary, removed before commit: a mapping of a `track:` property on a
  route item, a `route:` property on a track item, and `view:track_parent` on
  the Send/Receive view each fail with an error in the log.
- Performance: on a 150-track project with sends and receives, adding,
  deleting, and hiding tracks keeps the `Run()` log line's max in the low
  milliseconds, and the avg is unchanged.

### CL6 [ ] scene: Revealing a reference

Depends on: CL5.

- `ChildTracks::reveal`: the list reveals the reference's track when it
  changes while the view is active, and when the view becomes active.
- Unused, so no visible change.

**Verify**
- Standard checks.

### CL7 [ ] plugin: Follow the last touched track, and reveal it

Depends on: CL6.

- `user:current_track` follows `state:last_touched_track`, and the track list
  reveals it.
- Remove `OnLastTouchedTrackChanged()`, `EnsureTrackIsVisible()`, and
  `SetSendReceiveTrack()`. Entering Send/Receive mode sets
  `user:current_track`, and `EnterTrackMode()` no longer reveals anything
  itself.
- `View::Reveal()` becomes private, as only the list uses it.
- Docs: the **Today** notes and plugin table in
  [config_model.md](../config_model.md), and the parts of
  [surface_modes.md](surface_modes.md) that name the removed functions.

**Verify**
- Standard checks.
- Track mode: touching a track in REAPER that is scrolled off the surface, or
  in another folder, reveals it. Touching the master or a hidden track moves
  nothing.
- Send/Receive mode: touching a track in REAPER shows its routes (sends,
  unless it only has receives), except the master or a hidden track.
- Enter Send/Receive mode by tapping Send and by Send and select, then return
  to Track mode: the Send/Receive track is revealed among its siblings,
  including after crossing a route to a track in another folder.
- Every strip button in Track mode, including ranges, moves nothing (see To
  confirm).
- Performance: the `Run()` log line's avg is unchanged.
