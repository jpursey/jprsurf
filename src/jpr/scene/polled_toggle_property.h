// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/scene/scene_state_property.h"

namespace jpr {

// This property represents REAPER state that is read with a function, and
// polled each run. It can be mapped to control outputs that represent a binary
// value. If it has a write function, it can also be mapped to control inputs,
// and writing it changes the state.
//
// The built in polled toggles are in the state: namespace (see
// state_properties.h).
class PolledToggleProperty final : public SceneStateProperty {
 public:
  using ReadFunction = bool (*)();
  using WriteFunction = void (*)(bool value);

  PolledToggleProperty(Scene* scene, std::string_view name, ReadFunction read,
                       WriteFunction write = nullptr)
      : SceneStateProperty(scene, name, Type::kToggle),
        read_(read),
        write_(write),
        value_(read()) {}
  ~PolledToggleProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override;
  void WriteBool(bool value) override;

 private:
  ReadFunction read_;
  WriteFunction write_;
  bool value_;
};

}  // namespace jpr
