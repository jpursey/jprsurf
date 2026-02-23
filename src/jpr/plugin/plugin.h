// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "sdk/reaper_plugin.h"

namespace jpr {

// Singleton class representing the plugin instance. Responsible for
// plugin-level lifecycle management and global state.
class Plugin final {
 public:
  // Registration functions
  static bool Load(HINSTANCE hinstance, reaper_plugin_info_t& plugin_info);
  static void Unload();

  Plugin(const Plugin&) = delete;
  Plugin& operator=(Plugin&) = delete;
  ~Plugin() = default;

  static Plugin* GetInstance() { return s_instance_; }

  HINSTANCE GetHInstance() const { return hinstance_; }

 private:
  Plugin(HINSTANCE hinstance) : hinstance_(hinstance) {}

  static Plugin* s_instance_;

  HINSTANCE hinstance_ = nullptr;
};

}  // namespace jpr
