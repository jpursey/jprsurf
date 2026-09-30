// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "absl/time/time.h"
#include "gb/config/config.h"
#include "jpr/common/control_surface.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/runner.h"
#include "jpr/device/control_input.h"
#include "jpr/device/control_output.h"
#include "jpr/scene/scene.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

class PluginSurface final : private ControlSurfaceListener {
 public:
  // Registers JPRSurf as a control surface type with REAPER. Returns false (and
  // logs an error) if registration fails.
  static bool Register(reaper_plugin_info_t& plugin_info);

  PluginSurface(const PluginSurface&) = delete;
  PluginSurface& operator=(const PluginSurface&) = delete;
  ~PluginSurface() override;

 private:
  // Creates the listener for a new JPRSurf control surface.
  static std::unique_ptr<ControlSurfaceListener> Create(
      std::string_view config);

  explicit PluginSurface(std::string_view config);

  // ControlSurfaceListener overrides
  void OnRun(absl::Time now) override;
  std::string GetConfig() const override;

  // Implementation
  void InitViews();

  // State
  gb::Config config_;
  Runner device_runner_{"Device"};  // Resets device state, sends output.
  Runner scene_runner_{"Scene"};    // Updates the scene.
  MidiPorts midi_ports_;  // Outlives the scene, whose devices use the ports.
  std::unique_ptr<Scene> scene_;
};

}  // namespace jpr