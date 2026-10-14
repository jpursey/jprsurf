# Defer each run's refresh

Each run defers REAPER's UI refresh to its end, so whatever a run changes in
REAPER refreshes REAPER's UI once. Before, only the track setters deferred it,
through `TrackBatch`, so a new behavior had to remember to. Now nothing has to:
faders, pans, the automation override, and more than one gesture in one run
(two faders moved at once, or a mute pressed while a fader moves) share the
run's one refresh.

Actions are the exception. An action is arbitrary code (a `cmd:` mapping can
run any action, script, or custom action), which may open REAPER's UI, such as
the Save As dialog, so every action runs outside the run's deferred refresh.

"Batch" now means only what `TrackBatch` does: changes made as one undo point.
A `PreventUIRefresh()` scope is a deferred refresh, and the code, docs, and
finished summaries were renamed to match.

## Behavior

No change the user can see. Actions, Undo and Redo, the ruler and automation
mode buttons, faders and mutes together, and the automation mode lights all
behave as before (checked in REAPER at the end of the feature).

- **REAPER's calls back come at the run's end.** For a deferrable change
  (mute, solo, rec arm, selection, volume, pan, and the override), REAPER calls
  the surface back when the run's deferred refresh ends, after the listener's
  `OnRun()`, rather than when the gesture's `TrackBatch` ended. `TrackBatch`
  keeps the track cache in line with JPRSurf's own changes, so only what a
  callback refreshes (the selection version, and the selected tracks'
  automation modes) shows a run later, about 33ms. No surface test needed to
  wait for it.
- **An action in a run pays the refresh so far first.** A run that changes
  something deferrable and then runs an action refreshes REAPER's UI twice:
  before the action, and at the run's end.
- **A mute pressed while a fader moves** lands in the same undo point as the
  fader's change, as before: REAPER holds its own undo point for a track
  volume moved from a surface, and the mute's undo point takes it in.

## Names

| Name                 | What                                                                                                 | Might be confused with                                 |
| -------------------- | ---------------------------------------------------------------------------------------------------- | ------------------------------------------------------ |
| Deferred refresh     | A `PreventUIRefresh()` scope: REAPER refreshes its UI once, when the outermost ends                  | A batch, which is only an undo point (`TrackBatch`)    |
| Deferrable           | A setter whose refresh, and calls back, a deferred refresh puts off (`CallCost`)                     | The route setters, which REAPER doesn't defer          |
| `DeferredRefresh`    | The RAII type that defers REAPER's refresh while it exists                                           | `TrackBatch`, which no longer defers anything          |
| `RunAction()`        | Runs an action outside any deferred refresh                                                          | `ViewProperty::RunAction()`, which triggers a property |
| The one refresh rule | The fake's check (formerly the `TrackBatch` rule) that several tracks change in one deferred refresh | `CallCounter`'s rule of one refresh an entry point     |

"Hold" was set aside for the deferred refresh, as button holds are everywhere
(`mod:send_hold`, `AnchorHold`), as was "block", as undo blocks
(`Undo_BeginBlock()`) are exactly the other meaning.

## Structure

### common: DeferredRefresh and RunAction (deferred_refresh.h)

```
class DeferredRefresh final {
 public:
  DeferredRefresh();   // PreventUIRefresh(1)
  ~DeferredRefresh();  // PreventUIRefresh(-1)
};

void RunAction(int command_id);
```

- `DeferredRefresh` counts how deep the deferred refreshes are, in a global,
  as REAPER has no way to ask. Nothing else in the extension may call
  `PreventUIRefresh()`, or the count is wrong.
- `RunAction()` ends every open deferred refresh in one call
  (`PreventUIRefresh(-depth)`), runs `Main_OnCommand()`, and starts them again
  (`PreventUIRefresh(depth)`). Every action the extension runs goes through it.
- `RunAction()` is re-entrant: REAPER can call back into the surface from
  inside the action, so the count is zero while the action runs, and a
  `DeferredRefresh` or action there starts from no deferred refresh at all.

### common: ControlSurface, TrackBatch, and Timeline

- **`ControlSurface::Run()`** holds a `DeferredRefresh` for the whole run, from
  before the track cache's refresh to after the listener's `OnRun()`.
- **`TrackBatch`** is a gesture's undo point, the flush of pending route
  changes before it, and keeping the cache in line. It no longer defers the
  refresh. Its undo point is added before the run's deferred refresh ends,
  which REAPER treats as outside one (`UndoContractTest`).
- **`Timeline`** runs the ruler mode actions with `RunAction()`.

### scene: Command properties

`CommandActionProperty` and `CommandToggleProperty` run their actions with
`RunAction()`, written `jpr::RunAction()`, as `ViewProperty::RunAction()`
hides it inside a property. With them, the only `Main_OnCommand()` call left
is `RunAction()`'s.

### common/testing

- **The fake** fails `Main_OnCommand()` inside a deferred refresh, so an action
  not run through `RunAction()` fails any test that reaches it in a run. The
  `TrackBatch` rule is now the one refresh rule: as each run is one deferred
  refresh, it catches changes made outside a run, by a test or by REAPER's
  calls between runs.
