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
has checked yet, and which CL checks each one. Record the findings here as they
come in, and adjust the later CLs before starting them.*

## CLs

*One library per CL where possible, in dependency order: `common`, `device`,
`scene`, then `plugin`. The status is `[ ]` not started, `[~]` in progress (set
by the CL's first edit), or `[x]` submitted (set in the CL's own commit).*

### CL1 [ ] *library*: *what it does*

Depends on: nothing.

- *What changes, by file or class.*
- *"Unused, so no visible change." if nothing uses it yet.*

**Verify**
- Standard checks (Release build, clang-format, extension loads, log has no new
  errors, smoke test).
- *Unit tests, for code that can have them.*
- *Each feature specific check in REAPER, with what should happen.*
- *Performance: the `Run()` log line's avg and max against before the change,
  and the duration of any infrequent event it adds, with a large project.*
- *Temporary, removed before commit: extra logging or a test mapping, and what
  it should show. Record the findings under To confirm.*

### CL2 [ ] *library*: *what it does*

Depends on: CL1.

- *...*

**Verify**
- Standard checks.
- *...*
