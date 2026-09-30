// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>

#include "absl/time/time.h"
#include "gb/profile/profiler.h"

namespace jpr {

//==============================================================================
// ReaperProfiler
//
// Profiles JPRSurf for as long as it exists: every point defined with
// gb/profile on the thread that created it, which must be REAPER's UI thread
// (see gb::Profiler).
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
  // Writes the profile to path_.
  void WriteSnapshot() const;

  const std::filesystem::path path_;
  const absl::Time start_time_;
  gb::Profiler profiler_;
};

}  // namespace jpr
