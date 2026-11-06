// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "jpr/common/control_surface.h"
#include "jpr/common/midi_ports.h"
#include "jpr/common/reaper_profiler.h"
#include "jpr/common/runner.h"
#include "jpr/scene/scene.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// A JPRSurf control surface, whose config string is the path of its config
// file, in GetFileSystem(), which REAPER's control surface dialog shows, with a
// Browse button (see CreateConfigPanel()). An empty config string is the
// default config file, which Register() names. So is "null", which earlier
// versions wrote, and which the surface gives back to REAPER as empty.
//
// The surface reads its config file once, as it is created. Each error, from
// reading the file to creating the scene, is logged with the file's path in
// front (`<path>: line 61: ...`), and the whole config is rejected: the surface
// then does nothing.
class PluginSurface final : private ControlSurfaceListener {
 public:
  // Registers JPRSurf as a control surface type with REAPER, whose surfaces
  // read `default_config_path` when given no config string, and write
  // `profile`, or none if its path is empty. Returns false (and logs an error)
  // if registration fails.
  static bool Register(reaper_plugin_info_t& plugin_info,
                       std::string_view default_config_path,
                       const ReaperProfiler::Options& profile);

  PluginSurface(const PluginSurface&) = delete;
  PluginSurface& operator=(const PluginSurface&) = delete;
  ~PluginSurface() override;

 private:
  // Creates the listener for a new JPRSurf control surface.
  static std::unique_ptr<ControlSurfaceListener> Create(
      std::string_view default_config_path, std::string_view config);

  PluginSurface(std::string_view default_config_path, std::string_view config);

  // Returns the scene the config file at `path` describes, or null, logging
  // each error.
  std::unique_ptr<Scene> ReadScene(std::string_view path);

  // ControlSurfaceListener overrides
  void OnRun(const RunTime& time) override;
  std::string GetConfig() const override;

  // State
  std::string config_;              // The config string, empty for "null".
  Runner device_runner_{"Device"};  // Resets device state, sends output.
  Runner scene_runner_{"Scene"};    // Updates the scene.
  MidiPorts midi_ports_;  // Outlives the scene, whose devices use the ports.
  std::unique_ptr<Scene> scene_;  // Null if it couldn't be created.
};

}  // namespace jpr
