// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/plugin/plugin.h"

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "jpr/common/log_file.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/reaper_trace.h"
#include "jpr/plugin/plugin_surface.h"

namespace jpr {

namespace {

// Starts a trace of every call between JPRSurf and REAPER, if the JPRSURF_TRACE
// environment variable is set to anything but 0.
std::unique_ptr<ReaperTrace> StartTrace() {
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, "JPRSURF_TRACE") != 0 || value == nullptr) {
    return nullptr;
  }
  const std::string_view text(value);
  const bool trace = !text.empty() && text != "0";
  free(value);
  if (!trace) {
    return nullptr;
  }
  const std::filesystem::path path = GetLogPath("jprsurf_trace.txt");
  if (path.empty()) {
    LOG(ERROR)
        << "JPRSURF_TRACE is set, but there is nowhere to write the trace.";
    return nullptr;
  }
  return std::make_unique<ReaperTrace>(path);
}

}  // namespace

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
  if (!LoadReaperApi(plugin_info.GetFunc)) {
    LOG(ERROR) << "Failed to load REAPER API.";
    return false;
  }

  // The trace starts before the surface is registered, so it traces the
  // surface.
  std::unique_ptr<ReaperTrace> trace = StartTrace();

  if (!PluginSurface::Register(plugin_info)) {
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
