---
name: measure-reaper-costs
description: Measure what a REAPER API function costs in the test install, and set its category in the cost table (call_cost.cc) the call count tests weigh calls by. Use when a function is added to the REAPER API list (reaper_api.h), or a call's cost is in question. Rare.
---

# Measure REAPER's costs

What a function on the REAPER API list costs in REAPER on its own, with the
arguments JPRSurf calls it with: the time in the call, and REAPER's later work
on its UI thread. The cost table (`GetCallCost()` in
`src/jpr/common/testing/call_cost.cc`) is set from it, and is its only record:
the results aren't kept. How it measures is in
`docs/worklog/measure_reaper_costs.md`.

## When

- **A function added to the API list**, which then doesn't compile until it
  has a cost, and fails `cost_measurements_test.cc` until it is measured or
  listed as unmeasured.
- **A cost in question**, such as a profile or "Slow run" warning that points
  at a REAPER call.

Not for a REAPER update on its own: a function's cost is unlikely to change
category. If a function already in the table measures in a different
category, suspect the environment before the table: another program busy, or
REAPER not in the foreground, so its frames ran on efficiency cores. Run it
again on a quiet machine, and only change the table if it holds, telling the
user.

## Steps

1. **Measure it under the fake first.** Add a measurement for the function in
   `src/jpr/common/testing/cost_measurements.cc`, in the form of its
   neighbors: a read across tracks, routes, or repeats, or a change in a
   deferred refresh (as the extension makes changes inside a run), with the
   arguments the extension calls it with, on the sizes where its cost may
   grow. A function that can't be measured (it opens a window, or needs the
   machine's ports) goes in `kUnmeasured`, with why. Build, and run `ctest`:
   the coverage test, and every measurement for one sample under the fake.
2. **Ask for a quiet machine** (see "A quiet machine" in CLAUDE.md): one line
   to the user, saying it is for measuring the function in the test install,
   and how long. Only its measurements take a minute or two; every
   measurement takes about 15 minutes, which is only for a reason to doubt the
   whole table. It runs the test install, so only from the main session.
3. **Run it**, in the developer environment (see CLAUDE.md), with
   `JPR_MEASURE_FILTER` naming only the new function (names separated by
   `:`), and clear it afterwards:

   ```
   $env:JPR_MEASURE_FILTER = 'SetTrackUIMute'
   cmake --build out/build/x64-Release --target measure_in_reaper
   Remove-Item Env:JPR_MEASURE_FILTER
   ```

   It prints each measurement's line: the call, the end of the deferred
   refresh it was made in, and later work, each as a median with its 10th and
   90th percentiles.
4. **Set its cost** in `call_cost.cc`, by the ranges in `CallCost`'s comments
   (`call_cost.h`), at 128 tracks: cheap (a read, under about 0.4us),
   moderate, a refresh (its cost at the end of a deferred refresh, or as
   later work), an undo point, or a command. A refresh is `deferrable` if a
   deferred refresh moves its cost to the end, and its `repeat_key_args` are
   the arguments that name what it acts on. Say how a cost grows, if it does,
   in a comment beside it.
5. **Update the call count bounds** a new category moves, in
   `src/jpr/plugin/default_config/call_count_test.cc`, as each failure's
   counts show, checking each move makes sense.
6. **Show the user** the measurement's lines with the CL, as they aren't
   kept.
