// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"

namespace jpr {

class ViewMapping;

// A view property represents a single property of the REAPER state that is
// being mapped by one or more active views.
//
// It holds the state for the property and the set of all active mappings to
// view controls. This is used by the Scene to find all mappings that need to be
// updated when the property changes.
class ViewProperty final {
 public:
  using Mappings = absl::flat_hash_set<ViewMapping*>;

  explicit ViewProperty(std::string_view name);
  ViewProperty(const ViewProperty&) = delete;
  ViewProperty& operator=(const ViewProperty&) = delete;
  ~ViewProperty() = default;

  // Name of the property.
  std::string_view GetName() const { return name_; }

  // Current active mappings for this property. This will be empty if the
  // property is not currently mapped by any active views.
  const Mappings& GetMappings() const { return mappings_; }
  void AddMapping(ViewMapping* mapping);
  void RemoveMapping(ViewMapping* mapping);

 private:
  std::string name_;
  Mappings mappings_;
};

}  // namespace jpr
