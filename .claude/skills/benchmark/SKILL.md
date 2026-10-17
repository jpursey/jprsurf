---
name: benchmark
description: Run JPRSurf's benchmarks under the fake REAPER and compare them against the bar in profiles/benchmark. Use at the end of a feature that changed per-run work (Run(), polling, cached reads) or an infrequent event, or for a CL meant to make something faster. Not for every change.
---

# Benchmark

JPRSurf's own time under the fake REAPER: the default config's steady states,
and each kind of gesture in a session at the surface, on a large project (see
"Benchmarks" in `docs/testing_and_profiling.md`). It times the machine, so it
needs a quiet one.

## Whether to run it

It is a judgment call (see Performance in CLAUDE.md):
- **Run it** once at the end of a feature that changed per-run work (`Run()`,
  what is polled, or cached reads) or an infrequent event (a track list
  refresh, a mode change), and for a CL meant to make something faster.
- **Skip it** for a feature whose risk of a regression is low, such as one
  away from the realtime path, or one whose call counts didn't move. The
  "Slow run" warnings in the user's test at the end of the feature catch an
  egregious regression. Say that it was skipped, and why.

## Running it

1. **Ask for a quiet machine** (see "A quiet machine" in CLAUDE.md): one line
   to the user, saying it is for the benchmark and takes a couple of minutes,
   with anything else timed that is due in the same window. Keep working on
   whatever doesn't depend on the result until the user says to go.
2. Run every benchmark, in the developer environment (see CLAUDE.md):

   ```
   cmake --build out/build/x64-Release --target benchmark
   ```

   `GTEST_FILTER` runs only the benchmarks it names. It prints the summary,
   and writes it and each benchmark's profile to
   `out/build/x64-Release/benchmark/`.
3. **Compare** its `summary.txt` against `profiles/benchmark/summary.txt`, the
   bar, as "Benchmark" in `profiles/README.md` says: times within about 3%
   agree (5% for the larger actions), and an action under about 2us is noise.
   For a time that moves more, or a change meant to make something faster,
   run it again back to back: without the change (a worktree at the commit
   before it), then with it, on the same quiet machine.
4. **Report** each benchmark that moved beyond the noise, with the point in
   its profile behind it.

## Replacing the bar

Only on purpose: after an optimization, or when the user accepts a new cost,
and only when the user says so. From a run of every benchmark (no
`GTEST_FILTER`) that passed, copy `out/build/x64-Release/benchmark/` over
`profiles/benchmark/`, removing the profile of any benchmark that no longer
exists.
