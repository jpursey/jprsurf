# JPRSurf

This is a C++ control surface extension for the REAPER DAW. It handles bi-directional control between physical hardware and a running instance of REAPER. REAPER is a realtime, low latency, application processing audio, so performance and reliability of the extension are critical.

This extension depends only on the Reaper SDK (location defined by the REAPER_EXTENSION_SDK environment variable) and the Game Bits shared C++ library (location defined by the GB_DIR environment variable) which must be present in the environment and on the machine. It currently only works on Windows, and is built with Visual Studio 2022 Community and CMake.

## Workflow

@E:/Projects/game-bits/docs/workflow.md

The workflow above is shared with Game Bits and every project built on it, and lives in Game Bits as `docs/workflow.md`. Imports can't read environment variables, so it is imported by absolute path. Game Bits is always checked out beside this project, so the path is the directory holding both repositories (`E:/Projects` today), followed by `game-bits/docs/workflow.md`, which is also `$GB_DIR/docs/workflow.md`. If the drive or machine changes, update the path to match. Everything below is specific to JPRSurf.

## Directory Structure

This is a CMake project, starting at the root. The directory structure is as follows:
```
  src/             -- All source code in this project
    jpr/           -- All source code for the extension itself, separated into different libraries (see below)
  docs/            -- User guide, surface config model, feature plans (worklog/),
                      and the backlog
  profiles/        -- Profile snapshots to compare performance against (see
                      Performance)
  bin/             -- Compiled binary files used by compilation or execution
  out/             -- Generated output from building locally. This is transient
                      and can get deleted at any time.
```

Libraries depend strictly in the order `common` → `device` → `scene` → `plugin` (each may use only those before it):
- `common`: Common types and general utilities for working within the REAPER SDK and managing generic REAPER state (MIDI ports, project tracks, etc). Not specific to JPRSurf; it could be useful for any REAPER extension.
- `device`: An interface and concrete implementations for the hardware devices JPRSurf supports. Handles all communication with the device and tracks its current state, with no knowledge of REAPER behavior. Primary classes: `Device` (the generic interface for a hardware device) and `Control` (a logical control on that device that may be read and/or written to).
- `scene`: Defines how device controls may be mapped to REAPER actions and properties. It knows REAPER properties and abstractions, but does not hard code mappings; they must be configured. Primary classes: `Scene` (the top level mapping between one or more devices and REAPER state) and `View` (a hierarchical set of mappings that may be enabled or disabled independently by configuration or explicit application control).
- `plugin`: The entry point for the REAPER extension, which configures the Control Surface Integration (CSI) for the connected devices. Primary class: `PluginSurface`, the `ControlSurfaceListener` (see `common/control_surface.h`) that defines the actual mappings and business logic.

## Commands

Everything is driven directly by CMake using the Ninja generator, which is exactly what Visual Studio's "open a local folder" CMake integration does (see CMakeSettings.json). Command line builds, IDE builds, and CI all use the same build.

### Developer environment (once per shell)

CMake and Ninja need an x64 MSVC developer environment; nothing below works without it.

```
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation
```

### Configure

