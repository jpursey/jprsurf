// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "reaper_plugin_functions.h"

namespace jpr {

bool OnPluginLoad(reaper_plugin_info_t* plugin_info) {
  if (plugin_info == nullptr) {
    return false;
  }
  if (plugin_info->caller_version != REAPER_PLUGIN_VERSION) {
    return false;
  }
  if (plugin_info->GetFunc == nullptr) {
    return false;
  }
  if (REAPERAPI_LoadAPI(plugin_info->GetFunc) != 0) {
    return false;
  }
  return true;
}

}  // namespace jpr

extern "C" {

REAPER_PLUGIN_DLL_EXPORT int REAPER_PLUGIN_ENTRYPOINT(
    REAPER_PLUGIN_HINSTANCE instance, reaper_plugin_info_t* plugin_info) {
  return jpr::OnPluginLoad(plugin_info);
}

}  // extern "C"
