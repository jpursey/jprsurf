// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string>
#include <utility>

#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// ViewCondition
//==============================================================================

// A condition that decides whether a view or mapping is active. It is met while
// its property's value, read as the condition's value's type, equals it (see
// ViewProperty::Equals()).
//
// A condition can be watched, to find out when its property changes. It stops
// watching when it is destroyed, so it must be destroyed before its property.
class ViewCondition final {
 public:
  // Configures a condition, with its property by name.
  struct Config {
    // The name of a property, as seen from the view that has the condition,
    // or that has the mapping with the condition.
    std::string property;

    // The value the property must equal, such as true for a toggle, or an int
    // for an enumerated property.
    ViewProperty::Value value = true;
  };

  ViewCondition(ViewProperty* property, ViewProperty::Value value)
      : property_(property), value_(std::move(value)) {}
  ViewCondition(const ViewCondition&) = delete;
  ViewCondition& operator=(const ViewCondition&) = delete;
  ~ViewCondition() { Watch(false); }

  bool IsMet() const { return property_->Equals(value_); }

  // Starts or stops watching the property, and clears HasChanged().
  void Watch(bool watch);

  // Returns true if the property changed while watched, since the last call to
  // Watch().
  bool HasChanged() const { return changed_; }

 private:
  ViewProperty* const property_;
  const ViewProperty::Value value_;
  bool watching_ = false;
  bool changed_ = false;
};

}  // namespace jpr