These match the `x64-Debug` and `x64-Release` configurations in CMakeSettings.json, so the IDE picks up whatever the command line configures and vice versa. Note that Visual Studio's "x64-Release" is `RelWithDebInfo`, not `Release`.

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B out/build/x64-Debug
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B out/build/x64-Release
```

If configure or build fails with a missing `cl.exe`, `rc.exe`, or Windows SDK path, the tree's cache is left over from an older Visual Studio or Windows SDK version. Delete the build tree and configure again (in the IDE this is "Delete Cache and Reconfigure").

### Build

```
cmake --build out/build/x64-Release
```

Release (`RelWithDebInfo`) is the default build. REAPER is very latency sensitive, so the extension should always be run and tested optimized. Only build `out/build/x64-Debug` when you specifically need to step through the code in a debugger.

The build also will copy the binary to the REAPER plugin directory so it can be run immediately. This is controlled by the `JPR_DEPLOY_TO_REAPER` CMake option, which defaults to ON in the main checkout and OFF in a git worktree (see Parallel sessions below).

A running REAPER locks the extension DLL, so the final copy into the REAPER plugin directory fails if REAPER is open. Assume REAPER is closed and build without asking. Treat a copy or permission failure at the end of a build as "REAPER is open": ask the user to close it and build again, rather than debugging the build.

### Test

```
ctest --test-dir out/build/x64-Release
```

Runs every library's unit tests, after a build.

### Testing and Logging

Code is unit tested (see Build system), including code that depends on REAPER, which is tested against `FakeReaper` (see `src/jpr/common/testing/fake_reaper.h`): REAPER's state in memory, loaded as the REAPER API. A change is verified by its tests, not in REAPER, so most work can be done in a side session (see Parallel sessions). The user runs REAPER only for the reasons in Checking in REAPER below.

Logs from LOG statements are written to "C:\\Users\\johnp\\AppData\\Roaming\\jprsurf.log" and are cleared and rewritten each time REAPER is run and/or loads the extension. Additional debugging information can be added there to debug what is going on. However, LOGs should be minimized outside of debugging use cases as they affect performance and diskspace. LOGs for particular infrequent events may be retained as is helpful for persistent understanding of code flow (continuous controller and UI events generally do *not* fall into this category).

To see exactly what REAPER does, set the `JPRSURF_TRACE` environment variable (to anything but 0) for REAPER's process. Every call between JPRSurf and REAPER, in both directions, is then written to "C:\\Users\\johnp\\AppData\\Roaming\\jprsurf_trace.txt", replaced on each load, except runs that only read state (see `ReaperTrace` in `src/jpr/common/reaper_trace.h`). It is far too slow to leave on.

Every change is checked as follows:
- It builds cleanly in Release (`out/build/x64-Release`).
- Touched files pass `clang-format --dry-run -Werror`.
- `ctest` passes, with tests of what the change does.
- Any feature specific checks for the change (see Feature workflow below).

#### Checking in REAPER

The user runs REAPER, with the extension deployed to it, only for these:
1. **Behavior nobody has checked.** What REAPER or the hardware does, where code will rely on it, is checked before that code is written, with temporary code that is removed afterwards: extra logging, a test mapping on a spare button, or a trace. As much as possible, this is a step at the start of a feature, from its plan's To confirm (see Feature workflow). A fact that turns up partway through stops the work until it is checked. Each finding is recorded in the plan's To confirm, and goes into the fake as a tested fact (and the calls REAPER makes go in "Seen in traces" in `docs/testing_and_profiling.md`), so it is never checked twice. When REAPER shows the fake is wrong, fix the fake first, with a test, then the code.
2. **Performance**, for the changes Performance below names.
3. **The end of a feature.** Before a feature is done, the user tests it in REAPER:
   - REAPER loads the extension, and `jprsurf.log` has no new errors.
   - The feature's own checks, and how it feels on the hardware.
   - **Smoke test:** existing behavior still works: faders, pots, pot buttons, mute, solo, rec arm, select (press, double press, long press), folder navigation, bank/channel navigation, Global, master fader, transport, timecode, meters, scribble names and colors, and mode buttons. As *Surface tests* and the items after it in `docs/backlog.md` make it tests, this shrinks to what the fakes can't show.

   A bug found then is fixed in a follow-up CL, after fixing the gap in the fake that let it through.

#### Performance

REAPER is realtime and the extension runs on its UI thread, so performance is checked by running REAPER and reading the profile it writes on exit, "C:\\Users\\johnp\\AppData\\Roaming\\jprsurf_profile.txt" (see `ReaperProfiler` in `src/jpr/common/reaper_profiler.h`). It times every run, every REAPER call, and the scopes and counters JPRSurf defines with Game Bits' `gb/profile`, and is replaced each time REAPER exits or the surface is removed.
- **Steady state and actions:** the p50 and p99 run times, and the counts and times of the points the change touches, don't regress. `profiles/` holds snapshots to compare against, of REAPER idle and of a scripted smoke test (see `profiles/README.md` for the scenarios, and when the snapshots are replaced). A CL that changes per-run work (`Run()`, what is polled, or cached reads) records a short idle run (10 seconds is enough), and compares it against the feature's last snapshot and the bar; a test-only or docs CL, or one away from the realtime path, records none. The smoke test is only profiled at the end of a feature. Counts compare directly. Run times vary by about 30% between sessions on the same machine, so a back to back run (without the change, then with it) is only needed when a time moves by more than that, or when the change is meant to make something faster.
- **Infrequent events** (track list refresh, mode changes, and the like): no single event exceeds the low milliseconds. A frame is ~33ms, shared with REAPER's own UI work. A run over 8ms logs a "Slow run" warning with its breakdown by point. A CL that changes one is checked with a large project (100+ tracks, with sends and receives).
- **The profiler's own cost** stays within its budget, the larger of 3us a run (about twice what it costs) and 1% of the run. The "Profile:" line logged on exit shows it, and is a warning if it is over.

New work whose cost matters gets a scope (`gb::ProfileScope`), and work that scales gets a counter (`gb::ProfileCount`), named for the function it times (see Names in `docs/worklog/profiler.md`).

Keep per-run work to cheap cached reads, and push expensive REAPER queries to the events that can change their results (for example `SetTrackListChange()`).

REAPER track setters (mute, solo, rec arm, select) each pay a UI refresh of ~2–17ms per call. Any action that changes a UI-visible property on more than one track must batch the changes in a single `PreventUIRefresh` scope (see `TrackBatch` in `src/jpr/common/track.h`), so the refresh is paid once.

### Format

```
clang-format -i <files>              # format in place
clang-format --dry-run -Werror <files>   # check only
```

Style comes from `src/.clang-format` (Google style); clang-format finds it automatically for any file under `src/`. Only format files you actually touch.

## Build system

- Each library is defined by a `CMakeLists.txt` in its own directory using the `gb_add_library` / `gb_add_shared_library` commands from Game Bits.
- New source and test files must be added to their module's `CMakeLists.txt` (`<target>_SOURCE`) or they will not be compiled.
- Defining `<target>_TEST_SOURCE` automatically creates a `<target>_test` executable that links GoogleTest/GoogleMock and registers a ctest test of the same name.
- `<target>_DEPS` is for other CMake targets in the build (including `absl::*`); `<target>_LIBS` is for prebuilt external libraries.

## Conventions

These add to the C++ style in the workflow.
- Formatting strictly driven by clang-format in Google style via src/.clang-format
- All extension code is in the "jpr" namespace, except for required C extension points of the REAPER SDK.
- Every file starts with the four line MIT copyright comment used everywhere in the tree, with the year the file was created.
- Headers use `#pragma once` include guards (not `#ifndef` include guards).
- Include order: the file's own header first, then C/C++ standard headers in angle brackets, then third-party, Game Bits, and JPRSurf headers in quotes (`"absl/..."`, `"gb/..."`, `"jpr/..."`), with blank lines between groups.
- REAPER API functions come from `jpr/common/reaper_api.h`, never from the SDK's `reaper_plugin_functions.h` directly (`sdk/reaper_plugin.h`, which has only types, is fine). It declares only the functions JPRSurf calls, so calling a new one means adding it to both of its lists: a `REAPERAPI_WANT_` line and an entry in `JPR_REAPER_API`.
- Game Bits' libraries are in `$GB_DIR/src/gb/` (such as `gb/base` and `gb/container`), and the third-party libraries it vendors are in `$GB_DIR/third_party/`.
- C++20, built with both MSVC and clang-cl.

