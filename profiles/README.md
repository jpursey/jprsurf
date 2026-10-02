# Profiles

Snapshots of `jprsurf_profile.txt` (see CLAUDE.md, Performance), kept so a
change can be compared against them without rebuilding an older version.

- **The bar:** `idle.txt` and `smoke.txt`, one per scenario below. They change
  only on purpose: after an optimization, or when a new cost is accepted.
- **A feature's snapshots:** `<feature>/idle.txt`, committed with each CL of a
  feature that changes per-run work (see Performance in CLAUDE.md). Each CL
  compares against the feature's last snapshot, which catches a regression in
  the CL that caused it, and against the bar.
- **The end of a feature:** an idle and a smoke snapshot of the finished
  feature, compared against the bar. The user decides whether they replace it,
  and the feature's directory is deleted either way. The smoke test is a long
  manual run, so it is only recorded here.

**Comparing:** call counts, and counts per action, compare directly. Times vary
by about 30% between sessions on the same machine, so they are a guide within
that. A back to back run (a session without the change, then one with it) is
only needed when a time moves by more than that, or when the change is meant to
make something faster.

Each snapshot's header has the build it was made from, and the date.

## Scenarios

Both run on the same machine, with REAPER started fresh, other programs such as
web browsers closed, and REAPER closed (without saving) right after, so the
profile covers the scenario and nothing else.

### idle

Open the SurfaceTest project (81 tracks, with sends and receives), and leave
REAPER and the surface untouched for at least 10 seconds.

Shows the steady state: the cost of every run when nothing happens. Its figures
are per run, so runs of different lengths compare; 10 seconds (about 300 runs)
is enough for a quick check.

### smoke

Open the SurfaceTest project, then, at a steady pace:

1. Hold mute on T2, and press mute on T9: tracks 2 to 9 are muted.
2. Hold solo on T3, and press solo on T10: tracks 3 to 10 are soloed.
3. Hold rec on T4, and press rec on T11: tracks 4 to 11 are armed.
4. Hold select on T1, and press select on T12: tracks 1 to 12 are selected.
5. Press mute, then solo, then rec on T8: mute, solo, and rec arm are off on
   every track.
6. Press select on T4: only T4 is selected.
7. Double press select on T4, to go into its folder.
8. Press select on T4.1: the Send light is lit.
9. Press Send: T4.1's sends are shown.
10. Press mute on the send to T4.7.
11. Press select on the send to T4.7: it switches to T4.7's receives, and
    T4.1's receive mute is lit.
12. Zero all four receive faders at once.
13. Zero all four receive pans at once (press the pan buttons).
14. Undo twice: the faders and pans both come back.
15. Press Track, to return to track mode.
16. Hold Send, and choose T4.4: its sends are shown.
17. Press Track.
18. Hold Send, and choose T4.9: its receives are shown (it has no sends).
19. Press Track.
20. Press Global, to go back to the top level tracks.
21. Double press select on T2, then on T2.1.1, to go in two levels.
22. Hold Global, to go back out to the top level tracks.
23. Double press select on T5, to see its 32 child tracks.
24. Press Bank right three times (it moves twice), then Bank left three times
    (it moves twice).
25. Press Channel right eight times, then Channel left until it stops.
26. Select T5.2, and press Read.
27. Select T5.3, and press Write.
28. Select T5.4, and press Touch.
29. Select T5.5, and press Latch.
30. Hold select on T5.1, and press select on T5.5: every automation button
    blinks.
31. Press Group, then each automation mode in turn.
32. In REAPER, set the global automation override to Latch Preview: Latch
    blinks.
33. Press Latch: it goes solid. Press it again: the override is off.
34. Press Group, to leave the override.
35. Press Forward three times, then Reverse three times (by measure).
36. Turn on Nudge, and do the same (by beat).
37. Turn on Marker, and do the same (by marker).
38. Turn off Marker (back to by measure).
39. Press Cycle, Click, and Solo, turning each on.
40. Press the timecode mode button through every mode, back to the first.

Shows the work behind each action: input handling, track setters and batching,
folders, banks, routes, mode changes, automation modes, transport, and undo. Its counts per action, and
JPRSurf's time per call (`Self/call`), compare between runs. Its run counts and
per run figures depend on how long the steps took, and REAPER's call times are
noisy.
