// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <filesystem>
#include <utility>

#include "absl/time/time.h"
#include "gb/base/function_hook.h"
#include "gb/profile/profile_call_hook.h"
#include "gb/profile/profiler.h"
#include "jpr/common/reaper_api.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

//==============================================================================
// ReaperProfiler
//
// Profiles JPRSurf for as long as it exists: every point defined with
// gb/profile on the thread that created it, which must be REAPER's UI thread
// (see gb::Profiler), every call to a function on the REAPER API list (see
// reaper_api.h), as a call point with the function's name, and every call on a
// MIDI port REAPER creates meanwhile, as midi_Input::<method> or
// midi_Output::<method>.
//
// It is created with the control surface, before its listener, and destroyed
// after it, so a profile covers one surface's whole life.
//
// Only one ReaperProfiler may exist at a time, and it must be destroyed outside
// any timed point.
//==============================================================================

class ReaperProfiler final {
 public:
  // Starts profiling. A run over 8ms logs a warning with its breakdown. When
  // the profiler is destroyed, it writes the profile to the file at `path`,
  // replacing it, and logs a summary.
  explicit ReaperProfiler(std::filesystem::path path);
  ReaperProfiler(const ReaperProfiler&) = delete;
  ReaperProfiler& operator=(const ReaperProfiler&) = delete;
  ~ReaperProfiler();

  // Clears everything recorded so far, such as one time costs of starting up,
  // including values, which must then be set again. It must not be called
  // inside a timed point.
  void Reset() { profiler_.Reset(); }

 private:
  // JPRSurf's and REAPER's self time per run: every call point is REAPER's,
  // and every other timed point is JPRSurf's.
  struct RunSplit {
    absl::Duration jprsurf;
    absl::Duration reaper;
  };
  RunSplit GetRunSplit() const;

  // Hooks CreateMIDIInput() or CreateMIDIOutput(), returning a port that times
  // every call on the port REAPER created, or null if REAPER returned null.
  class MidiPortHook final {
   public:
    MidiPortHook() = default;
    MidiPortHook(const MidiPortHook&) = delete;
    MidiPortHook& operator=(const MidiPortHook&) = delete;
    ~MidiPortHook() = default;

    template <typename Function, typename... Args>
    auto Call(Function original, Args&&... args) {
      return Wrap(original(std::forward<Args>(args)...));
    }

   private:
    // Returns a port that times every call on `port`, and destroys it when it
    // is destroyed, or null if `port` is null.
    static midi_Input* Wrap(midi_Input* port);
    static midi_Output* Wrap(midi_Output* port);
  };

  // Writes the profile to path_.
  void WriteSnapshot(const RunSplit& split) const;

  // Logs a summary of the profile, as a warning if the profiler was over its
  // budget.
  void LogSummary(const RunSplit& split) const;

  const std::filesystem::path path_;
  const absl::Time start_time_;
  gb::Profiler profiler_;

  // The MIDI port hooks are installed over the call hooks, so creating a port
  // is timed as a REAPER call, and wrapping it isn't.
  ReaperApiHooks<gb::ProfileCallHook> hooks_;
  gb::FunctionHook<&CreateMIDIInput, MidiPortHook> midi_input_hook_;
  gb::FunctionHook<&CreateMIDIOutput, MidiPortHook> midi_output_hook_;
};

}  // namespace jpr
