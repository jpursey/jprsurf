// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <string_view>
#include <utility>

#include "absl/strings/str_cat.h"
#include "absl/time/time.h"
#include "gb/profile/profile_call_hook.h"
#include "gb/profile/profiler.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

//==============================================================================
// ReaperProfiler
//
// Profiles JPRSurf for as long as it exists: every point defined with
// gb/profile on the thread that created it, which must be REAPER's UI thread
// (see gb::Profiler), and every call to a function on the REAPER API list (see
// reaper_api.h), as the call point reaper/<name>.
//
// It is created with the control surface, before its listener, and destroyed
// after it, so a profile covers one surface's whole life.
//
// Only one ReaperProfiler may exist at a time, and it must be destroyed outside
// any timed point.
//==============================================================================

class ReaperProfiler final {
 public:
  // Starts profiling. When the profiler is destroyed, it writes the profile to
  // the file at `path`, replacing it, or logs an error if `path` is empty.
  explicit ReaperProfiler(std::filesystem::path path);
  ReaperProfiler(const ReaperProfiler&) = delete;
  ReaperProfiler& operator=(const ReaperProfiler&) = delete;
  ~ReaperProfiler();

 private:
  // Times each call through a hooked REAPER API function as the call point
  // reaper/<name>.
  class CallHook final {
   public:
    explicit CallHook(std::string_view name)
        : hook_(absl::StrCat("reaper/", name)) {}
    CallHook(const CallHook&) = delete;
    CallHook& operator=(const CallHook&) = delete;
    ~CallHook() = default;

    template <typename Function, typename... Args>
    auto Call(Function original, Args&&... args) {
      return hook_.Call(original, std::forward<Args>(args)...);
    }

   private:
    gb::ProfileCallHook hook_;
  };

  // Writes the profile to path_.
  void WriteSnapshot() const;

  const std::filesystem::path path_;
  const absl::Time start_time_;
  gb::Profiler profiler_;
  ReaperApiHooks<CallHook> hooks_;
};

}  // namespace jpr
