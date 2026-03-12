// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/container/flat_hash_map.h"
#include "jpr/common/runner.h"
#include "jpr/device/device.h"
#include "jpr/scene/view.h"

namespace jpr {

// This is the top level mapping and view between one or more devices and the
// REAPER state.
//
// It holds the state for all actively mapped controls and reaper properties,
// and is responsible for ensuring synchronization.
class Scene final {
 public:
  explicit Scene(std::string_view name);
  Scene(const Scene&) = delete;
  Scene& operator=(const Scene&) = delete;
  ~Scene();

  // Attributes
  std::string_view GetName() const { return name_; }

  // Devices
  void AddDevice(std::string_view device_name, std::unique_ptr<Device> device);

  // Views
  View* GetRootView() const { return root_view_.get(); }

  // Controls and Properties
  Control* GetControl(std::string_view name) const;
  ViewProperty* GetProperty(std::string_view name) const;

  // Activation and deactivation
  void Activate(RunRegistry& registry);
  void Deactivate();

 private:
  void OnRun(const RunTime& time);

  // State
  std::string name_;
  absl::flat_hash_map<std::string, std::unique_ptr<Device>> devices_;
  absl::flat_hash_map<std::string, Control*> controls_;
  absl::flat_hash_map<std::string, std::unique_ptr<ViewProperty>> properties_;
  std::unique_ptr<View> root_view_;
  RunHandle run_handle_;
};

}  // namespace jpr