# Extension Host

Using `common` correctly takes a checklist that every extension must follow
today: forward `SetTrackListChange()` and defer `TrackCache::Refresh()` to the
next run, poll `TrackCache::RefreshVisibility()` every second, forward selection
and automation mode changes, set the last touched track, and call
`ContinuousUndo::Update()` every run. The plugin's control surface does all of
that by hand, alongside the generic `Extended()` parameter decoding.

`ControlSurface`, in `common`, takes all of it over. It is the
`IReaperControlSurface` REAPER creates, does the plumbing itself, and passes a
small set of events on to a `ControlSurfaceListener`. The plugin's surface,
renamed `PluginSurface`, is that listener, and keeps only what is specific to
JPRSurf.

REAPER also lets the user add JPRSurf more than once, and nothing defines what
happens then. `ControlSurface` now allows only one instance: a second one fails
to initialize, with an error saying why, and leaves the first untouched.

Otherwise, there is no change in behavior.

## Design

### Names

- `ControlSurface` (`common/control_surface.h/.cc`) is REAPER's own term: it is
  the `IReaperControlSurface`.
- `ControlSurfaceListener` (same files) is the interface for the events a
  surface's own logic needs.
- `PluginSurface` (`plugin/plugin_surface.h/.cc`) is the class currently named
  `ControlSurface` in `plugin`: JPRSurf's scene, devices, and modes. It
  implements `ControlSurfaceListener`.
- Both `ControlSurface` classes are in namespace `jpr`, so the plugin's class is
  renamed first, in its own CL. Adding `jpr::ControlSurface` to `common` while
  `plugin` still defines one would violate the one definition rule, which the
  linker may not catch.

### ControlSurfaceListener

```
class ControlSurfaceListener {
 public:
  virtual ~ControlSurfaceListener() = default;

  // Events, which do nothing by default.
  virtual void OnRun(absl::Time now);
  virtual void OnTracksChanged();
  virtual void OnSelectionChanged();
  virtual void OnLastTouchedTrackChanged(Track* track);

  // The config string REAPER saves in its preferences for this surface.
  virtual std::string GetConfig() const;
};
```

- It has only the events `PluginSurface` needs today, named for what happened
  rather than after REAPER's calls. More are added when something needs them.
- `OnTracksChanged()` covers both the track list and track visibility (below).
- `OnLastTouchedTrackChanged()` gets the `Track*` that `ControlSurface` looked up
  to set as the last touched track (null if `TrackCache` doesn't know it yet, as
  today).

### ControlSurface

```
class ControlSurface final : private IReaperControlSurface {
 public:
  struct Type {
    const char* type_string;  // REAPER's ID for the surface type.
    const char* description;  // Shown in REAPER's preferences.
    std::unique_ptr<ControlSurfaceListener> (*create_listener)(
        std::string_view config);
  };

  // Registers the control surface type with REAPER, so the user can add it in
  // preferences.
  static bool Register(reaper_plugin_info_t& plugin_info, const Type& type);
};
```

- `Plugin::Load()` calls `Register()` with JPRSurf's type. When REAPER creates
  the surface, it calls `create_listener` with the config string, and owns the
  listener it returns.
- The listener is destroyed first in `ControlSurface`'s destructor, so it can't
  outlive the surface or be called while half built, and there is no
  `SetListener()` for a caller to remember.
- It implements all of `IReaperControlSurface`, privately. The type and
  description come from `Type`, and the config string from the listener.
- `Extended()` decodes every call as the plugin's surface does today, including
  the `VLOG`s and the null parameter checks, and so do the other REAPER calls.
  Only the calls the listener has an event for are passed on.
- One listener per surface. More than one can be added later if something
  needs it.

### When the plumbing runs

Each piece runs at the same point relative to the listener's work as it does
today:

| REAPER call              | Plumbing                                   | Then, on the listener           |
| ------------------------ | ------------------------------------------ | ------------------------------- |
| `SetTrackListChange()`   | Mark the track list changed                | Nothing yet (see `Run()`)       |
| `SetSurfaceSelected()`   | `TrackCache::OnSelectionChanged()`         | `OnSelectionChanged()`          |
| `SetAutoMode()`          | `TrackCache::OnAutoModeChanged()`          | None                            |
| Last touched track       | `TrackCache::SetLastTouchedTrack()`        | `OnLastTouchedTrackChanged()`   |
| Anything else            | None                                       | None                            |
| `Run()`                  | See below                                  |                                 |

`Run()`, in order:
1. If the track list changed, `TrackCache::Refresh()` (logging its duration, as
   today), which also restarts the visibility poll. Otherwise, if a second has
   passed, `TrackCache::RefreshVisibility()`.
2. If either one changed anything, `OnTracksChanged()`.
3. `OnRun(now)`.
4. `ContinuousUndo::Update(now)`.
5. The `Run()` performance log line, covering the whole run, as it does today.

Events are passed on immediately rather than deferred, exactly as today. REAPER
can call them in the middle of `OnRun()` (for example, when the surface selects
a track), so a listener must still only record what they mean, and act on it
from `OnRun()`, as the plugin's surface already does with
`mode_buttons_changed_`. The last touched track is set before
`OnLastTouchedTrackChanged()` rather than after, which is safe: nothing that
`PluginSurface` does from it reads the last touched track.

