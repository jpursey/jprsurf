// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <string_view>

#include "absl/container/flat_hash_set.h"
#include "jpr/device/control.h"

namespace jpr {

class ViewMapping;

// A view control represents a single control on a device that is being mapped
// by one or more active views.
//
// It holds the state for the control and the set of all active mappings to view
// properties. This is used by the Scene to find all mappings that need to be
// updated when the control changes.
class ViewControl final {
 public:
  using Mappings = absl::flat_hash_set<ViewMapping*>;

  explicit ViewControl(std::string_view device_name, Control* control);
  ViewControl(const ViewControl&) = delete;
  ViewControl& operator=(const ViewControl&) = delete;
  ~ViewControl() = default;

  // Attributes
  Control* GetControl() const { return control_; }
  std::string_view GetName() const { return name_; }

  // Current active mappings for this control. This will be empty if the control
  // is not currently mapped by any active views.
  const Mappings& GetMappings() const { return mappings_; }
  void AddMapping(ViewMapping* mapping);
  void RemoveMapping(ViewMapping* mapping);

 private:
  Control* control_;
  std::string name_;
  Mappings mappings_;
};

}  // namespace jpr