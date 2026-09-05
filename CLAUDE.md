# JPSurf

This is a C++ control surface extension for the REAPER DAW. It handles bi-directional control between physical hardware and a running instance of REAPER. REAPER is a realtime, low latency, application processing audio, so performance and reliability of the extension are critical.

This extension depends only on the Reaper SDK (location defined by the REAPER_EXTENSION_SDK environment variable) and the Game Bits shared C++ library (location defined by the GB_DIR extension variable) which must be present in the environment and on the machine. It currently only works on Windows, and is built with Visual Studio 2022 Community and CMake.

## Directory Structure

This is a CMake project, starting at the root. The directory structure is as follows:
```
  src/             -- All source code in this project
    jpr/           -- All source code for the extension itself, separated into different libraries (see below)
    reaper_sdk.cc  -- Implementation of the header-only REAPER SDK library
  bin/          -- Compiled binary files used by compilation or execution
  out/          -- Generated output from building locally. This is transient
                   and can get deleted at any time.
```

The current library structure is as follows:
- `common`: This library contains common types, and classes that provide general utilties for working within the REAPER SDK, and managing generic REAPER state (MIDI ports, project tracks, etc). It does not depend on any other JPSurf library and is not specific to the JPSurf extension itself (it could be generically useful for any REAPER extension).
- `device`: This library provides an interface and concrete implementations for hardware devices supported by JPSurf. This handles all communication to and from the device and tracks the current device state. It does not contain information about how those devices are related to any REAPER behavior. The primary classes here are `Device` (the generic interface for a hardware device), and `Control` (a logical representation of a control on that device that may be read and/or written to). The only JPSurf library it depends on is `common`. 
- `scene`: This library defines how devices controls may be mapped to REAPER actions and properties. It knows and can define specific REAPER properties and abstractions, but it does not hard code these mappings and must be configured. The primary classes here are `Scene` which defines the top level mapping between one or more devices and REAPER state, and `View` which is a hierarchical set of specific mappings that may be enabled or disabled independently based on configuration or explicit application control. The only JPSurf libraries it depends on are `common` and `device`.
- `plugin`: This is the top level library which is the entry point for the REAPER extension and configures the actual Control Surface Integration (CSI), using the connected devices for REAPER. The primary class here is `ControlSurface` which implements the `IReaperControlSurface`, and defines the actual mappings and business logic for the extension. It depends on everything.

## Commands

Everything is driven directly by CMake using the Ninja generator, which is exactly what Visual Studio's "open a local folder" CMake integration does (see CMakeSettings.json). Command line builds, IDE builds, and CI all use the same build.

### Developer environment (once per shell)

CMake and Ninja need an x64 MSVC developer environment; nothing below works without it.

```
# PowerShell
Import-Module "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files\Microsoft Visual Studio\2022\Community" -DevCmdArguments "-arch=x64 -host_arch=x64" -SkipAutomaticLocation

# cmd
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
```

There are no test binaries, as nearly all code requires REAPER to be running to test.

### Configure

These match the `x64-Debug` and `x64-Release` configurations in CMakeSettings.json, so the IDE picks up whatever the command line configures and vice versa. Note that Visual Studio's "x64-Release" is `RelWithDebInfo`, not `Release`.

```
cmake -G Ninja -DCMAKE_BUILD_TYPE=Debug -S . -B out/build/x64-Debug
cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S . -B out/build/x64-Release
```

If configure or build fails with a missing `cl.exe`, `rc.exe`, or Windows SDK path, the tree's cache is left over from an older Visual Studio or Windows SDK version. Delete the build tree and configure again (in the IDE this is "Delete Cache and Reconfigure").

### Build

```
cmake --build out/build/x64-Debug
```

Do not build through a generated Visual Studio solution (`-G "Visual Studio 17 2022"`) instead.

MSVC 14.44 intermittently crashes (`fatal error C1001`, occasionally `LNK1127`) in the optimizer, in a different translation unit each time. This is a toolchain problem, not an error in the code, and it only shows up in optimized (`Release` / `RelWithDebInfo`) builds -- Debug builds are reliable. Re-run the exact same command and the failing target compiles.

The build also will copy the binary to the REAPER plugin directory so it can be run immediately.

### Testing and Logging

Testing must be performed manually by the user in REAPER. 

Logs from LOG statements are written to "C:\Users\username\AppData\Roaming\jpsurf.log" and are cleared and rewritten each time REAPER is run and/or loads the extension. Additional debugging information can be added there to get debug what is going on. However, LOGs should be minimized outside of debugging use cases as they affect performance and diskspace. LOGs for particular infrequent events, may be retained as is helpful for persistent understanding of code flow (continuous controler and UI events generally do *not* fall into this category).

### Format

```
clang-format -i <files>              # format in place
clang-format --dry-run -Werror <files>   # check only
```

Style comes from `src/.clang-format` (Google style); clang-format finds it automatically for any file under `src/`. Only format files you actually touch.

## Build system

- Each library is defined by a `CMakeLists.txt` in its own directory using the `gb_add_library` / `gb_add_shared_library` commands from Game Bits.
- New source and test files must be added to their module's `CMakeLists.txt` (`<target>_SOURCE`) or they will not be compiled.
- Defining `<target>_TEST_SOURCE` automatically creates a `<target>_test` executable that links GoogleTest/GoogleMock and registers a ctest test of the same name. However, most no libraries can be tested if they depend on the REAPER SDK directly or indirectly.
- `<target>_DEPS` is for other CMake targets in the build (including `absl::*`); `<target>_LIBS` is for prebuilt external libraries.

## Conventions

- Coding guidelines: Generally follows the Google C++ style guide (https://google.github.io/styleguide/cppguide.html).
- Formatting strictly driven by clang-format in Google style via src/.clang-format
- All extension code is in the "jpr" namespace, except for required C extension points of the REAPER SDK.
- Every file starts with the four line MIT copyright comment used everywhere in the tree, with the year the file was created.
- Headers use `#pragma once` include guards (not `#ifndef` include guards).
- Include order: the file's own header first, then C/C++ standard headers in angle brackets, then third-party, Game Bits, and JPSurf headers in quotes (`"absl/..."`, `"gb/..."`, `"jpr/..."`), with blank lines between groups.
- Sections in a file are separated by //===== blocks (extending to column 80) surrounding descriptive text: One line section description, and if necessary further description in additional paragraphs.
- Sections within a class or between groups of related functions are separated by //---- blocks (otherwise the same as above).
- All comments are // style (not /// or /*...*/)
- Prefer Abseil (and other Google open source libraries already vendored in third_party/) over hand-rolled utilities.
- C++20, built with both MSVC and clang-cl.
- Files in the working tree use CRLF line endings (git `core.autocrlf` is true); leave them that way.

## Don't
- Don't add new dependencies without asking.
- Don't add or modify code outside src/jpr/ without asking.
- Don't generate or build Visual Studio solutions; build with Ninja as described above.
- Don't reformat files you aren't otherwise changing.
- Don't commit a change to a branch without a human review from the user first
