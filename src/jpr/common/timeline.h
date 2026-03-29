// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <cstdint>
#include <optional>

#include "absl/strings/str_format.h"

namespace jpr {

//==============================================================================
// Timeline modes
//==============================================================================

// The different modes that a timeline position can be interpreted in or the
// ruler in REAPER is set to.
enum TimelineMode {
  // Corresponds to the following in REAPER:
  // - "Measure.Beats (minimal)" -- Default when setting.
  // - "Measure.Beats"
  // - "Measure.Fractions"
  kBeats,

  // Corresponds to the following in REAPER:
  // - "Minutes:Seconds (minimal)" -- Default when setting.
  // - "Minutes:Seconds"
  // - "Seconds"
  kTime,

  // Corresponds to the following in REAPER:
  // - "Hours:Minutes:Seconds:Frame" -- Default when setting.
  // - "Absolute Frames"
  kFrames,

  // Corresponds to "Samples" in REAPER.
  kSamples,
};

template <typename Sink>
void AbslStringify(Sink& sink, TimelineMode mode) {
  switch (mode) {
    case TimelineMode::kBeats:
      absl::Format(&sink, "kBeats");
      break;
    case TimelineMode::kTime:
      absl::Format(&sink, "kTime");
      break;
    case TimelineMode::kFrames:
      absl::Format(&sink, "kFrames");
      break;
    case TimelineMode::kSamples:
      absl::Format(&sink, "kSamples");
      break;
  }
}

// Returns the current primary mode of the timeline ruler in REAPER.
TimelineMode GetRulerMode();

// Sets the mode of the timeline ruler in REAPER. This will affect how timeline
// positions are interpreted and displayed in REAPER.
//
// This maintains fine grained ruler settings that are not expressible with
// TimelineMode when switching the ruler display. For instance, if the ruler is
// currently "Measure.Beats" (instead of "Measure.Beats (minimal)"), when the
// ruler is set back to kBeats, it will remember and change to "Measure.Beats".
// If there was no previously stored setting for the ruler, it will switch to
// the defaults as documented above.
void SetRulerMode(TimelineMode mode);

// Returns the current secondary mode of the timeline ruler in REAPER, if there
// is one. Otherwise will return std::nullopt.
std::optional<TimelineMode> GetRulerSecondaryMode();

// Sets the secondary mode of the timeline ruler in REAPER.
//
// This caches current settings in the same way as SetRulerMode.
//
// Note that kBeats is not supported as a secondary mode in REAPER. If setting
// to kBeats, this will instead clear the current mode.
void SetRulerSecondaryMode(TimelineMode mode);

// Clears the secondary mode of the timeline ruler in REAPER, if there is one.
//
// This will also cache the current secondary mode setting as with
// SetRulerSecondaryMode, so that if the secondary mode is set again, it will
// switch to the previously set mode instead of the default.
void ClearRulerSecondaryMode();

// Returns true if the given mode is the current primary mode for the timeline
// ruler.
bool IsCurrentRulerMode(TimelineMode mode);

// Returns true if the given mode is the current secondary mode for the timeline
// ruler.
bool IsCurrentRulerSecondaryMode(TimelineMode mode);

// Returns true if there is a current secondary mode for the timeline ruler.
bool HasCurrentRulerSecondaryMode();

//==============================================================================
// Timline position
//==============================================================================

// Represents a position in the timeline in terms of beats.
struct BeatsPosition {
  bool negative = false;
  int measure = 0;
  int beat = 0;
  int division = 0;
};

template <typename Sink>
void AbslStringify(Sink& sink, const BeatsPosition& value) {
  absl::Format(&sink, "%d.%d.%02d", value.measure, value.beat, value.division);
}

// Represents a position in the timeline in terms of time.
struct TimePosition {
  bool negative = false;
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int milliseconds = 0;
};

template <typename Sink>
void AbslStringify(Sink& sink, const TimePosition& value) {
  if (value.hours == 0) {
    absl::Format(&sink, "%d:%02d.%03d", value.minutes, value.seconds,
                 value.milliseconds);
  } else {
    absl::Format(&sink, "%d:%02d:%02d.%03d", value.hours, value.minutes,
                 value.seconds, value.milliseconds);
  }
}

// Represents a position in the timeline in terms of frames.
struct FramesPosition {
  bool negative = false;
  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  int frames = 0;
};

template <typename Sink>
void AbslStringify(Sink& sink, const FramesPosition& value) {
  absl::Format(&sink, "%02d:%02d:%02d:%02d", value.hours, value.minutes,
               value.seconds, value.frames);
}

// Represents a position in the timeline, which can be interpreted in different
// modes (e.g. frames, samples, time, beats).
class TimelinePosition final {
 public:
  // Returns the current timeline position in REAPER, which is the Edit Cursor
  // position if REAPER is not playing or paused, and the Playback position
  // otherwise.
  static TimelinePosition Get();

  // Returns the playback position in REAPER. This is the current position if
  // REAPER is playing or paused, and is the last playback position otherwise.
  static TimelinePosition GetPlayback();

  // Returns the Edit Cursor position in REAPER.
  static TimelinePosition GetEdit();

  TimelinePosition() = default;
  explicit TimelinePosition(double position) : position_(position) {}
  TimelinePosition(const TimelinePosition&) = default;
  TimelinePosition& operator=(const TimelinePosition&) = default;
  ~TimelinePosition() = default;

  // Gets or sets the core timeline position value.
  double GetValue() const { return position_; }
  void SetValue(double position) { position_ = position; }

  // Converters to interpretations of the timeline position.
  BeatsPosition ToBeats() const;
  TimePosition ToTime() const;
  FramesPosition ToFrames() const;
  int64_t ToSamples() const;

  // Helper to create the default formatted string for this timeline position in
  // the specified mode. This is the same format as the default display for each
  // mode in REAPER.
  std::string ToString(TimelineMode mode) const;

 private:
  double position_ = 0.0;
};

template <typename Sink>
void AbslStringify(Sink& sink, const TimelinePosition& value) {
  absl::Format(&sink, "%f", value.GetValue());
}

}  // namespace jpr
