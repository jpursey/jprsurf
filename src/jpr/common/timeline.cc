// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/timeline.h"

#include "absl/log/log.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_cat.h"
#include "absl/strings/str_split.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

//==============================================================================
// Timeline modes
//==============================================================================

namespace {

// Actions IDs for the different time modes of the ruler. These are used to
// query and switch the ruler to the specified mode.
constexpr int kRulerBeatsModes[] = {
    41916,  // "Measure.Beats (minimal)".
    40367,  // "Measure.Beats".
    43205,  // "Measure.Fractions".
};
constexpr int kRulerTimeModes[] = {
    43204,  // "Minutes:Seconds (minimal)".
    40365,  // "Minutes:Seconds".
    40368,  // "Seconds".
};
constexpr int kRulerFramesModes[] = {
    40370,  // "Hours:Minutes:Seconds:Frame".
    41973,  // "Absolute Frames".
};
constexpr int kRulerSamplesMode = 40369;  // "Samples".

// Actions IDs for the different secondary time modes of the ruler. These are
// used to query and switch the ruler to the specified mode.
constexpr int kRulerSecondaryNoneMode = 42360;  // No secondary mode.
constexpr int kRulerSecondaryTimeModes[] = {
    43705,  // "Minutes:Seconds (minimal)".
    42361,  // "Minutes:Seconds".
    42362,  // "Seconds".
};
constexpr int kRulerSecondaryFramesModes[] = {
    42364,  // "Hours:Minutes:Seconds:Frame".
    42365,  // "Absolute Frames".
};
constexpr int kRulerSecondarySamplesMode = 42363;  // "Samples".

// Cached values for the last selected time mode for each TimelineMode.
int g_last_ruler_beats_mode = kRulerBeatsModes[0];
int g_last_ruler_time_mode = kRulerTimeModes[0];
int g_last_ruler_frames_mode = kRulerFramesModes[0];
int g_last_ruler_secondary_time_mode = kRulerSecondaryTimeModes[0];
int g_last_ruler_secondary_frames_mode = kRulerSecondaryFramesModes[0];

}  // namespace

TimelineMode GetRulerMode() {
  for (int beats_mode : kRulerBeatsModes) {
    if (GetToggleCommandState(beats_mode) > 0) {
      g_last_ruler_beats_mode = beats_mode;
      return TimelineMode::kBeats;
    }
  }
  for (int time_mode : kRulerTimeModes) {
    if (GetToggleCommandState(time_mode) > 0) {
      g_last_ruler_time_mode = time_mode;
      return TimelineMode::kTime;
    }
  }
  for (int frames_mode : kRulerFramesModes) {
    if (GetToggleCommandState(frames_mode) > 0) {
      g_last_ruler_frames_mode = frames_mode;
      return TimelineMode::kFrames;
    }
  }
  if (GetToggleCommandState(kRulerSamplesMode) > 0) {
    return TimelineMode::kSamples;
  }
  // This should never happen, but if it does, default to beats mode.
  return TimelineMode::kBeats;
}

void SetRulerMode(TimelineMode mode) {
  // Setting the mode is a toggle, so we don't want to trigger the command if
  // we're already in the correct mode.
  if (GetRulerMode() == mode) {
    return;
  }
  switch (mode) {
    case TimelineMode::kBeats:
      Main_OnCommand(g_last_ruler_beats_mode, 0);
      break;
    case TimelineMode::kTime:
      Main_OnCommand(g_last_ruler_time_mode, 0);
      break;
    case TimelineMode::kFrames:
      Main_OnCommand(g_last_ruler_frames_mode, 0);
      break;
    case TimelineMode::kSamples:
      Main_OnCommand(kRulerSamplesMode, 0);
      break;
  }
}

std::optional<TimelineMode> GetRulerSecondaryMode() {
  for (int time_mode : kRulerSecondaryTimeModes) {
    if (GetToggleCommandState(time_mode) > 0) {
      g_last_ruler_secondary_time_mode = time_mode;
      return TimelineMode::kTime;
    }
  }
  for (int frames_mode : kRulerSecondaryFramesModes) {
    if (GetToggleCommandState(frames_mode) > 0) {
      g_last_ruler_secondary_frames_mode = frames_mode;
      return TimelineMode::kFrames;
    }
  }
  if (GetToggleCommandState(kRulerSecondarySamplesMode) > 0) {
    return TimelineMode::kSamples;
  }
  // It isn't a known secondary mode, or it is none.
  return std::nullopt;
}

void SetRulerSecondaryMode(TimelineMode mode) {
  // Setting the mode is a toggle, so we don't want to trigger the command if
  // we're already in the correct mode.
  std::optional<TimelineMode> current_secondary_mode = GetRulerSecondaryMode();
  if (current_secondary_mode.has_value() && *current_secondary_mode == mode) {
    return;
  }

  switch (mode) {
    case TimelineMode::kBeats:
      // kBeats is not a valid secondary mode in REAPER, so instead we clear the
      // current secondary mode.
      ClearRulerSecondaryMode();
      break;
    case TimelineMode::kTime:
      Main_OnCommand(g_last_ruler_secondary_time_mode, 0);
      break;
    case TimelineMode::kFrames:
      Main_OnCommand(g_last_ruler_secondary_frames_mode, 0);
      break;
    case TimelineMode::kSamples:
      Main_OnCommand(kRulerSecondarySamplesMode, 0);
      break;
  }
}

void ClearRulerSecondaryMode() {
  // Clearing the secondary mode is a toggle, so we don't want to trigger the
  // command if there isn't currently a secondary mode.
  if (!GetRulerSecondaryMode().has_value()) {
    return;
  }
  Main_OnCommand(kRulerSecondaryNoneMode, 0);
}

