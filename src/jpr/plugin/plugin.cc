// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin.h"

#include <memory>
#include <utility>

#include "absl/log/log.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/reaper_trace.h"
#include "jpr/common/test_reset.h"
#include "jpr/plugin/plugin_surface.h"

namespace jpr {

Plugin* Plugin::s_instance_ = nullptr;

const TestReset Plugin::s_test_reset_([] {
  delete s_instance_;
  s_instance_ = nullptr;
});

bool Plugin::Load(HINSTANCE hinstance, reaper_plugin_info_t& plugin_info,
                  const Options& options) {
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
  if (!LoadReaperApi(plugin_info.GetFunc)) {
    LOG(ERROR) << "Failed to load REAPER API.";
    return false;
  }

  // The trace starts before the surface is registered, so it traces the
  // surface.
  std::unique_ptr<ReaperTrace> trace;
  if (!options.trace_path.empty()) {
    trace = std::make_unique<ReaperTrace>(options.trace_path);
  }

  if (!PluginSurface::Register(plugin_info, options.profile_path)) {
    return false;
  }

  s_instance_ = new Plugin(hinstance, std::move(trace));

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
