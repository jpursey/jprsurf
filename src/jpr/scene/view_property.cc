// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/view_property.h"

#include <string_view>

#include "jpr/scene/view_mapping.h"

namespace jpr {

ViewProperty::ViewProperty(std::string_view name) : name_(name) {}

void ViewProperty::AddMapping(ViewMapping* mapping) {
  mappings_.insert(mapping);
}

void ViewProperty::RemoveMapping(ViewMapping* mapping) {
  mappings_.erase(mapping);
}

}  // namespace jpr