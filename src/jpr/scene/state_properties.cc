// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/state_properties.h"

#include <memory>

#include "jpr/common/automation.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/test_reset.h"
#include "jpr/common/timeline.h"
#include "jpr/common/track_cache.h"
#include "jpr/scene/polled_toggle_property.h"
#include "jpr/scene/timeline_property.h"

namespace jpr {

namespace {

using Type = ViewProperty::Type;

//------------------------------------------------------------------------------
// Polled toggle read and write functions
//------------------------------------------------------------------------------

bool IsAnyItemSelected() { return CountSelectedMediaItems(nullptr) > 0; }

// This can't be a CommandToggleProperty for kCmdSoloDefeat, as REAPER reports
// no toggle state for that command.
bool IsAnyTrackSolo() { return AnyTrackSolo(nullptr); }

bool CanRedo() { return Undo_CanRedo2(nullptr) != nullptr; }

bool IsCurrentProjectDirty() { return IsProjectDirty(nullptr) != 0; }

template <AutoMode kMode>
bool HasSelectedAutoMode() {
  return TrackCache::Get().HasSelectedAutoMode(kMode);
}

bool HasMixedSelectedAutoModes() {
  return TrackCache::Get().HasMixedSelectedAutoModes();
}

// Returns true if the override is any of kOverrides.
template <AutoOverride... kOverrides>
bool IsAutoOverride() {
  const AutoOverride auto_override = GetAutoOverride();
  return ((auto_override == kOverrides) || ...);
}

// Sets the override to kMode when turned on. Turning it off sets Bypass, or for
// Bypass itself, removes the override.
template <AutoOverride kMode>
void WriteAutoOverride(bool value) {
  constexpr AutoOverride kOff =
      (kMode == AutoOverride::kBypass ? AutoOverride::kNone
                                      : AutoOverride::kBypass);
  SetAutoOverride(value ? kMode : kOff);
}

// The override that turning kStateAutoOverrideActive on restores. Its read
// function is polled every run while mapped, so this includes overrides set
// from REAPER as well as the surface.
AutoOverride g_last_auto_override = AutoOverride::kBypass;
const TestReset kResetLastAutoOverride([] {
  g_last_auto_override = AutoOverride::kBypass;
});

bool IsAutoOverrideActive() {
  const AutoOverride auto_override = GetAutoOverride();
  if (auto_override == AutoOverride::kNone) {
    return false;
  }
  g_last_auto_override = auto_override;
  return true;
}

void WriteAutoOverrideActive(bool value) {
  SetAutoOverride(value ? g_last_auto_override : AutoOverride::kNone);
}

//------------------------------------------------------------------------------
// State property table
//------------------------------------------------------------------------------

// Creates a property of type T, from the scene, the name, and kArgs.
template <typename T, auto... kArgs>
std::unique_ptr<ViewProperty> Create(Scene* scene, std::string_view name) {
  return std::make_unique<T>(scene, name, kArgs...);
}

// A polled toggle for whether the override is kMode, which sets it when written
// (see WriteAutoOverride()).
template <AutoOverride kMode>
std::unique_ptr<ViewProperty> CreateAutoOverride(Scene* scene,
                                                 std::string_view name) {
  return Create<PolledToggleProperty, IsAutoOverride<kMode>,
                WriteAutoOverride<kMode>>(scene, name);
}

struct StateProperty {
  PropertyInfo info;
  std::unique_ptr<ViewProperty> (*create)(Scene* scene, std::string_view name);
};

// A new state property needs a name in the header, and a row here, in the same
// group. The static_assert below checks that the names are unique, and in
// state:.
constexpr StateProperty kStateProperties[] = {
    // Polled toggles.
    {{kStateAnyTrackSolo, Type::kToggle, kReadAccess,
      "True while any track is soloed."},
     Create<PolledToggleProperty, IsAnyTrackSolo>},
    {{kStateCanRedo, Type::kToggle, kReadAccess,
      "True while the current project has anything to redo."},
     Create<PolledToggleProperty, CanRedo>},
    {{kStateProjectDirty, Type::kToggle, kReadAccess,
      "True while the current project has unsaved changes. This is always "
      "false if \"undo/prompt to save\" is disabled in REAPER's preferences."},
     Create<PolledToggleProperty, IsCurrentProjectDirty>},
    {{kStateAnyItemSelected, Type::kToggle, kReadAccess,
      "True while any media items are selected in the current project."},
     Create<PolledToggleProperty, IsAnyItemSelected>},
    {{kStateSelectedAutoTrimRead, Type::kToggle, kReadAccess,
      "True while any selected track, including the master track, is in "
      "Trim/Read automation mode."},
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kTrimRead>>},
    {{kStateSelectedAutoRead, Type::kToggle, kReadAccess,
      "True while any selected track, including the master track, is in Read "
      "automation mode."},
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kRead>>},
    {{kStateSelectedAutoTouch, Type::kToggle, kReadAccess,
      "True while any selected track, including the master track, is in Touch "
      "automation mode."},
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kTouch>>},
    {{kStateSelectedAutoWrite, Type::kToggle, kReadAccess,
      "True while any selected track, including the master track, is in Write "
      "automation mode."},
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kWrite>>},
    {{kStateSelectedAutoLatch, Type::kToggle, kReadAccess,
      "True while any selected track, including the master track, is in Latch "
      "automation mode."},
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kLatch>>},
    {{kStateSelectedAutoLatchPreview, Type::kToggle, kReadAccess,
      "True while any selected track, including the master track, is in Latch "
      "Preview automation mode."},
     Create<PolledToggleProperty,
            HasSelectedAutoMode<AutoMode::kLatchPreview>>},
    {{kStateSelectedAutoMixed, Type::kToggle, kReadAccess,
      "True while the selected tracks, including the master track, are in "
      "more than one automation mode."},
     Create<PolledToggleProperty, HasMixedSelectedAutoModes>},
    {{kStateAutoOverrideActive, Type::kToggle, kReadWriteAccess,
      "True while any global automation override is on. Turning it on "
      "restores the last override it saw on, or Bypass if there hasn't been "
      "one since REAPER started. Turning it off removes the override."},
     Create<PolledToggleProperty, IsAutoOverrideActive,
            WriteAutoOverrideActive>},
    {{kStateAutoOverrideTrimRead, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Trim/Read. Turning it on "
      "sets the override to Trim/Read, and turning it off sets it to Bypass."},
     CreateAutoOverride<AutoOverride::kTrimRead>},
    {{kStateAutoOverrideRead, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Read. Turning it on sets "
      "the override to Read, and turning it off sets it to Bypass."},
     CreateAutoOverride<AutoOverride::kRead>},
    {{kStateAutoOverrideTouch, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Touch. Turning it on sets "
      "the override to Touch, and turning it off sets it to Bypass."},
     CreateAutoOverride<AutoOverride::kTouch>},
    {{kStateAutoOverrideWrite, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Write. Turning it on sets "
      "the override to Write, and turning it off sets it to Bypass."},
     CreateAutoOverride<AutoOverride::kWrite>},
    {{kStateAutoOverrideLatch, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Latch. Turning it on sets "
      "the override to Latch, and turning it off sets it to Bypass."},
     CreateAutoOverride<AutoOverride::kLatch>},
    {{kStateAutoOverrideLatchPreview, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Latch Preview. Turning it "
      "on sets the override to Latch Preview, and turning it off sets it to "
      "Bypass."},
     CreateAutoOverride<AutoOverride::kLatchPreview>},
    {{kStateAutoOverrideAnyLatch, Type::kToggle, kReadAccess,
      "True while the global automation override is Latch or Latch Preview."},
     Create<PolledToggleProperty,
            IsAutoOverride<AutoOverride::kLatch, AutoOverride::kLatchPreview>>},
    {{kStateAutoOverrideBypass, Type::kToggle, kReadWriteAccess,
      "True while the global automation override is Bypass. Turning it on sets "
      "Bypass, and turning it off removes the override."},
     CreateAutoOverride<AutoOverride::kBypass>},

    // Timeline positions.
    {{kStateTimelinePosition, Type::kTimelinePosition, kReadAccess,
      "The playback position while playing or paused, and otherwise the edit "
      "cursor's position."},
     Create<TimelinePositionProperty,
            TimelinePositionProperty::Source::kCurrent>},
    {{kStatePlaybackPosition, Type::kTimelinePosition, kReadAccess,
      "The playback position."},
     Create<TimelinePositionProperty,
            TimelinePositionProperty::Source::kPlayback>},
    {{kStateEditPosition, Type::kTimelinePosition, kReadAccess,
      "The edit cursor's position."},
     Create<TimelinePositionProperty, TimelinePositionProperty::Source::kEdit>},

    // Rulers.
    {{kStateRulerMode, Type::kEnumerated, kReadWriteAccess,
      "The primary ruler's mode: 0 for beats, 1 for time, 2 for frames, and 3 "
      "for samples."},
     Create<RulerModeProperty>},
    {{kStateRulerBeats, Type::kToggle, kReadWriteAccess,
      "True while the primary ruler is in beats. Turning it on sets the "
      "primary ruler to beats, and turning it off does nothing, as the "
      "primary ruler always has a mode."},
     Create<IsRulerModeProperty, TimelineMode::kBeats>},
    {{kStateRulerTime, Type::kToggle, kReadWriteAccess,
      "True while the primary ruler is in time. Turning it on sets the "
      "primary ruler to time, and turning it off does nothing."},
     Create<IsRulerModeProperty, TimelineMode::kTime>},
    {{kStateRulerFrames, Type::kToggle, kReadWriteAccess,
      "True while the primary ruler is in frames. Turning it on sets the "
      "primary ruler to frames, and turning it off does nothing."},
     Create<IsRulerModeProperty, TimelineMode::kFrames>},
    {{kStateRulerSamples, Type::kToggle, kReadWriteAccess,
      "True while the primary ruler is in samples. Turning it on sets the "
      "primary ruler to samples, and turning it off does nothing."},
     Create<IsRulerModeProperty, TimelineMode::kSamples>},
    {{kStateSecondaryRulerMode, Type::kEnumerated, kReadWriteAccess,
      "The secondary ruler's mode: 0 for none, 1 for time, 2 for frames, and "
      "3 for samples."},
     Create<SecondaryRulerModeProperty>},
    {{kStateSecondaryRulerTime, Type::kToggle, kReadWriteAccess,
      "True while the secondary ruler is in time. Turning it on sets the "
      "secondary ruler to time, and turning it off clears the secondary "
      "ruler."},
     Create<IsSecondaryRulerModeProperty, TimelineMode::kTime>},
    {{kStateSecondaryRulerFrames, Type::kToggle, kReadWriteAccess,
      "True while the secondary ruler is in frames. Turning it on sets the "
      "secondary ruler to frames, and turning it off clears the secondary "
      "ruler."},
     Create<IsSecondaryRulerModeProperty, TimelineMode::kFrames>},
    {{kStateSecondaryRulerSamples, Type::kToggle, kReadWriteAccess,
      "True while the secondary ruler is in samples. Turning it on sets the "
      "secondary ruler to samples, and turning it off clears the secondary "
      "ruler."},
     Create<IsSecondaryRulerModeProperty, TimelineMode::kSamples>},
};

constexpr auto kStatePropertyInfo = GetPropertyInfo(kStateProperties);
static_assert(AreValidProperties(kStatePropertyInfo, kStateNamespace),
              "kStateProperties rows must have unique names in state:");

}  // namespace

//==============================================================================
// State properties
//==============================================================================

std::unique_ptr<ViewProperty> CreateStateProperty(Scene* scene,
                                                  std::string_view name) {
  const StateProperty* row = FindRow(kStateProperties, name);
  return row != nullptr ? row->create(scene, name) : nullptr;
}

absl::Span<const PropertyInfo> GetStatePropertyInfo() {
  return kStatePropertyInfo;
}

}  // namespace jpr