## Feature workflow

JPRSurf is moving toward a surface that is entirely driven by a config file, described in `docs/config_model.md`. So that features move toward that goal rather than away from it, a feature is a named, parameterized component below `plugin`, plus its use in JPRSurf's own surface (today the mappings in `PluginSurface`, later its config). `plugin` gains no new state or callbacks: if a feature seems to need one, the missing piece is a component.

The `scene` layer aims to be flexible and composable, not just simple: few concepts, each as small as it can be, so that a new feature in a new config comes from composing existing pieces rather than adding new ones. Before adding a runtime concept or option, check whether existing pieces already combine to do the job. If they do, use the combination and document it, and leave any friendlier shorthand to the config file (or SurfaceSpec), which can expand it to the runtime form. Performance is the trade-off: when a composition would put the realtime budget at risk (see Performance), a dedicated piece is justified.

A feature follows the feature workflow, with its CLs in library order: `common`, then `device`, then `scene`, then `plugin`. Its plan starts with what it relies on that nobody has checked (its To confirm), which is checked in REAPER before the CLs that rely on it (see Checking in REAPER). The CLs' **Verify** steps start from the checks in Testing and Logging, and the user reviews each CL, but tests the feature in REAPER only at its end.

## Parallel sessions

These add to Parallel sessions in the workflow. REAPER loads a single copy of the plugin, and the user is the only one who can test it, so work that needs REAPER (see Checking in REAPER) happens one change at a time in the main session, in the main checkout.

Side sessions run in their own git worktree, for work that can be verified without REAPER: most CLs, which their unit tests verify, documentation and comment cleanup, research, and reviews.
- A worktree build does not deploy the plugin (`JPR_DEPLOY_TO_REAPER` defaults to OFF there), so it never replaces what the user is testing. Don't turn it on.
- A side session never merges or pushes to `main`. The main session cherry-picks its commit onto `main`, builds, and hands anything that needs a REAPER check to the user.
- If a change turns out to need REAPER (behavior nobody has checked, or a profile), say so and hand it back to the main session rather than deploying it.

## Resources

REAPER is notoriously underdocumented. The best resources are as follows:
- REAPER C++ SDK. What is installed locally is at `../reaper-sdk`. Very light on docs. Includes the locally available ReaScript API in C++ form.
- ReaScript API (mirrors the C++ API).
  - Official documentation here: https://www.reaper.fm/sdk/reascript/reascripthelp.html
  - Navigable site for REAPER functions in multiple languages: https://www.extremraym.com/cloud/reascript-doc/
- Working plugins that do largely the same sort of thing. JPRSurf is my personal replacement for these:
  - Klinke MCU: https://github.com/jpursey/csurf_klinke_mcu_jp This was very reliable, and in C++, but missing features I wanted. This is my personal fork of the project. It is downloaded and available locally at `../csurf_klinke_mcu`.
  - DrivenByMOSS: https://github.com/git-moss/DrivenByMoss4Reaper This was feature rich, but quite flaky in practice, and also was written in Java.

## Don't
- Don't add or modify code outside `src/` without asking. Game Bits code is changed only in Game Bits sessions (see the workflow).
- Don't generate or build Visual Studio solutions (`-G "Visual Studio 17 2022"`); build with Ninja as described above.
- Don't add entries to the README's "Development log", or plan steps for it. The user writes those.
