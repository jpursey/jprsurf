# Defer each run's refresh

Each run defers REAPER's UI refresh to its end, so whatever a run changes in
REAPER refreshes REAPER's UI once. Today only the track setters defer it,
through `TrackBatch`, even for one track, so a new behavior has to remember to.
With every run deferred there is nothing to remember, and what isn't deferred
today is, if REAPER puts its refresh off: `CSurf_OnVolumeChangeEx()` and
`CSurf_OnPanChangeEx()`, the route setters, the automation override, and more
than one gesture in one run, such as two faders moved at once.

Actions are the exception. An action is arbitrary code (a `cmd:` mapping can
run any action, script, or custom action), which may open REAPER's UI, such as
the Save As dialog. Every action runs outside the run's deferred refresh,
exactly as it does today.

There is no change in behavior, apart from REAPER's calls back for a deferred
change coming at the end of the run, rather than after the gesture (see
Design).

## Design

### Names

- **Deferred refresh:** a `PreventUIRefresh()` scope, which defers REAPER's UI
  refresh to the end of the outermost one. "Batch" means only what `TrackBatch`
  does once this is done: changes made as one undo point. Today the code and
  docs call every `PreventUIRefresh()` scope a batch, as `TrackBatch` opens
  one, so they are renamed (CL1). "Hold" was set aside, as button holds are
  everywhere (`mod:send_hold`, `AnchorHold`), as was "block", as undo blocks
  (`Undo_BeginBlock()`) are exactly the other meaning.
- **Deferrable:** a setter whose refresh a deferred refresh puts off (mute,
  solo, rec arm, and selection, as far as anyone has checked).
  `CallCost::batched` becomes `deferrable`.
- **`DeferredRefresh`:** the RAII type that defers REAPER's refresh while it
  exists (alternatives: `RefreshDeferral`, `UIRefreshScope`, which says how
  rather than what).
- **`RunAction(command_id)`:** runs an action outside any deferred refresh
  (alternatives: `RunCommand()`, as REAPER's API names actions, though
  "command" is also the `cmd:` namespace; `TriggerAction()` is taken by
  `CommandActionProperty`).

### common: DeferredRefresh and RunAction (deferred_refresh.h)

```
// Defers REAPER's UI refresh while it exists: REAPER refreshes its UI once,
// when the outermost one ends, for everything changed meanwhile. Actions run
// outside it (see RunAction()).
class DeferredRefresh final {
 public:
  DeferredRefresh();
  ~DeferredRefresh();
};

// Runs REAPER's action, outside any deferred refresh: the refresh deferred so
// far is paid first, and deferring resumes after the action. Every action the
// extension runs goes through this.
void RunAction(int command_id);
```

- It is the only code that calls `PreventUIRefresh()`, so it knows how deep
  the deferred refreshes are, and `RunAction()` ends all of them around the
  action (`PreventUIRefresh(-depth)`, then `+depth`).
- Ending them mid-run pays the refresh deferred so far, and REAPER's calls back
  for it come then, as they do today. A run that changes something deferrable
  and then runs an action pays one refresh more than it would without the
  action.
- The fake fails `Main_OnCommand()` inside a deferred refresh, so an action not
  run through `RunAction()` fails every test that reaches it in a run.

### common: ControlSurface

`ControlSurface::Run()` holds a `DeferredRefresh` for the whole run, from
before the track cache's refresh to after the listener's `OnRun()`.

- **`TrackBatch`** no longer defers the refresh itself: inside a run, the run's
  does. It remains a gesture's undo point, the flush of pending route changes
  before it, and keeping the cache in line.
- **Callbacks come at the run's end.** REAPER calls a deferrable setter's
  callbacks (see "Seen in traces") when the outermost deferred refresh ends,
  which is now the end of the run (or the next action), rather than the end of
  the gesture. `TrackBatch` keeps the cache in line with JPRSurf's own changes,
  so what shows a run later (about 33ms) is only what a callback refreshes: the
  selected tracks' automation modes, and what reads the selection version (such
  as `state:selected_track`). The fake already defers these callbacks, so the
  surface tests show where it matters.
- **Performance:** two `PreventUIRefresh()` calls a run (cheap), if an empty
  deferred refresh costs nothing (To confirm). If it costs more, it opens at
  the run's first change instead, and the design is revisited before CL4.

**Brittleness:** nothing for a caller to remember for refreshes: a run defers
without anyone asking, and an action run directly fails the tests. Tests that
change several tracks outside a run (such as `track_test.cc`) hold a
`DeferredRefresh` as a run does, or the fake's rule fails them.

### common/testing: CallCounter

`CallCounter` counts the end of every outermost deferred refresh as a refresh,
empty or not, so with each run deferred every idle run would count one. It
changes to count the end as a refresh only if a deferrable setter was called in
it. Any other refresh in a deferred refresh is still a refresh of its own, as
today.

