// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>

#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// CreateConstProperty
//==============================================================================

// Creates a property that always reads the value, and ignores writes (see
// Scene::AddConstProperty(), which creates one for the scene).
//
// Each property gets a new name in the const: namespace, unique for the life of
// the process.
//
// The type and value must be one of these, or this returns null:
// - kToggle, with a bool.
// - kNormalized, with a double in [0,1].
// - kText, with a std::string.
// - kColor, with a Color.
std::unique_ptr<ViewProperty> CreateConstProperty(ViewProperty::Type type,
                                                  ViewProperty::Value value);

}  // namespace jpr
