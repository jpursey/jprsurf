// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/scene/surface_spec.h"

namespace jpr {

// Returns JPRSurf's own surface: an X-Touch, and an X-Touch Extender to its
// left if there is one, with two surface modes. In Track mode the strips show
// the child tracks of a folder, and in Send/Receive mode they show one track's
// routes (see "JPRSurf's surface in the model" in docs/config_model.md).
SurfaceSpec GetDefaultSpec();

}  // namespace jpr
