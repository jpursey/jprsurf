// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_profiler.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_format.h"
#include "absl/time/clock.h"
#include "gb/base/local_time.h"
#include "gb/profile/profile_point.h"
#include "gb/profile/profile_timer.h"
#include "jpr/common/file_system.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

namespace {

// The profiler's budget per run is the larger of these. It costs about 1.5us a
// run (about 120 timed points), so the budget warns if that doubles.
constexpr absl::Duration kBudgetPerRun = absl::Microseconds(3);
constexpr double kBudgetFraction = 0.01;

// A run longer than this logs a warning (see LogSlowRun()).
constexpr absl::Duration kSlowRun = absl::Milliseconds(8);

// Logs a slow run with `report`, its breakdown by point. At most one is logged
// every 5 seconds, so a burst of slow runs doesn't flood the log.
void LogSlowRun(std::string_view report) {
  LOG_EVERY_N_SEC(WARNING, 5) << "Slow run (" << COUNTER << " so far)\n"
                              << report;
}

//==============================================================================
// Profiled MIDI ports
//
// Each forwards every call to the REAPER object it wraps, timing it. They
// implement every virtual method of their SDK class, so no call is missed.
// Opening and closing a port (start(), stop(), and Destroy()) happen once, and
// aren't timed, as the profile is of runs (see ControlSurface).
//
// An input's read buffer isn't wrapped: reading it is a walk over memory, which
// a timer would cost more than, so it is charged to whatever reads it.
//==============================================================================

class ProfiledMidiInput final : public midi_Input {
 public:
  explicit ProfiledMidiInput(midi_Input* port) : port_(port) {}
  ProfiledMidiInput(const ProfiledMidiInput&) = delete;
  ProfiledMidiInput& operator=(const ProfiledMidiInput&) = delete;
  ~ProfiledMidiInput() override = default;

  void start() override { port_->start(); }
  void stop() override { port_->stop(); }
  void SwapBufs(unsigned int timestamp) override {
    gb::ProfileCall<"midi_Input::SwapBufs"> call;
    port_->SwapBufs(timestamp);
  }
  void RunPreNoteTracking(int is_accum) override {
    gb::ProfileCall<"midi_Input::RunPreNoteTracking"> call;
    port_->RunPreNoteTracking(is_accum);
  }
  MIDI_eventlist* GetReadBuf() override {
    gb::ProfileCall<"midi_Input::GetReadBuf"> call;
    return port_->GetReadBuf();
  }
  void SwapBufsPrecise(unsigned int coarse_timestamp,
                       double precise_timestamp) override {
    gb::ProfileCall<"midi_Input::SwapBufsPrecise"> call;
    port_->SwapBufsPrecise(coarse_timestamp, precise_timestamp);
  }
  void Destroy() override {
    port_->Destroy();
    delete this;
  }

 private:
  midi_Input* const port_;
};

class ProfiledMidiOutput final : public midi_Output {
 public:
  explicit ProfiledMidiOutput(midi_Output* port) : port_(port) {}
  ProfiledMidiOutput(const ProfiledMidiOutput&) = delete;
  ProfiledMidiOutput& operator=(const ProfiledMidiOutput&) = delete;
  ~ProfiledMidiOutput() override = default;

  void BeginBlock() override {
    gb::ProfileCall<"midi_Output::BeginBlock"> call;
    port_->BeginBlock();
  }
  void EndBlock(int length, double srate, double curtempo) override {
    gb::ProfileCall<"midi_Output::EndBlock"> call;
    port_->EndBlock(length, srate, curtempo);
  }
  void SendMsg(MIDI_event_t* msg, int frame_offset) override {
    gb::ProfileCall<"midi_Output::SendMsg"> call;
    port_->SendMsg(msg, frame_offset);
  }
  void Send(unsigned char status, unsigned char d1, unsigned char d2,
            int frame_offset) override {
    gb::ProfileCall<"midi_Output::Send"> call;
    port_->Send(status, d1, d2, frame_offset);
  }
  void Destroy() override {
    port_->Destroy();
    delete this;
  }

