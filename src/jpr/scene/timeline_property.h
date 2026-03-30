// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>

#include "jpr/common/timeline.h"
#include "jpr/scene/scene_state_property.h"

namespace jpr {

//==============================================================================
// Property name constants
//==============================================================================

inline constexpr std::string_view kTimelinePosition = "timeline_position";
inline constexpr std::string_view kPlaybackPosition = "playback_position";
inline constexpr std::string_view kEditPosition = "edit_position";

inline constexpr std::string_view kRulerMode = "ruler_mode";
inline constexpr std::string_view kRulerBeats = "ruler_beats";
inline constexpr std::string_view kRulerTime = "ruler_time";
inline constexpr std::string_view kRulerFrames = "ruler_frames";
inline constexpr std::string_view kRulerSamples = "ruler_samples";

inline constexpr std::string_view kRuler2Mode = "ruler2_mode";
inline constexpr std::string_view kRuler2Time = "ruler2_time";
inline constexpr std::string_view kRuler2Frames = "ruler2_frames";
inline constexpr std::string_view kRuler2Samples = "ruler2_samples";

//==============================================================================
// TimelinePositionProperty
//==============================================================================

// A read-only property that tracks a timeline position in REAPER. The specific
// position tracked depends on the PositionSource.
class TimelinePositionProperty final : public SceneStateProperty {
 public:
  // Determines which REAPER timeline position this property tracks.
  enum class Source {
    // The current timeline position: the playback position if playing or
    // paused, and the edit cursor position otherwise.
    kCurrent,

    // The playback position.
    kPlayback,

    // The edit cursor position.
    kEdit,
  };

  TimelinePositionProperty(Scene* scene, std::string_view name, Source source)
      : SceneStateProperty(scene, name, Type::kTimelinePosition),
        source_(source) {}
  ~TimelinePositionProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  TimelinePosition ReadTimelinePosition() const override { return position_; }

 private:
  Source source_;
  std::optional<TimelineMode> mode_;
  TimelinePosition position_;
};

//==============================================================================
// IsRulerModeProperty
//==============================================================================

// A toggle property that tracks whether a specific primary ruler mode is active
// in REAPER. Writing true sets the ruler to that mode; writing false is a
// no-op since there must always be an active primary mode.
class IsRulerModeProperty final : public SceneStateProperty {
 public:
  IsRulerModeProperty(Scene* scene, std::string_view name, TimelineMode mode)
      : SceneStateProperty(scene, name, Type::kToggle), mode_(mode) {}
  ~IsRulerModeProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override { return value_; }
  void WriteBool(bool value) override;

 private:
  TimelineMode mode_;
  bool value_ = false;
};

//==============================================================================
// IsSecondaryRulerModeProperty
//==============================================================================

// A toggle property that tracks whether a specific secondary ruler mode is
// active in REAPER. Writing true sets the secondary ruler to that mode; writing
// false clears the secondary ruler mode.
class IsSecondaryRulerModeProperty final : public SceneStateProperty {
 public:
  IsSecondaryRulerModeProperty(Scene* scene, std::string_view name,
                               TimelineMode mode)
      : SceneStateProperty(scene, name, Type::kToggle), mode_(mode) {}
  ~IsSecondaryRulerModeProperty() override = default;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  bool ReadBool() const override { return value_; }
  void WriteBool(bool value) override;

 private:
  TimelineMode mode_;
  bool value_ = false;
};

//==============================================================================
// RulerModeProperty
//==============================================================================

// An enumerated property that tracks and controls the primary ruler mode in
// REAPER. The value is a TimelineMode integer (0=kBeats, 1=kTime, 2=kFrames,
// 3=kSamples).
class RulerModeProperty final : public SceneStateProperty {
 public:
  RulerModeProperty(Scene* scene, std::string_view name)
      : SceneStateProperty(scene, name, Type::kEnumerated) {}
  ~RulerModeProperty() override = default;

  // Overrides from ViewProperty.
  int GetMaxValue() const override { return 3; }
  std::string GetText() const override;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  int ReadInt() const override { return value_; }
  void WriteInt(int value) override;

 private:
  int value_ = 0;
};

//==============================================================================
// SecondaryRulerModeProperty
//==============================================================================

// An enumerated property that tracks and controls the secondary ruler mode in
// REAPER. The value is a TimelineMode integer (0=kBeats/none, 1=kTime,
// 2=kFrames, 3=kSamples). Setting to 0 clears the secondary ruler mode.
class SecondaryRulerModeProperty final : public SceneStateProperty {
 public:
  SecondaryRulerModeProperty(Scene* scene, std::string_view name)
      : SceneStateProperty(scene, name, Type::kEnumerated) {}
  ~SecondaryRulerModeProperty() override = default;

  // Overrides from ViewProperty.
  int GetMaxValue() const override { return 3; }
  std::string GetText() const override;

  // Overrides from SceneStateProperty.
  void UpdateState() override;

 protected:
  // Overrides from ViewProperty.
  int ReadInt() const override { return value_; }
  void WriteInt(int value) override;

 private:
  int value_ = 0;
};

}  // namespace jpr