- **`SurfaceNotifier`** defers what REAPER defers (below): faders and pans
  (`deferred_volume_`, every track whose volume or pan changed, ganged ones
  included), and the override (every track's volume and selection). At the end
  of the outermost deferred refresh it sends each track's volume and pan, then
  its unsent mute and solo, then its selection. The override and the
  automation mode actions send a mute not yet sent, which it didn't before.
- **`CallCounter`** counts the end of an outermost deferred refresh as a
  refresh only if a deferrable setter was called in it, so an idle run's
  empty one isn't. `CallCost::batched` is `deferrable`, and faders, pans, and
  the override are now deferrable.

### REAPER, as checked

Contract tests in the test install, which the fake and notifier pass:
- **Faders and pans defer.** During `CSurf_OnVolumeChangeEx()` and
  `CSurf_OnPanChangeEx()`, REAPER calls `IsKeyDown(VK_SHIFT)` and the last
  touched track, as outside one. At the end of the outermost deferred refresh
  it sends the volume, pan, and pan mode of each track whose volume or pan was
  set in it, ganged tracks too, once, with their last values, even if they
  changed back. Outside a deferred refresh they send neither.
- **The override defers.** `SetGlobalAutomationOverride()` sends nothing
  during the call, and one round of every track's volume, pan, and selection
  at the end, however often it was set.
- **The order at the end:** each track in order, its volume and pan, then its
  mute and solo, then its selection. After a rec arm, every track's state
  comes first, leaving out the volume and pan that wait, which follow it.
- **Routes don't defer:** their calls come during the setter.
- **Undo points behave as outside one**, the extension's and REAPER's own.
- **One call ends or starts several levels:** `PreventUIRefresh(-2)` ends two
  nested deferred refreshes, and `PreventUIRefresh(2)` starts two.
- **Unrelated:** with the override at Write or Latch, a track's first volume
  change adds an envelope, which REAPER sends as a change to the track. The
  fake doesn't model it (the backlog's *Model the envelope a volume change
  writes*).

These are also in "Seen in traces" in
[testing_and_profiling.md](../testing_and_profiling.md).

## Tests

| File                                        | What                                                                                                                                      |
| ------------------------------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `deferred_refresh_test.cc`                  | Deferring and nesting; an action outside one, inside two (paid before, resumed after), and a deferred refresh and action inside an action |
| `control_surface_test.cc`                   | The listener runs in a deferred refresh that has ended after the run; the run's callbacks come after `OnRun()`                            |
| `command_properties_test.cc`                | Actions and toggles run outside a deferred refresh                                                                                        |
| `fake_reaper_test.cc`                       | An action in a deferred refresh fails                                                                                                     |
| `surface_notifier_contract_test.cc`         | REAPER's calls back in a deferred refresh: faders, pans, the override, their order, routes, and several levels in one call                |
| `fake_reaper_contract_test.cc`              | Undo points in a deferred refresh (`UndoContractTest`)                                                                                    |
| `call_counter_test.cc`, `call_cost_test.cc` | An empty deferred refresh is no refresh; faders, pans, and the override are deferrable                                                    |
| `call_count_test.cc`                        | Every bound moved: 2 cheap calls a run, and a moderate call where a fader, pan, selection, or the override is now deferred                |

## Building blocks

- **A change to REAPER in a run** needs nothing: the run defers the refresh.
  Several tracks still change in one `TrackBatch`, for one undo point.
- **A change outside a run**, such as a test calling `common` or `scene`
  directly, holds a `DeferredRefresh`, as a run would (see `TrackActionsTest`'s
  `Act()`).
- **An action** runs with `RunAction()`, never `Main_OnCommand()`.

## Decisions

- **Every action runs outside the deferred refresh,** rather than only those
  known to open UI: there is no way to know what a `cmd:` mapping's action
  does. It costs one extra refresh only in a run that changes something
  deferrable and then runs an action.
- **The depth is counted, not asked,** as REAPER has no query for it. The fake
  catches a raw `PreventUIRefresh()` that throws it off only where an action
  then runs.
- **`TrackBatch` was slimmed in this feature** rather than later, so "batch"
  means only an undo point from here on.
- **The deferred refresh opens every run,** not at the run's first change: an
  empty one costs nothing measurable.

## Performance

- **An empty deferred refresh** costs nothing measurable in REAPER: the two
  `PreventUIRefresh()` calls are 57ns to 82ns each, and every other count was
  unchanged.
- **Back to back idle profiles** (an 81-track project, a quiet machine), before
  and after: P50 39.8us both, P99 79.7us to 74.5us, average 44.2us to 43.2us.
  `ControlSurface::Run`'s own time rose 0.41us (1.90us to 2.31us, and 2.26us
  in another run), likely the profiler's hooks around the two calls.
- **Call counts:** no gesture's refreshes or undo points changed. Faders, pans,
  and the override count as deferrable, as REAPER defers what they send to a
  surface, so they share the run's refresh with anything else it changes.
