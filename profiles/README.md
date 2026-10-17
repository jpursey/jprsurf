# Profiles

Snapshots of `jprsurf_profile.txt`, and of the benchmark (see CLAUDE.md,
Performance), kept so a change can be compared against them without
rebuilding an older version.

- **The bar:** `idle.txt`, a profile in the user's REAPER of the scenario
  below, and `benchmark/`, everything the benchmark writes (see Benchmark
  below). They change only on purpose: after an optimization, or when a new
  cost is accepted.
- **The end of a feature** that changed per-run work or an infrequent event:
  its benchmark (see the `benchmark` skill), and an idle profile in the
  user's REAPER, compared against the bar. The user decides whether they
  replace it. Nothing is kept between a feature's CLs.

A gesture's cost in REAPER isn't profiled: it is its calls, which the call
count tests bound, weighed by what each costs in REAPER on its own (see the
`measure-reaper-costs` skill).

## Benchmark

`benchmark/summary.txt` has a line for each benchmark: its runs, JPRSurf's
self time per run, and for an action, its actions (100) and JPRSurf's time
per action, beyond the idle time of its runs. Beside it is each benchmark's
profile, of its runs alone, for finding the point behind a change. The
benchmarks are described in "Benchmarks" in `docs/testing_and_profiling.md`.

**Comparing:** run counts compare directly. On a quiet machine, the times
agree within about 3% between runs, and the larger actions within about 5%. An
action under about 2us is noise: it costs about as much as an idle run. Every
frame so far has run on performance cores (the profiles' core tables show it),
but a run made while the machine is busy, such as with another session
building, isn't comparable, and is run again.

**Replacing it:** from a run of every benchmark (no `GTEST_FILTER`) that
passed, copy `out/build/x64-Release/benchmark/` over `benchmark/`, removing any
profile of a benchmark that no longer exists. A filtered run writes a summary
of only the benchmarks it ran.

## Profiles in REAPER

**Comparing:** call counts compare directly. Times depend on the kind of core
the code ran on. On this machine (an i9-14900KF, with
8 performance and 16 efficiency cores), a frame on an efficiency core takes
about 25-30% longer. With REAPER in the foreground, Windows keeps nearly every
frame on performance cores, and times vary by about 10% between sessions; in
the background, it moves a third or more of them to efficiency cores. The
profile's core table shows where frames ran (class 1 is performance, class 0
efficiency), with a warning when they ran on both:
- **A few frames** (a few percent) on efficiency cores barely move the totals,
  and the class 1 row compares on its own.
- **More than that** makes the per run and point times read high, so the
  session is recorded again, with REAPER in the foreground throughout.

REAPER can be set to keep off efficiency cores, but that limits its use of the
machine just to profile it, so profiles are taken as REAPER normally runs.

A back to back run (a session without the change, then one with it) is only
needed when a time moves by more than 10%, or when the change is meant to make
something faster.

Each snapshot's header has the build it was made from, and the date. Points keep
the names they were recorded with: `TrackCache::RefreshFilters` was
`TrackCache::RefreshVisibility`.

## The idle scenario

It runs on the same machine each time, with other programs such as web
browsers closed. REAPER is started fresh by double-clicking the SurfaceTest
project (81 tracks, with sends and receives), so it opens at once, stays in the
foreground, and is closed (without saving) right after, so the profile covers
the scenario and nothing else. A profile starts with the surface, so time spent
choosing a project in REAPER lowers the per-run counts of everything that polls
tracks.

Leave REAPER and the surface untouched for at least 10 seconds.

Shows the steady state: the cost of every run when nothing happens. Its figures
are per run, so runs of different lengths compare; 10 seconds (about 300 runs)
is enough for a quick check.
