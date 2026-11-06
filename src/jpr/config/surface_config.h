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
// block missing or given more often than it may be, a statement where it
// can't be (such as named mappings in a view, or anything but a view in a
// "when" around statements), a "when" in a "when", or around a use or an
// option other than "mode", a component kind, parameter, or setting that
// doesn't exist, a parameter, setting, or how a control is read given twice,
// more than one press behavior, input, mode, or range on a mapping, a value of
// the wrong form, a color over 255, a use's argument given twice, and a
// required choice (such as a track anchor's
// action) left out, as a spec has no value for none of its choices. Checking
// the spec finds any other parameter left out, and everything else.
std::optional<SurfaceSpec> ReadSurfaceConfig(std::string text,
                                             std::vector<std::string>* errors);

}  // namespace jpr