**One event for the track list and visibility.** The plugin's surface does the
same thing for both (`RefreshTrackViews()` and marking the mode buttons
changed). Its extra check on a track list change, leaving Send/Receive mode if
its track was deleted, does nothing on a visibility change, as the track still
exists. So `OnTracksChanged()` covers both, and the plugin's surface keeps no
flags of its own for them (`track_list_changed_` and `last_visibility_time_`
go).

### Only one instance

`TrackCache`, `ContinuousUndo`, and the modifier state are singletons, and the
plumbing assumes it is the only thing driving them. Two instances would refresh
and update them twice, and would both try to drive the same hardware. So
`ControlSurface` allows one instance at a time:
- It keeps a pointer to the live instance, set on construction and cleared on
  destruction.
- REAPER's `create` callback refuses while one exists: it logs an error saying
  JPRSurf is already running and only one is supported, and returns null
  without touching the first instance.
- The error is also shown in REAPER's console (`ShowConsoleMsg()`), as the log
  file isn't somewhere the user would look after adding a surface.

**To confirm in CL3:**
- What REAPER does with null from `create`: whether it shows its own error,
  keeps the entry in the list, and doesn't crash.
- Whether REAPER creates a replacement before destroying the old instance when
  the user edits JPRSurf's entry in preferences (or presses OK or Apply). If it
  does, refusing would break that edit, and the check has to be different, for
  example refusing only while the other instance still has a listener that has
  run.

## CLs

### CL0 [x] plugin: rename ControlSurface to PluginSurface

Depends on: nothing.

- `plugin/control_surface.h/.cc` become `plugin/plugin_surface.h/.cc`, and the
  class becomes `PluginSurface`. Nothing else changes.
- References to the plugin's class elsewhere follow: `plugin.cc`, the
  worklogs, `config_model.md`, the backlog, and `CLAUDE.md`. The comments in
  `common` and `device` that named it no longer name any class in `plugin`, as
  lower layers shouldn't refer to it.

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors). No smoke test needed, as only names changed.
- No references to the old class name remain, other than the new `common`
  class in this plan.

### CL1 [x] common: ControlSurface and ControlSurfaceListener

Depends on: CL0.

- `common/control_surface.h/.cc`, as above (without the single instance check,
  which is CL3), added to `jpr_common_SOURCE`.
- `kVisibilityInterval`, the `Run()` performance log, `CheckParamValue()`, and
  `JPR_GET_PARAM_VALUE` are copied here from `plugin_surface.cc`, along with
  the decoding and `VLOG`s of every REAPER call. CL2 removes the originals.
- `Register()` accepts one type, which `Create()` uses for every instance.
- Unused, so no visible change.

**Verify**
- Standard checks. No smoke test needed, as nothing uses it yet.
- Covered by CL2.

### CL2 [x] plugin: PluginSurface is a ControlSurfaceListener

Depends on: CL1.

- `PluginSurface` implements `ControlSurfaceListener` (privately, as it did
  `IReaperControlSurface`): `OnRun()`, `OnTracksChanged()`,
  `OnSelectionChanged()`, `OnLastTouchedTrackChanged()`, and `GetConfig()`.
  Its constructor takes the config string.
- `PluginSurface::Register()` registers JPRSurf's type (its type string,
  description, and `Create()`) with `ControlSurface::Register()`, and
  `Plugin::Load()` calls it.
- Removed from `PluginSurface`: `GetControlSurfaceReg()`, `ShowConfig()`, all
  the `IReaperControlSurface` overrides and `Extended()` stubs,
  `type_string_`, `config_string_`, `track_list_changed_`,
  `last_visibility_time_`, the performance log members, and the direct calls
  into `TrackCache` and `ContinuousUndo` for plumbing.

**Verify**
- Standard checks, including the full smoke test.
- The `Run()` log line's avg and max are unchanged from before the change.
- Track list: add, delete, and reorder tracks, and switch project tabs. The
  strips follow, "Refreshed TrackCache" is logged once per change, and deleting
  the Send/Receive mode track goes back to Track mode.
- Visibility: hide and show a track in the mixer. The strips follow within a
  second.
- Selection: the Send/Receive mode button lights and goes out as a track with
  routes is selected and unselected.
- Automation mode: the automation lights follow a track's mode set from its
  TCP button.
- Last touched track: touching a track in REAPER scrolls the bank to it in Track
  mode, and switches the shown track in Send/Receive mode. Shift + select on the
  surface still selects a range from it.
- Continuous undo: moving a send fader on the surface still adds one undo point
  after it stops.
- Removing JPRSurf in preferences clears the X-Touch, and adding it again
  brings it back. REAPER exits cleanly, clearing the X-Touch.

### CL3 [ ] common: refuse a second instance

Depends on: CL2.

- The single instance check in `ControlSurface`, as above, adjusted for what
  the first two checks below find.
- User guide: JPRSurf can only be added once.

**Verify**
- Standard checks, including the full smoke test.
- Before writing the check, with temporary logging of `ControlSurface`
  construction and destruction: edit JPRSurf's entry in preferences and press
  OK, and press Apply with no changes. Record whether REAPER destroys the old
  instance before creating the new one. Record the findings here.
- Add a second JPRSurf in preferences. It fails with the error in the console
  and the log, the first keeps working, and REAPER doesn't crash. Record what
  REAPER shows and whether the entry stays in the list.
- Restart REAPER with both entries still listed: the first one works, and the
  second fails the same way.
- Remove the first entry, keeping the refused one, and restart REAPER: the
  remaining one now works.
