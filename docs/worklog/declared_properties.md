# Properties Declared on Views, and Track Anchors

Adds the declared properties of the [config model](../config_model.md)
(Declared properties, Track anchor). Today every `user:` property is global, so
a property that each strip needs gets a name generated from the strip's index:
`user:anchor_select_3`, and `user:pick_send_receive_track_3`. Afterwards a
property can be declared on a view, which gives each view its own instance
under one name, visible from that view and its descendants. Every strip
declares `user:anchor_select`, and its mappings use that name.

The track anchor becomes the first component declared on a view. The plugin's
`AddTrackAnchorMapping()` callback, and its empty strip check, move into
`scene` as `TrackAnchorProperty`.

There is no change in behavior.

## Design

### Decisions

- **Declared on a view, looked up through its ancestors.** A `user:` name in a
  view resolves to the declaration on that view, or the nearest ancestor that
  has one, and then to the scene's global properties and references. Names
  never say which view they are declared on, as the config model requires.
- **A declaration never hides another.** Declaring a name fails if it is
  already visible where it is declared (on the view, an ancestor, or globally),
  or if a declaration made earlier would see it hidden (on a descendant of the
  view, or on any view, for a global declaration). Siblings, such as the
  strips of a list, may each declare the same name. The check walks the view
  tree, which only happens while the scene is built, so no extra index of
  names has to be kept in sync.
- **A component that acts on its view adds itself.** The track anchor needs its
  view for the view's anchor slot (see below) and its track, so with a public
  constructor the caller would name the view twice, once to construct it and
  once to add it, and nothing would make them match. Instead it has a private
  constructor and a static `AddToView(view, name, config)`, which creates it and
  adds it to that view, so an anchor for one view can't be added to another.
  A property that doesn't need its view, such as the plugin's callbacks, is
  added with `View::AddUserProperty()` directly.
- **The view still holds the anchor.** A view holds at most one anchor, and
  releases it when its subject changes or it deactivates (`View::SetAnchor()`).
  The track anchor uses that slot, rather than each anchor holding its own and
  the view having to tell them all when to release. As today, holding a second
  anchor on the same strip releases the first.
- **The pick moves too.** The Send/Receive pick is still a plugin callback until
  *Modes from exclusive groups and picks* makes it a component, but it is
  declared on each strip view now, as `user:pick_send_receive_track`, so no
  `user:` name is generated from an index any more.

### Names

- **Declared**: the config model's term for a property added to a view
  (`View::AddUserProperty()`), or to the scene, which the config model calls
  the top level (`Scene::AddUserProperty()`, unchanged). It is only used in
  docs and comments. Code says "add", which in `scene` always means create,
  own, and register (`AddChildView()`, `AddTrackReference()`), so both use the
  `AddUserProperty` name, and the track anchor uses `AddToView()`.
  "Create" is left for factories that return a `unique_ptr` to their caller
  (`ViewList::Create()`).
- **Track anchor** (`TrackAnchorProperty`): the component. "Anchor" is already
  the `common` class it holds (`Anchor<Track>`), and the property is named for
  what it is, like `TrackBoolViewProperty`.
- **`user:anchor_select`**, **`user:anchor_mute`**, **`user:anchor_solo`**,
  **`user:anchor_rec_arm`**, and **`user:pick_send_receive_track`**: the names
  every strip declares, replacing the `_<n>` ones.

### scene: Declared properties (view.h/.cc, scene.h/.cc)

```
class View {
 public:
  // Adds a property to this view, in the user: namespace, which the view and
  // its descendants see by its name (see GetProperty()). Each view that adds a
  // name has its own instance, so every item of a list can add the same one.
  //
  // This returns the added property, or null (logging why) if the name is not
  // in the user: namespace or contains a '.', or if it is already visible from
  // this view (added to it, an ancestor, or the scene), or added to one of its
  // descendants. On failure the property is destroyed.
  template <typename PropertyType>
    requires std::derived_from<PropertyType, ViewProperty>
  PropertyType* AddUserProperty(std::unique_ptr<PropertyType> property);

 private:
  // Returns true if this view or any of its descendants has a user: property
  // with the name.
  bool HasUserPropertyInSubtree(std::string_view name) const;

  // The user: properties added to this view. Declared before the child views,
  // whose mappings may use them.
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>>
      user_properties_;
};
```

- `View::GetProperty()`: a `user:` name is looked up on the view and each of its
  ancestors in turn, and then in the scene. Declared names have no `.`, so a
  reference's field (`user:current_track.exists`) always goes to the scene.