bool IsCurrentRulerMode(TimelineMode mode) { return GetRulerMode() == mode; }

bool IsCurrentRulerSecondaryMode(TimelineMode mode) {
  return GetRulerSecondaryMode().has_value() &&
         *GetRulerSecondaryMode() == mode;
}

bool HasCurrentRulerSecondaryMode() {
  return GetRulerSecondaryMode().has_value();
}

//==============================================================================
// Timeline position
//==============================================================================

TimelinePosition TimelinePosition::Get() {
  int play_state = GetPlayState();
  // Bit mask 1 is playing and bit mask 2 is paused, so check if either of these
  // are set.
  if ((play_state & 3) != 0) {
    return GetPlayback();
  }
  return GetEdit();
}

TimelinePosition TimelinePosition::GetPlayback() {
  return TimelinePosition(GetPlayPosition());
}

TimelinePosition TimelinePosition::GetEdit() {
  return TimelinePosition(GetCursorPosition());
}

BeatsPosition TimelinePosition::ToBeats() const {
  char timestr[100];
  // Two is "Beats" mode.
  format_timestr_pos(position_, timestr, std::size(timestr),
                     /*modeoverride=*/2);

  bool negative = (timestr[0] == '-');
  const char* timestr_start = (negative ? timestr + 1 : timestr);
  std::vector<std::string_view> beat_parts =
      absl::StrSplit(timestr_start, absl::ByAnyChar("."));
  if (beat_parts.size() != 3) {
    LOG(ERROR) << "Unexpected beat format: " << timestr;
    return {};
  }

  BeatsPosition beats = {};
  beats.negative = negative;
  if (!absl::SimpleAtoi(beat_parts[0], &beats.measure) ||
      !absl::SimpleAtoi(beat_parts[1], &beats.beat) ||
      !absl::SimpleAtoi(beat_parts[2], &beats.division)) {
    LOG(ERROR) << "Unexpected beat format: " << timestr;
    return {};
  }
  return beats;
}

TimePosition TimelinePosition::ToTime() const {
  char timestr[100];
  // Zero is "Time" mode.
  format_timestr_pos(position_, timestr, std::size(timestr),
                     /*modeoverride=*/0);

  bool negative = (timestr[0] == '-');
  const char* timestr_start = (negative ? timestr + 1 : timestr);
  std::vector<std::string_view> time_parts =
      absl::StrSplit(timestr_start, absl::ByAnyChar(":."));
  // There always is at least three parts (minutes, seconds, and milliseconds),
  // with hours only being present if the position is long enough.
  if (time_parts.size() < 3 || time_parts.size() > 4) {
    LOG(ERROR) << "Unexpected time format: " << timestr;
    return {};
  }

  int minutes_index = (time_parts.size() == 4 ? 1 : 0);
  TimePosition time = {};
  time.negative = negative;
  if (time_parts.size() == 4) {
    if (!absl::SimpleAtoi(time_parts[0], &time.hours)) {
      LOG(ERROR) << "Unexpected time format: " << timestr;
      return {};
    }
  }
  if (!absl::SimpleAtoi(time_parts[minutes_index], &time.minutes) ||
      !absl::SimpleAtoi(time_parts[minutes_index + 1], &time.seconds) ||
      !absl::SimpleAtoi(time_parts[minutes_index + 2], &time.milliseconds)) {
    LOG(ERROR) << "Unexpected time format: " << timestr;
    return {};
  }
  return time;
}

FramesPosition TimelinePosition::ToFrames() const {
  char timestr[100];
  // Five is "Hours:Minutes:Seconds:Frame" mode.
  format_timestr_pos(position_, timestr, std::size(timestr),
                     /*modeoverride=*/5);

  bool negative = (timestr[0] == '-');
  const char* timestr_start = (negative ? timestr + 1 : timestr);
  std::vector<std::string_view> time_parts =
      absl::StrSplit(timestr_start, absl::ByAnyChar(":"));
  if (time_parts.size() != 4) {
    LOG(ERROR) << "Unexpected time format: " << timestr;
    return {};
  }

  FramesPosition frames = {};
  frames.negative = negative;
  if (!absl::SimpleAtoi(time_parts[0], &frames.hours) ||
      !absl::SimpleAtoi(time_parts[1], &frames.minutes) ||
      !absl::SimpleAtoi(time_parts[2], &frames.seconds) ||
      !absl::SimpleAtoi(time_parts[3], &frames.frames)) {
    LOG(ERROR) << "Unexpected time format: " << timestr;
    return {};
  }
  return frames;
}

int64_t TimelinePosition::ToSamples() const {
  char timestr[100];
  // Four is "Samples" mode.
  format_timestr_pos(position_, timestr, std::size(timestr),
                     /*modeoverride=*/4);
  int64_t samples = 0;
  if (!absl::SimpleAtoi(timestr, &samples)) {
    LOG(ERROR) << "Unexpected time format: " << timestr;
    return 0;
  }
  return samples;
}

std::string TimelinePosition::ToString(TimelineMode mode) const {
  switch (mode) {
    case TimelineMode::kBeats:
      return absl::StrCat(ToBeats());
    case TimelineMode::kTime:
      return absl::StrCat(ToTime());
    case TimelineMode::kFrames:
      return absl::StrCat(ToFrames());
    case TimelineMode::kSamples:
      return absl::StrCat(ToSamples());
  }
  // Should never get here, but if we do, just return the raw position value.
  return absl::StrCat(position_);
}

}  // namespace jpr
