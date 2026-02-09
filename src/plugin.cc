// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "plugin.h"

#include "absl/log/log.h"
#include "control_surface.h"
#include "reaper_plugin_functions.h"

namespace jpr {

Plugin* Plugin::s_instance_ = nullptr;

bool Plugin::Load(HINSTANCE hinstance, reaper_plugin_info_t& plugin_info) {
  if (s_instance_ != nullptr) {
    LOG(ERROR) << "Plugin instance already exists.";
    return false;
  }
  if (plugin_info.caller_version != REAPER_PLUGIN_VERSION) {
    LOG(ERROR) << "Plugin version mismatch: expected " << REAPER_PLUGIN_VERSION
               << ", got " << plugin_info.caller_version;
    return false;
  }
  if (plugin_info.GetFunc == nullptr) {
    LOG(ERROR) << "Plugin info GetFunc is null.";
    return false;
  }
  if (REAPERAPI_LoadAPI(plugin_info.GetFunc) != 0) {
    LOG(ERROR) << "Failed to load REAPER API.";
    return false;
  }

  if (plugin_info.Register("csurf", ControlSurface::GetControlSurfaceReg()) ==
      0) {
    LOG(ERROR) << "Failed to register control surface.";
    return false;
  }

  s_instance_ = new Plugin(hinstance);

  LOG(INFO) << "Plugin loaded.";
  return true;
}

void Plugin::Unload() {
  if (s_instance_ == nullptr) {
    LOG(WARNING) << "Plugin instance is null during unload.";
    return;
  }
  delete s_instance_;
  s_instance_ = nullptr;
  LOG(INFO) << "Plugin unloaded.";
}

}  // namespace jpr

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* plugin_info) {
  if (plugin_info == nullptr) {
    jpr::Plugin::Unload();
    return 0;
  }
  return jpr::Plugin::Load(instance, *plugin_info) ? 1 : 0;
}

}  // extern "C"
