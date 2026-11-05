// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/surface_spec.h"

namespace jpr {

// Creates the scene the spec describes, on its devices, which run on
// `device_registry`, and returns it, owning them. The scene isn't active yet
// (see Scene::Activate()).
//
// Returns null, adding why to `errors` (if provided), if the spec isn't valid
// (see ValidateSurfaceSpec()), a required device is absent (see
// CreateDevices()), or the scene refuses anything the spec adds to it. Only
// REAPER knows which commands exist, so a command it doesn't have, which the
// scene would refuse, is reported as one: a spec never runs half working.
// Errors about what the scene refuses start as checking's do, with each
// repeated view named by its instance, such as "/track_list/strip[3]".
//
// The scene is created with the spec's track filter, then its top-level
// components are declared, in order, then its views built, from the root, as
// ViewSpec describes. A repeated view has an instance for each element of its
// array that is present, named by its element's path, such as "strip[3]", so
// the elements of an absent device have none. Every view is enabled, as
// conditions decide which are active. Each distinct fixed value is one const:
// property (see Scene::AddConstProperty()), which every mapping of it shares.
std::unique_ptr<Scene> CreateScene(const SurfaceSpec& spec, MidiPorts& ports,
                                   RunRegistry& device_registry,
                                   std::vector<std::string>* errors);

}  // namespace jpr
