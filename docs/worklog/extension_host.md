# Extension Host

`ControlSurface`, in `common`, is the `IReaperControlSurface` REAPER creates for
JPRSurf. It does all the plumbing that `common` needs from REAPER's
notifications, decodes REAPER's calls, and passes a small set of events on to a
`ControlSurfaceListener`. `PluginSurface`, in `plugin`, is that listener, and
holds only what is specific to JPRSurf: its devices, scene, and modes.

Before this, the plugin's surface did the plumbing by hand, as a checklist any
extension using `common` had to follow. Now nothing that uses `common` has to
remember it.

## Behavior

The surface behaves as it did, except:
- JPRSurf can only be added once. Adding it again in Preferences >
  Control/OSC/web shows a message in REAPER's console (and the log) saying it
  is already running, and REAPER adds nothing. The first instance is left
  untouched. Editing JPRSurf's settings still works.
- The last touched track is recorded in `TrackCache` even with no X-Touch
  connected (it used to be skipped).
- `ContinuousUndo::Update()` runs after MIDI output is sent, rather than just
  before.

## REAPER facts

Found from the `ControlSurface created` and `ControlSurface destroyed` log
lines, as the SDK doesn't document them:
- Editing a surface's entry in preferences and pressing OK in its dialog
  destroys the old instance, then creates the new one, within the same
  millisecond. The X-Touch goes blank and comes back as the old instance clears
  it and the new one draws it.
- Pressing OK in preferences with nothing changed doesn't recreate the instance
  (Apply is disabled then).
- Exiting REAPER destroys the instance before the extension unloads.
- When the `create` callback returns null, REAPER doesn't add the entry to the
  list, and shows no error of its own.

## Structure

### common: ControlSurface and ControlSurfaceListener (control_surface.h/.cc)

`ControlSurfaceListener` has only the events a surface needs, each doing nothing
by default. More are added when something needs them.

| Event                         | When                                                                      |
| ----------------------------- | ------------------------------------------------------------------------- |
| `OnRun(now)`                  | Every run, after `TrackCache` is up to date                               |
| `OnTracksChanged()`           | Before `OnRun()`, when the track list or any track's visibility changed   |
| `OnSelectionChanged()`        | REAPER's `SetSurfaceSelected()`, which may come once per track            |
| `OnLastTouchedTrackChanged()` | REAPER reports a new last touched track (null if not yet in `TrackCache`) |
| `GetConfig()`                 | REAPER asks for the config string to save in its preferences              |

`ControlSurface` is `final`, and implements `IReaperControlSurface` privately:
- `Register(plugin_info, Type)` registers one surface type: its type string,
  description, and a function that creates the listener from the saved config
  string. The type and REAPER's registration struct are static members.
- `Create()` makes the listener, and the surface owns it. The destructor
  destroys the listener first, so it can't outlive the surface, and there is no
  `SetListener()` to remember. A null listener is a programming error, and
  `CHECK`s.
- Every REAPER call and `Extended()` call is decoded and `VLOG`ged as before,
  each in its own private member function, so any of them can grow real work
  later. Only the calls with a listener event are passed on.

The plumbing, and when it runs relative to the listener:

| REAPER call            | Plumbing                            | Then, on the listener         |
| ---------------------- | ----------------------------------- | ----------------------------- |
| `SetTrackListChange()` | Mark the track list changed         | Nothing yet (see `Run()`)     |
| `SetSurfaceSelected()` | `TrackCache::OnSelectionChanged()`  | `OnSelectionChanged()`        |
| `SetAutoMode()`        | `TrackCache::OnAutoModeChanged()`   | None                          |
| Last touched track     | `TrackCache::SetLastTouchedTrack()` | `OnLastTouchedTrackChanged()` |

`Run()`, in order:
1. If the track list changed, `TrackCache::Refresh()`, which also restarts the
   visibility poll. Otherwise, once a second, `TrackCache::RefreshVisibility()`
   (REAPER reports no visibility changes).
2. `OnTracksChanged()`, if either changed anything. After a refresh, the
   "Refreshed TrackCache" log line gives the duration, including the listener's
   response.
3. `OnRun(now)`.
4. `ContinuousUndo::Update(now)`.
5. The `Run()` performance log line every 5 seconds, covering the whole run.

Events are passed on as REAPER sends them, not deferred. REAPER can send them in
the middle of `OnRun()` (for example, when the surface selects a track), so a
listener only records what they mean, and acts on it from `OnRun()`.

**One instance.** `TrackCache`, `ContinuousUndo`, and the modifier state hold
REAPER state for the whole extension, so two instances would drive them (and
the same hardware) twice. The static `s_instance_` is set by the constructor
and cleared by the destructor, which also `CHECK`s there is only one.
`Create()` refuses while an instance exists, before creating the listener, as
creating a `PluginSurface` opens the MIDI ports the first one is using. The
message says to remove any extra entries, which can only come from a REAPER
config saved before the check.

**Brittleness:** while the listener is being destroyed, the surface's pointer
to it is already null, so a REAPER notification sent from inside the
listener's destructor would crash. `PluginSurface`'s destructor only
deactivates the scene and sends MIDI, which never causes one.

### plugin: PluginSurface (plugin_surface.h/.cc)

- `PluginSurface` implements `ControlSurfaceListener` privately. Its static
  `Register()` passes JPRSurf's type to `ControlSurface::Register()`, and
  `Plugin::Load()` calls it. Its static `Create()` converts the new surface to
  its private base itself, as `unique_ptr`'s converting constructor can't.
- `OnRun()` is the old `Run()` without the plumbing: mode buttons, the device,
  MIDI input, scene, and MIDI output runners, and the deferred mode and Send
  button handling.
- `OnTracksChanged()` refreshes the track views, marks the mode buttons changed,
  and leaves Send/Receive mode if its track was deleted. That check does
  nothing on a visibility change, as the track still exists, which is why one
  event covers both.
- `OnSelectionChanged()` marks the mode buttons changed.
- `OnLastTouchedTrackChanged()` scrolls the track list to the track in Track
  mode, or follows it in Send/Receive mode.

Along the way, `plugin/jprsurf.cc` became `plugin/dll_main.cc`, and comments in
`common` and `device` that named the plugin's class became layer-neutral.

## Building blocks

- **`ControlSurface` (common/control_surface.h)**: the whole REAPER control
  surface for any extension built on `common`. Register a `Type` from the
  extension's entry point, and implement `ControlSurfaceListener` for the
  surface's own logic. `common`'s state stays current without the extension
  doing anything, and only one instance can exist.

## Performance

On an 81-track project, the steady state `Run()` averages ~30us, with a max
under ~110us. A track list refresh, including `PluginSurface`'s response, takes
~140–155us. The listener adds one virtual call per run and per notification.