The cost table also marks `CSurf_OnVolumeChangeEx()`, `CSurf_OnPanChangeEx()`,
and `SetGlobalAutomationOverride()` `deferrable`, as REAPER defers what they
send to a surface (To confirm), so each run's deferred refresh batches them,
and the bounds show it. The route setters stay refreshes of their own, as
their calls back aren't deferred, until *Measure REAPER's costs* says
otherwise.

### To confirm

**In the test install**, as contract tests (Claude runs `check_in_reaper`):
- **REAPER's calls back inside a deferred refresh** for what has only been
  checked outside one: `CSurf_OnVolumeChangeEx()` and `CSurf_OnPanChangeEx()`,
  the route setters (`SetTrackSendUIVol()`, `SetTrackSendUIPan()`, and
  `ToggleTrackSendUIMute()`), and `SetGlobalAutomationOverride()`: whether each
  comes during the call, as outside one, or at its end. `SurfaceNotifier` sends
  them during the call either way today, which no one has checked.
- **Undo points inside a deferred refresh:** an undo point added inside one
  holds the changes made in it, and REAPER's own undo point for a track volume
  moved from a surface is held and added as it is outside one.

**Found** (2026-10-13), and now contract tests the fake passes:
- **Faders and pans defer.** During `CSurf_OnVolumeChangeEx()` and
  `CSurf_OnPanChangeEx()` REAPER calls what it does outside one
  (`IsKeyDown(VK_SHIFT)`, then the last touched track). At the end of the
  outermost deferred refresh it sends the volume, pan, and pan mode of each
  track whose volume or pan was set in it, ganged tracks too, once, with their
  last values, even if they changed back. A change made outside one isn't sent
  at a later one's end. Outside a deferred refresh they send nothing.
- **The override defers.** In a deferred refresh, `SetGlobalAutomationOverride()`
  sends nothing during the call, and one round of every track's volume, pan,
  and selection at its end, however often it was set, which also covers the
  volumes and selection changed in it.
- **The order at the end:** each track in order, its volume and pan, then its
  mute and solo, then its selection, for those that wait. After a rec arm,
  every track's state comes first, leaving out the volume and pan that wait,
  which follow it.
- **The override sends a mute not yet sent,** after the track's pan, and so do
  the automation mode actions, in or out of a deferred refresh. The notifier
  didn't, which is fixed.
- **One call ends or starts several levels:** `PreventUIRefresh(-2)` ends two
  nested deferred refreshes, sending what waited, and `PreventUIRefresh(2)`
  starts two, as `RunAction()` relies on (found in CL4).
- **Routes don't defer:** their calls come during the setter, as outside one.
- **Undo points behave as outside one**, both the extension's and REAPER's own
  for a surface volume change.
- **Unrelated:** with the override in a mode that writes automation (Write or
  Latch), each track's first volume change adds an envelope, which REAPER sends
  as a change to the track (and inside a deferred refresh, as a track list
  change). The fake doesn't model it, which is in the backlog.

What this means for the later CLs: REAPER defers what fader, pan, and override
changes send to a surface, which suggests it defers their UI refresh too, so
each run's deferred refresh batches them. The cost table could mark them
`deferrable` now (CL3), rather than waiting for *Measure REAPER's costs*.

