// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "jpr/scene/surface_spec.h"

namespace jpr {

// The version of the config language this reads, which a config's first
// statement names (`jprsurf 1`).
inline constexpr int kSurfaceConfigVersion = 1;

// Reads a config's text into a spec, which is nullopt if there is an error,
// each added to `errors` (if provided), starting with the line it is on (see
// GetSpecError()). The spec isn't checked (see ValidateSurfaceSpec()), but each
// part of it has its line, so checking's errors start with it too.
//
// A syntax error stops reading, as does a version other than
// kSurfaceConfigVersion, but reading reports every other error it finds: a
// block missing or given more often than it may be, a component kind,
// parameter, or setting that doesn't exist, a parameter or setting given
// twice, a value of the wrong form, and a required choice (such as a track
// anchor's action) left out, as a spec has no value for none of its choices.
// Checking the spec finds any other parameter left out.
std::optional<SurfaceSpec> ReadSurfaceConfig(std::string text,
                                             std::vector<std::string>* errors);

}  // namespace jpr
