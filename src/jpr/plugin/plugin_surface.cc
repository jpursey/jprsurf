// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin_surface.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/log/log.h"
#include "gb/config/text_config.h"
#include "jpr/plugin/default_spec.h"
#include "jpr/scene/create_scene.h"

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
  return ControlSurface::Register(plugin_info,
                                  {.type_string = kTypeString,
                                   .description = kDescString,
                                   .create_listener = PluginSurface::Create,
                                   .profile = profile});
}

std::unique_ptr<ControlSurfaceListener> PluginSurface::Create(
    std::string_view config) {
  // The listener is a private base, so convert the pointer here, where it is
  // accessible.
  return std::unique_ptr<ControlSurfaceListener>(new PluginSurface(config));
}

PluginSurface::PluginSurface(std::string_view config) {
  if (!config.empty()) {
    auto parsed_config = gb::ReadConfigFromText(std::string(config));
    if (!parsed_config.ok()) {
      LOG(ERROR) << "Failed to parse control surface config: "
                 << parsed_config.status();
      LOG(ERROR) << config;
    }
  }
  LOG(INFO) << "PluginSurface created";

  // Without the scene, as when the X-Touch is absent, the surface does nothing.
  std::vector<std::string> errors;
  scene_ = CreateScene(GetDefaultSpec(), midi_ports_, device_runner_, &errors);
  for (const std::string& error : errors) {
    LOG(ERROR) << "Failed to create the scene: " << error;
  }
  if (scene_ != nullptr) {
    scene_->Activate(scene_runner_);
  }
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

std::string PluginSurface::GetConfig() const {
  return gb::WriteConfigToText(config_, gb::kCompactTextConfig);
}

}  // namespace jpr