- One check says whether a name is new where it is declared
  (`View::IsNewUserName()`): unused by the scene's own properties and
  references (`Scene::IsUnusedUserName()`), not found by `GetProperty()` from
  the view, and not added to any descendant. `Scene::IsNewUserName()`, which
  `Scene::AddUserProperty()` and `AddTrackReference()` use, is that check on
  the root view, so a global name can't be hidden by a view that declared it
  first.
- `view_property.h`'s namespace comment says where `user:` names resolve.
- **Performance:** nothing per run. Lookups and checks walk the view tree only
  while the scene is built, and mappings keep the property pointer.

**Brittleness:** member order. Mappings in child views hold pointers to their
ancestors' declared properties, so a view's declared properties must outlive
its child views: `user_properties_` is declared before `child_views_`, with a
comment saying why. Nothing else is paired or ordered: a declaration is checked
against everything declared before it, in both directions, whichever order the
views and declarations are added in.

### scene: Track anchor (track_anchor_property.h/.cc)

```
// A toggle for a held button that anchors a track action on its view's track
// (see TrackActions::GetAnchor()). While it is on, pressing the same action on
// another track acts on the range from this one. It is added to a view, and
// holds the anchor on the view's track, unless the view has no track.
//
// The anchor is released when the property turns off, and when the view
// releases it (see View::SetAnchor()): when the view's subject changes, when
// it is deactivated, or when it holds another anchor. It always reads as off.
class TrackAnchorProperty final : public ViewProperty {
 public:
  struct Config {
    TrackBoolProperty action = TrackBoolProperty::kSelected;

    // Modifier bits that are on while the anchor is held, if any.
    Modifiers modifier = 0;
  };

  // Creates a track anchor with the name, and adds it to the view (see
  // View::AddUserProperty()). Returns null if the name can't be added there.
  static TrackAnchorProperty* AddToView(View* view, std::string_view name,
                                        const Config& config);

 protected:
  void WriteBool(bool value) override;
};
```

- The body is today's `AddTrackAnchorMapping()` callback: on, it holds the
  scene's anchor for the action on the view's track, if the track exists; off,
  it releases the view's anchor if it is this one.
- It keeps a pointer to its view and to the anchor in the scene's
  `TrackActions`, which outlives every view.
- The mapping stays with the caller, as the button, press behavior, and
  `press_release` are the mapping's, not the component's.

**Brittleness:** none new. The constructor is private, so the anchor can only act
on the view it is declared on.

### plugin

- `AddTrackAnchorMapping()` adds a `TrackAnchorProperty` named
  `user:anchor_<action>` on the strip view, and maps it as today. It loses its
  scene and strip index parameters, and the callback and empty strip check go.
- The Send/Receive pick is declared on the strip view as
  `user:pick_send_receive_track`, with the same callback.

### To confirm

Nothing: the REAPER behavior is unchanged.

## CLs

### CL1 [x] scene: Properties declared on views

Depends on: nothing.

- `View::AddUserProperty()`, `user:` lookup through the ancestors, and the
  global check in `Scene` (see above).
- Unused, so no visible change.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).
- Temporary, removed before commit: in the plugin, declare a toggle on the track
  list view and map it on a strip, then try declaring the same name on a strip
  (fails), globally (fails), and on two sibling strips under a new name
  (succeeds). The log shows exactly the two expected errors.

### CL2 [ ] scene: Track anchor component

Depends on: CL1.

- `TrackAnchorProperty` in `track_anchor_property.h/.cc`, added to the scene's
  `CMakeLists.txt`.
- Unused, so no visible change.

**Verify**
- Standard checks.

### CL3 [ ] plugin: Declare anchors and picks on the strips

Depends on: CL2.

- `AddTrackAnchorMapping()` adds a `TrackAnchorProperty`, and the pick is
  declared on each strip (see above).
- `config_model.md`: the Track anchor's **Today** note, and the names audit row
  for the anchor and pick names. `backlog.md`: *Modes from exclusive groups and
  picks* replaces `user:pick_send_receive_track` rather than the `_<n>`
  properties.

**Verify**
- Standard checks.
- Ranges work as before, on both devices and after banking:
  - Hold select on one strip (long press), then press select on another: the
    range between them is selected.
  - Hold mute on one strip, then press mute on another: the range takes the
    held track's value. The same for solo and rec arm.
  - Holding select or mute on an empty strip anchors nothing: pressing another
    strip acts on it alone.
  - Bank while holding an anchor: the anchor is released.
- Holding Send and pressing a strip's select enters Send/Receive mode for that
  track, also while holding a select anchor.
