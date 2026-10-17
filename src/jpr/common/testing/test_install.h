// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <string_view>

#include "jpr/common/testing/fake_project.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// The test install
//
// What a DLL that runs inside the test install of REAPER needs (see "Contract
// tests in REAPER" in docs/testing_and_profiling.md): loading REAPER's API,
// the run directory it writes its files to, opening projects, and finishing.
//
// run_in_test_install.cmake runs such a DLL, with JPR_REAPER_RUN_DIR naming
// the run directory, and the DLL's surface type as the only control surface.
// The surface does its work from its runs, then calls FinishRun(). The run
// directory gets:
// - output.txt: what the DLL has to show, which the script prints.
// - result.txt: 0 if the DLL succeeded, and 1 otherwise (see FinishRun()).
// - log.txt: what the DLL logged, which shows how far a run got.
// - project.rpp: the last project opened (see OpenProjectFile()).
//==============================================================================

// Loads the test install from the DLL's entry point: logging to log.txt in the
// run directory, REAPER's API, and the functions the test install calls that
// aren't on the API list. Then registers `registration` as a control surface
// type. Returns false, having logged why, if `plugin_info` is null (REAPER
// unloading the DLL) or anything failed to load. The entry point returns the
// result as 1 or 0.
bool LoadTestInstall(reaper_plugin_info_t* plugin_info,
                     reaper_csurf_reg_t* registration);

// Returns the run directory, or empty if JPR_REAPER_RUN_DIR isn't set, in which
// case the surface should do nothing, so the test install can be set up by
// hand.
const std::filesystem::path& GetRunDir();

// Writes `text` to the file `name` in the run directory, replacing it. Returns
// false if it couldn't.
bool WriteRunFile(std::string_view name, std::string_view text);

// Opens `project` in REAPER as the current project, in place of the one open,
// from a project file (project.rpp in the run directory), without asking to
// save the one it replaces. It finishes within the call. Returns false, having
// logged why, if it couldn't write the project file.
bool OpenProjectFile(const FakeProject& project);

// Finishes the run, from the surface's run: writes result.txt, opens an empty
// project, so quitting has nothing to save, and quits REAPER once the run has
// returned (quitting from inside a run destroys the surface while it runs). The
// surface must do nothing in any run after this.
void FinishRun(bool succeeded);

}  // namespace jpr
