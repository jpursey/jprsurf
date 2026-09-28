# Properties Declared on Views, and Track Anchors

A `user:` property can be declared on a view, as in the
[config model](../config_model.md) (Declared properties, Track anchor). Each
view that declares a name gets its own instance, visible from that view and its
descendants. Before this, every `user:` property was global, so a property that
each strip needed got a name generated from the strip's index
(`user:anchor_select_3`, `user:pick_send_receive_track_3`). Now every track
strip declares `user:anchor_select`, and its mappings use that name. No `user:`
name is generated from an index any more.

The track anchor is the first component declared on a view. The plugin's
`AddTrackAnchorMapping()` callback, and its empty strip check, moved into
`scene` as `TrackAnchorProperty`.

There is no change in behavior.

## Design decisions

- **Declared on a view, looked up through its ancestors.** A `user:` name in a
  view resolves to the property added to that view or one of its ancestors,
  and then to the scene's. Names never say which view they are declared on, as
  the config model requires.
- **A declaration never hides another.** Adding a name fails if it is already
  visible where it is added (on the view, an ancestor, or the scene), or if it
  would hide one added earlier (on a descendant, or on any view, for the
  scene). Siblings, such as the strips of a list, may each add the same name.
  The check walks the view tree, which only happens while the scene is built,
  so no index of names has to be kept in sync.
- **A property is told its view when it is added.** A component that acts on
  its view doesn't take the view in its constructor, where the caller would
  name the view twice (once to construct it, once to add it) with nothing to
  make them match. `View::AddUserProperty()` calls the property's `SetView()`
  instead, a protected virtual on `ViewProperty` that does nothing by default.
  Every property is added the same way, and the hierarchy stays flat: a
  component that needs its view overrides one function, rather than deriving
  from another base class. A first version gave the track anchor a private
  constructor and a static `AddToView()`, which this replaced.
- **A property that needs its view does nothing without one.** Nothing stops
  one being added to the scene, where it is never told a view. The track anchor
  then never anchors, which is harmless, and simpler than a compile time check.
- **The view still holds the anchor.** A view holds at most one anchor, and
  releases it when its subject changes or it deactivates (`View::SetAnchor()`).
  The track anchor uses that slot, rather than each anchor holding its own and
  the view having to tell them all when to release. Holding a second anchor on
  the same strip releases the first, as before.
- **The pick moved too.** The Send/Receive pick was still a plugin callback,
  but each strip view added it as `user:pick_send_receive_track`. It later
  became `TrackPickProperty`, as `user:pick_track`
  ([enumerated_values_and_picks.md](enumerated_values_and_picks.md)).

## Names

- **Declared**: the config model's term for a property added to a view or the
  scene (the top level). It is only used in docs and comments. Code says "add",
  which in `scene` always means create, own, and register (`AddChildView()`,
  `AddTrackReference()`), so both places use `AddUserProperty()`. "Create" is
  left for factories that return a `unique_ptr` to their caller
  (`ViewList::Create()`).
- **Track anchor** (`TrackAnchorProperty`): the component. "Anchor" is the
  `common` class it holds (`Anchor<Track>`), and the property is named for what
  it is, like `TrackBoolViewProperty`.
- **`user:anchor_select`**, **`user:anchor_mute`**, **`user:anchor_solo`**,
  **`user:anchor_rec_arm`**, and **`user:pick_send_receive_track`**: the names
  every track strip adds.

## Structure

### scene: Declared properties (view.h/.cc, scene.h/.cc, view_property.h)

- **`View::AddUserProperty(std::unique_ptr<PropertyType>)`** adds a `user:`
  property to the view, calls its `SetView()`, and returns it, or returns null
  (logging why) if the name isn't new there. The view owns it.
- **Lookups**: `View::GetProperty()` looks a `user:` name up on the view and each
  of its ancestors in turn, then in the scene. Added names have no `.`, so a
  reference's field (`user:current_track.exists`) always goes to the scene.
  `Scene::GetProperty()` never sees a view's properties.
- **One check for a new name**, `View::IsNewUserName()`:
  - `Scene::IsUnusedUserName()`: the name is in `user:`, has no `.`, and isn't
    one of the scene's own properties or references. `GetProperty()` alone
    would miss the namespace, a `.`, and reference names, which aren't
    properties.
  - `GetProperty()` from the view finds nothing, so no property on the view, an
    ancestor, or the scene has it.
  - `HasUserPropertyInSubtree()` finds nothing, so no descendant has it.
- **`Scene::IsNewUserName()`** is that check on the root view, which covers
  every view. `Scene::AddUserProperty()` and `AddTrackReference()` use it, so a
  global name can't be hidden by a view that added it first.
- **`ViewProperty::SetView(View*)`**: a protected virtual, a no-op by default,
  with `View` as a friend to call it. A property that overrides it must do
  nothing if it is never called.
- `View::ClearAnchor()` is private, as only the view uses it.

**Brittleness:** member order. Mappings in child views hold pointers to their
ancestors' properties, so a view's `user_properties_` is declared before its
`child_views_`, and outlives them, with a comment saying why. Nothing else is
paired or ordered: an added name is checked against everything added before it,
in both directions, whatever order views and properties are added in.

### scene: Track anchor (track_anchor_property.h/.cc)

- **`TrackAnchorProperty(name, {.action, .modifier})`**: a toggle, for a
  `press_release` mapping. It always reads as off.
  - On: holds the scene's anchor for the action (`TrackActions::GetAnchor()`)
    on the view's track, turning on the modifier bits while it is held, unless
    the view's track doesn't exist.
  - Off: releases the view's anchor, if it is still this one.
  - The view also releases it when its subject changes, when it deactivates, or
    when it holds another anchor (`View::SetAnchor()`).
- It overrides `SetView()` to keep its view, and does nothing without one.
- The mapping stays with the caller, as the button, press behavior, and
  `press_release` are the mapping's, not the component's.

**Brittleness:** none. The caller never names the view, and outside the
property only `View` calls `SetView()`, so the anchor only ever acts on the
view it was added to.

### plugin: PluginSurface (plugin_surface.cc)

- `AddTrackAnchorMapping(view, name, config, control, press_behavior)` adds a
  `TrackAnchorProperty` to the strip view and maps it to the control, held.
- Each track strip adds `user:anchor_select` (with `mod:select_anchor`, on a
  long press), `user:anchor_mute`, `user:anchor_solo`, and
  `user:anchor_rec_arm`, and `user:pick_send_receive_track` with the plugin's
  pick callback.

## Building blocks

- **Properties on views (`View::AddUserProperty()`):** any per-view state a
  component needs, under one name on every view that adds it. Its mappings,
  conditions, and mode overrides, and its descendants', find it by name.
- **`ViewProperty::SetView()`:** the hook for a component that acts on the view
  it is added to, such as the pick when it becomes a component.
- **`TrackAnchorProperty`:** a held button that anchors a ranged track action
  on a strip's track.

## Performance

Nothing runs per run. Lookups and name checks walk the view tree only while the
scene is built, and mappings keep the property pointer. A press of a held
anchor button does the same work as the plugin callback it replaced.