**In the user's REAPER**, with temporary code that holds a `PreventUIRefresh()`
pair around each run:
- **What an empty deferred refresh costs.** A 10 second idle profile: the
  `PreventUIRefresh` row's self time a call, and the run times against
  `profiles/idle.txt`. A deferred refresh is paid inside `PreventUIRefresh(-1)`
  (a mute's is about 2.5ms there), so a refresh for an empty one would show in
  that row.

**Found** (2026-10-13, an 81-track project, 629 runs): an empty deferred
refresh costs nothing measurable. `PreventUIRefresh` was 2 calls a run at
66.5ns each (133ns a run), and every other count matched `profiles/idle.txt`.
Run times were about 20% above the bar, but JPRSurf's own scopes, which the
pair can't touch, were up as much (`View::SyncMappings` 8.30us to 9.92us), so
it is the session, and CL6's profile compares back to back if it still is.

## CLs

### CL1 [x] common: Rename PreventUIRefresh() scopes to deferred refreshes

Depends on: nothing.

- Code: `CallCost::batched` to `deferrable` (and `BatchedRefresh()` to
  `DeferrableRefresh()`), the fake's and `SurfaceNotifier`'s members and
  functions (`batch_depth_`, `EndBatch()`, `OnBatchedChange()`,
  `batch_rec_arm_`, `CheckOutsideBatch()`, and the rest), test names (such as
  `MuteAndSoloInABatchSendTheTracksAtItsEnd`), and comments. `TrackBatch`, and
  "batch" for what it does, stay.
- Docs: `testing_and_profiling.md`, CLAUDE.md, the backlog, and the finished
  worklogs' summaries.
- No change in behavior.

**Verify**
- Standard checks (Release build, clang-format, ctest).
- `check_in_reaper` passes (contract test names change).

### CL2 [x] common: Check deferred refreshes in REAPER

Depends on: CL1.

- Temporary: the pair around each run, deployed for the user's check above,
  and removed afterwards.
- Contract tests of REAPER's calls back inside a deferred refresh
  (`surface_notifier_contract_test.cc`), and of undo points inside one
  (`UndoContractTest` in `fake_reaper_contract_test.cc`). Where REAPER differs
  from the fake or the notifier, they are fixed, with their unit tests.
- The findings go into To confirm above, and "Seen in traces" in
  `testing_and_profiling.md`. The later CLs are adjusted to them.

**Verify**
- Standard checks.
- `check_in_reaper` passes.
- The user's idle profile, with the findings recorded.

### CL3 [x] common: Count only a deferred refresh that changed something

Depends on: CL2 (an empty deferred refresh costs nothing).

- `CallCounter` counts the end of an outermost deferred refresh as a refresh
  only if a deferrable setter was called in it.
- The cost table marks `CSurf_OnVolumeChangeEx()`, `CSurf_OnPanChangeEx()`,
  and `SetGlobalAutomationOverride()` `deferrable`.
- `call_counter_test.cc`: an empty one, and one with only a refresh that isn't
  deferrable, end with no refresh of their own. `call_cost_test.cc`: faders,
  pans, and the override are deferrable.
- No bound in `call_count_test.cc` moves: no gesture ends with an empty
  `TrackBatch`, and faders, pans, and the override aren't called in a deferred
  refresh until runs are (CL6).

**Verify**
- Standard checks.

### CL4 [x] common: DeferredRefresh and RunAction

Depends on: CL3.

- `DeferredRefresh` and `RunAction()` (`deferred_refresh.h`), with
  `deferred_refresh_test.cc`: nesting, and an action run inside one, with the
  deferred refresh paid before it and resumed after.
- `TrackBatch` holds a `DeferredRefresh` rather than calling
  `PreventUIRefresh()` itself. Its undo point is now added before the deferred
  refresh ends, rather than after, as it will be inside a run's (CL6), which
  REAPER treats as outside one (`UndoContractTest`).
- `Timeline` runs the ruler mode actions with `RunAction()`.
- The fake fails `Main_OnCommand()` inside a deferred refresh.
- No change in behavior: nothing runs an action inside a deferred refresh yet.

**Verify**
- Standard checks.

### CL5 [x] scene: Run actions with RunAction

Depends on: CL4.

- `CommandActionProperty` and `CommandToggleProperty` run their actions with
  `RunAction()`.
- No change in behavior.

**Verify**
- Standard checks.

### CL6 [ ] common: Defer each run's refresh

Depends on: CL5.

- `ControlSurface::Run()` holds a `DeferredRefresh` (see Design), and its class
  comment says so.
- `TrackBatch` no longer holds one, and its comment says that a run defers the
  refresh, and a batch is a gesture's undo point.
- `control_surface_test.cc`: the listener's `OnRun()` runs inside a deferred
  refresh, it has ended after the run, and a deferrable setter's callbacks come
  after `OnRun()`.
- Tests that change several tracks outside a run hold a `DeferredRefresh`.
  Surface tests that relied on a callback within the gesture's run wait a run.
  The bounds in `call_count_test.cc` move by the two cheap calls a run, and
  where a run's callbacks moved.
- CLAUDE.md's Performance rule on track setters, the fake's rule, and
  `testing_and_profiling.md` say that each run defers its refresh.

**Verify**
- Standard checks.
- A 10 second idle profile, against `profiles/idle.txt`: two
  `PreventUIRefresh` calls a run, and no change in run times beyond the usual
  10%.

### Summary [ ] docs: Summarize the feature

Depends on: every CL above, and the user saying the feature is done.

- When the last CL lands, ask the user whether the feature is done. Once they
  say so, replace this plan with a summary of the final implementation, and
  move anything left undone into the backlog, in the same change.

## Checks in REAPER

- Actions run from the surface behave as they did: Save in an unsaved project
  (the Save As dialog, with REAPER drawn behind it), an action that opens a
  window (such as View: Toggle mixer visible, on a spare button), Undo and
  Redo, and the ruler and automation mode buttons.
- Two faders moved at once, and a mute pressed while a fader moves, look right
  in REAPER's UI, and Undo restores them.
- The selected tracks' automation mode lights follow a selection change made
  from the surface.
- The extension loads with no new errors in the log, the smoke test passes,
  and the idle and smoke profiles are compared against the bar.
