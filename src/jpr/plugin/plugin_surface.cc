// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin_surface.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/log/log.h"
#include "jpr/common/file_system.h"
#include "jpr/common/win_config_panel.h"
#include "jpr/config/surface_config.h"
#include "jpr/scene/create_scene.h"
#include "jpr/scene/surface_spec.h"

namespace jpr {

namespace {

constexpr const char kTypeString[] = "JPRSurf";
constexpr const char kDescString[] = "Jovian Path Control Surface";

}  // namespace

//------------------------------------------------------------------------------
// Registration
//------------------------------------------------------------------------------

bool PluginSurface::Register(reaper_plugin_info_t& plugin_info,
                             const ReaperProfiler::Options& profile) {
  return ControlSurface::Register(
      plugin_info, {.type_string = kTypeString,
                    .description = kDescString,
                    .create_listener = &PluginSurface::Create,
                    .profile = profile,
                    .config_file = FileFilter{.description = "JPRSurf config",
                                              .pattern = "*.jprsurf"}});
}

std::unique_ptr<ControlSurfaceListener> PluginSurface::Create(
    std::string_view config) {
  // The listener is a private base, so convert the pointer here, where it is
  // accessible.
  return std::unique_ptr<ControlSurfaceListener>(new PluginSurface(config));
}

PluginSurface::PluginSurface(std::string_view config)
    : config_(config == "null" ? "" : config) {
  LOG(INFO) << "PluginSurface created";

  if (config_.empty()) {
    LOG(WARNING) << "The surface has no config file, so it does nothing until "
                    "one is picked in its settings in REAPER";
    return;
  }

  // Without the scene, as when the X-Touch is absent, the surface does nothing.
  scene_ = ReadScene(config_);
  if (scene_ != nullptr) {
    scene_->Activate(scene_runner_);
  }
}

std::unique_ptr<Scene> PluginSurface::ReadScene(std::string_view path) {
  LOG(INFO) << "Reading the config file " << path;
  std::string text;
  if (!GetFileSystem()->ReadFile(path, &text)) {
    LOG(ERROR) << path << ": the config file can't be read";
    return nullptr;
  }
  std::vector<std::string> errors;
  std::unique_ptr<Scene> scene;
  if (std::optional<SurfaceSpec> spec =
          ReadSurfaceConfig(std::move(text), &errors)) {
    scene = CreateScene(*spec, midi_ports_, device_runner_, &errors);
  }
  for (const std::string& error : errors) {
    LOG(ERROR) << path << ": " << error;
  }
  return scene;
}

PluginSurface::~PluginSurface() {
  // Clear the surface, so it doesn't keep showing the last state after REAPER
  // exits or the surface is removed. Deactivating the scene releases every
  // mapping's output writer, which clears each control on its next run. There
  // are no more calls to OnRun(), so run the devices to clear their controls.
  // The devices let go of their ports as the scene goes, and destroying the
  // MIDI ports then sends it (see MidiPorts).
  if (scene_ != nullptr) {
    scene_->Deactivate();
    device_runner_.Run(RunTime::Now());
  }
  LOG(INFO) << "PluginSurface destroyed";
}

//------------------------------------------------------------------------------
// ControlSurfaceListener overrides
//------------------------------------------------------------------------------

void PluginSurface::OnRun(const RunTime& time) {
  device_runner_.Run(time);
  midi_ports_.RunInput(time);
  scene_runner_.Run(time);
  midi_ports_.RunOutput(time);
}

std::string PluginSurface::GetConfig() const { return config_; }

}  // namespace jpr
