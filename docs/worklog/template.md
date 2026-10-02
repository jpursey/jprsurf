# *Feature Name*

*What the feature does and why, in a paragraph or two, from the user's point of
view. For a change with nothing to see, say so: "There is no change in
behavior."*

## Behavior

*What the user sees: controls, lights, modifiers, and edge cases. Tables work
well for buttons and lights. Leave this section out if nothing changes.*

## Design

*How it is built, by library, in dependency order. Use the subsections that
fit, such as the ones below.*

### Names

*Each new name, and anything existing it might be confused with.*

### *Component*

```
// A sketch of the interface: the public methods, and anything a caller
// overrides.
```

- *Where it lives, and why it belongs in that library.*
- *How it behaves, and when it runs relative to the rest of `Run()`.*
- *Performance: what it costs per run, and what it pushes to infrequent
  events.*

**Brittleness:** *what a caller has to remember to get this right (paired
calls, state kept in sync, ordering), and how the design enforces it. Call out
anything that is left.*

### To confirm

*Facts about REAPER or the hardware that the design depends on but that nobody
has checked yet. They are checked in REAPER first, before the CLs that rely on
them (see Checking in REAPER in CLAUDE.md). Record the findings here, put each
into the fake as a tested fact, and adjust the later CLs before starting them.*

## CLs

*One library per CL where possible, in dependency order: `common`, `device`,
`scene`, then `plugin`. The status is `[ ]` not started, `[~]` in progress (set
by the CL's first edit), or `[x]` submitted (set in the CL's own commit).*

*If a fact under To confirm needs temporary code to check, the first CL checks
it: the temporary code (extra logging, a test mapping, or a trace), and what it
should show. It commits only the findings, and the fake's tests of them.*

### CL1 [ ] *library*: *what it does*

Depends on: nothing.

- *What changes, by file or class.*
- *"Unused, so no visible change." if nothing uses it yet.*

**Verify**
- Standard checks (Release build, clang-format, ctest).
- *Unit tests of what changes, against the fake for code that depends on
  REAPER.*
- *Performance, only for a change to per-run work: a short idle profile,
  against the feature's last snapshot and the bar. For a change to an
  infrequent event, its cost with a large project.*

### CL2 [ ] *library*: *what it does*

Depends on: CL1.

- *...*

**Verify**
- Standard checks.
- *...*

## Checks in REAPER

*What the user checks in REAPER at the end of the feature (see Checking in
REAPER in CLAUDE.md): each feature specific check, with what should happen, and
what to watch for on the hardware. Then the extension loads with no new errors
in the log, the smoke test passes, and the idle and smoke profiles are compared
against the bar.*
