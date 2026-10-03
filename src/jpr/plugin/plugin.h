// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <memory>
#include <utility>

#include "jpr/common/reaper_trace.h"
#include "jpr/common/test_reset.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

// Singleton class representing the plugin instance. Responsible for
// plugin-level lifecycle management and global state.
class Plugin final {
 public:
  // What the plugin reads from its environment when REAPER loads it (see
  // dll_main.cc). An empty path turns each off, as tests load it.
  struct Options {
    // Where to write a trace of every call between JPRSurf and REAPER (see
    // ReaperTrace), set by JPRSURF_TRACE.
    std::filesystem::path trace_path;

    // Where each surface writes its profile (see ReaperProfiler).
    std::filesystem::path profile_path;
  };

  // Loads the plugin from what REAPER passes its entry point: loads the REAPER
  // API, and registers the control surface type. Returns false (and logs an
  // error) if it is already loaded, or loading fails.
  static bool Load(HINSTANCE hinstance, reaper_plugin_info_t& plugin_info,
                   const Options& options);
  static void Unload();

  Plugin(const Plugin&) = delete;
  Plugin& operator=(Plugin&) = delete;
  ~Plugin() = default;

  static Plugin* GetInstance() { return s_instance_; }

  HINSTANCE GetHInstance() const { return hinstance_; }

 private:
  Plugin(HINSTANCE hinstance, std::unique_ptr<ReaperTrace> trace)
      : hinstance_(hinstance), trace_(std::move(trace)) {}

  static Plugin* s_instance_;
  static const TestReset s_test_reset_;

  HINSTANCE hinstance_ = nullptr;
  std::unique_ptr<ReaperTrace> trace_;
};

}  // namespace jpr