 private:
  midi_Output* const port_;
};

}  // namespace

std::string FormatRunTime(absl::Duration time) {
  if (time < absl::Milliseconds(1)) {
    return absl::StrFormat("%.1fus", absl::ToDoubleMicroseconds(time));
  }
  return absl::StrFormat("%.1fms", absl::ToDoubleMilliseconds(time));
}

//==============================================================================
// ReaperProfiler
//==============================================================================

ReaperProfiler::ReaperProfiler(Options options)
    : options_(std::move(options)),
      start_time_(absl::Now()),
      profiler_({.budget_per_frame = kBudgetPerRun,
                 .budget_fraction = kBudgetFraction,
                 .slow_frame = kSlowRun,
                 .on_slow_frame = &LogSlowRun}) {}

ReaperProfiler::~ReaperProfiler() {
  const RunSplit split = GetRunSplit();
  WriteSnapshot(split);
  LogSummary(split);
}

ReaperProfiler::RunSplit ReaperProfiler::GetRunSplit() const {
  RunSplit split;
  split.runs = profiler_.GetFrameSummary().frames;
  if (split.runs == 0) {
    return split;
  }
  for (const gb::ProfilePoint& point :
       gb::ProfilePoint::GetRegisteredPoints()) {
    switch (point.GetKind()) {
      case gb::ProfilePoint::Kind::kCall:
        split.reaper += profiler_.GetSelfTime(point.GetName());
        break;
      case gb::ProfilePoint::Kind::kFrame:
      case gb::ProfilePoint::Kind::kScope:
        split.extension += profiler_.GetSelfTime(point.GetName());
        break;
      default:
        break;
    }
  }
  split.extension /= split.runs;
  split.reaper /= split.runs;
  return split;
}

midi_Input* ReaperProfiler::MidiPortHook::Wrap(midi_Input* port) {
  return port != nullptr ? new ProfiledMidiInput(port) : nullptr;
}

midi_Output* ReaperProfiler::MidiPortHook::Wrap(midi_Output* port) {
  return port != nullptr ? new ProfiledMidiOutput(port) : nullptr;
}

std::string ReaperProfiler::FormatRunSplit(const RunSplit& split) const {
  return absl::StrCat(options_.name, " ", FormatRunTime(split.extension),
                      ", REAPER ", FormatRunTime(split.reaper));
}

void ReaperProfiler::WriteSnapshot(const RunSplit& split) const {
  const absl::Time end_time = absl::Now();
  const std::string snapshot = absl::StrCat(
      options_.name, " profile\n", "Build:    ", options_.build, "\n",
      "Date:     ",
      absl::FormatTime("%Y-%m-%d %H:%M", start_time_, gb::GetLocalTimeZone()),
      ", ", absl::ToInt64Seconds(end_time - start_time_), "s\n",
      "Per run:  ", FormatRunSplit(split), "\n\n", profiler_.GetReport());

  if (!GetFileSystem()->WriteFile(options_.path, snapshot)) {
    LOG(ERROR) << "Failed to write the profile to " << options_.path;
    return;
  }
  LOG(INFO) << "Wrote the profile to " << options_.path;
}

void ReaperProfiler::LogSummary(const RunSplit& split) const {
  const gb::Profiler::FrameSummary summary = profiler_.GetFrameSummary();
  const std::string text = absl::StrCat(
      summary.frames, " runs, p50 ", FormatRunTime(summary.p50), ", p99 ",
      FormatRunTime(summary.p99), ", max ", FormatRunTime(summary.max),
      ", avg ", FormatRunTime(summary.average), " (", FormatRunSplit(split),
      "), profiler ", FormatRunTime(summary.profiler_cost), "/run (budget ",
      FormatRunTime(summary.budget), ")");
  if (summary.profiler_cost > summary.budget) {
    LOG(WARNING) << "Profile over budget: " << text;
  } else {
    LOG(INFO) << "Profile: " << text;
  }
}

}  // namespace jpr
