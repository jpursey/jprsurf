// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/reaper_profiler.h"

#include <cstdio>
#include <filesystem>
#include <string>
#include <utility>

#include "absl/log/log.h"
#include "absl/strings/str_cat.h"
#include "absl/time/clock.h"
#include "gb/profile/profile_timer.h"
#include "jpr/common/local_time.h"
#include "sdk/reaper_plugin.h"

namespace jpr {

namespace {

// The profiler's budget per run is the larger of these.
constexpr absl::Duration kBudgetPerRun = absl::Microseconds(20);
constexpr double kBudgetFraction = 0.01;

//==============================================================================
// Profiled MIDI ports
//
// Each forwards every call to the REAPER object it wraps, timing it. They
// implement every virtual method of their SDK class, so no call is missed.
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

  void start() override {
    gb::ProfileCall<"midi_Input::start"> call;
    port_->start();
  }
  void stop() override {
    gb::ProfileCall<"midi_Input::stop"> call;
    port_->stop();
  }
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
    {
      gb::ProfileCall<"midi_Input::Destroy"> call;
      port_->Destroy();
    }
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
    {
      gb::ProfileCall<"midi_Output::Destroy"> call;
      port_->Destroy();
    }
    delete this;
  }

 private:
  midi_Output* const port_;
};

}  // namespace

//==============================================================================
// ReaperProfiler
//==============================================================================

ReaperProfiler::ReaperProfiler(std::filesystem::path path)
    : path_(std::move(path)),
      start_time_(absl::Now()),
      profiler_({.budget_per_frame = kBudgetPerRun,
                 .budget_fraction = kBudgetFraction}) {}

ReaperProfiler::~ReaperProfiler() { WriteSnapshot(); }

midi_Input* ReaperProfiler::MidiPortHook::Wrap(midi_Input* port) {
  return port != nullptr ? new ProfiledMidiInput(port) : nullptr;
}

midi_Output* ReaperProfiler::MidiPortHook::Wrap(midi_Output* port) {
  return port != nullptr ? new ProfiledMidiOutput(port) : nullptr;
}

void ReaperProfiler::WriteSnapshot() const {
  if (path_.empty()) {
    LOG(ERROR) << "There is nowhere to write the profile.";
    return;
  }
  const absl::Time end_time = absl::Now();
  const std::string snapshot = absl::StrCat(
      "JPRSurf profile\n", "Date:     ",
      absl::FormatTime("%Y-%m-%d %H:%M", start_time_, GetLocalTimeZone()), ", ",
      absl::ToInt64Seconds(end_time - start_time_), "s\n\n",
      profiler_.GetReport());

  std::FILE* file = nullptr;
  if (_wfopen_s(&file, path_.c_str(), L"w") != 0 || file == nullptr) {
    LOG(ERROR) << "Failed to open " << path_.string()
               << " to write the profile.";
    return;
  }
  const bool written = (std::fwrite(snapshot.data(), 1, snapshot.size(),
                                    file) == snapshot.size());
  if (std::fclose(file) != 0 || !written) {
    LOG(ERROR) << "Failed to write the profile to " << path_.string();
    return;
  }
  LOG(INFO) << "Wrote the profile to " << path_.string();
}

}  // namespace jpr
