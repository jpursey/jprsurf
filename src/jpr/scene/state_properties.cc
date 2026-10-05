// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/state_properties.h"

#include <algorithm>
#include <iterator>
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
  std::string_view name;
  std::unique_ptr<ViewProperty> (*create)(Scene* scene, std::string_view name);
};

// A new state property needs a name in the header, and a row here, in the same
// group. The static_asserts below check that the names are unique, and in
// state:.
constexpr StateProperty kStateProperties[] = {
    // Polled toggles.
    {kStateAnyTrackSolo, Create<PolledToggleProperty, IsAnyTrackSolo>},
    {kStateCanRedo, Create<PolledToggleProperty, CanRedo>},
    {kStateProjectDirty, Create<PolledToggleProperty, IsCurrentProjectDirty>},
    {kStateAnyItemSelected, Create<PolledToggleProperty, IsAnyItemSelected>},
    {kStateSelectedAutoTrimRead,
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kTrimRead>>},
    {kStateSelectedAutoRead,
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kRead>>},
    {kStateSelectedAutoTouch,
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kTouch>>},
    {kStateSelectedAutoWrite,
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kWrite>>},
    {kStateSelectedAutoLatch,
     Create<PolledToggleProperty, HasSelectedAutoMode<AutoMode::kLatch>>},
    {kStateSelectedAutoLatchPreview,
     Create<PolledToggleProperty,
            HasSelectedAutoMode<AutoMode::kLatchPreview>>},
    {kStateSelectedAutoMixed,
     Create<PolledToggleProperty, HasMixedSelectedAutoModes>},
    {kStateAutoOverrideActive,
     Create<PolledToggleProperty, IsAutoOverrideActive,
            WriteAutoOverrideActive>},
    {kStateAutoOverrideTrimRead, CreateAutoOverride<AutoOverride::kTrimRead>},
    {kStateAutoOverrideRead, CreateAutoOverride<AutoOverride::kRead>},
    {kStateAutoOverrideTouch, CreateAutoOverride<AutoOverride::kTouch>},
    {kStateAutoOverrideWrite, CreateAutoOverride<AutoOverride::kWrite>},
    {kStateAutoOverrideLatch, CreateAutoOverride<AutoOverride::kLatch>},
    {kStateAutoOverrideLatchPreview,
     CreateAutoOverride<AutoOverride::kLatchPreview>},
    {kStateAutoOverrideAnyLatch,
     Create<PolledToggleProperty,
            IsAutoOverride<AutoOverride::kLatch, AutoOverride::kLatchPreview>>},
    {kStateAutoOverrideBypass, CreateAutoOverride<AutoOverride::kBypass>},

    // Timeline positions.
    {kStateTimelinePosition,
     Create<TimelinePositionProperty,
            TimelinePositionProperty::Source::kCurrent>},
    {kStatePlaybackPosition,
     Create<TimelinePositionProperty,
            TimelinePositionProperty::Source::kPlayback>},
    {kStateEditPosition,
     Create<TimelinePositionProperty, TimelinePositionProperty::Source::kEdit>},

    // Rulers.
    {kStateRulerMode, Create<RulerModeProperty>},
    {kStateRulerBeats, Create<IsRulerModeProperty, TimelineMode::kBeats>},
    {kStateRulerTime, Create<IsRulerModeProperty, TimelineMode::kTime>},
    {kStateRulerFrames, Create<IsRulerModeProperty, TimelineMode::kFrames>},
    {kStateRulerSamples, Create<IsRulerModeProperty, TimelineMode::kSamples>},
    {kStateSecondaryRulerMode, Create<SecondaryRulerModeProperty>},
    {kStateSecondaryRulerTime,
     Create<IsSecondaryRulerModeProperty, TimelineMode::kTime>},
    {kStateSecondaryRulerFrames,
     Create<IsSecondaryRulerModeProperty, TimelineMode::kFrames>},
    {kStateSecondaryRulerSamples,
     Create<IsSecondaryRulerModeProperty, TimelineMode::kSamples>},
};

// Returns true if no two rows in kStateProperties have the same name.
constexpr bool StatePropertyNamesAreUnique() {
  for (auto it = std::begin(kStateProperties); it != std::end(kStateProperties);
       ++it) {
    if (std::ranges::find(it + 1, std::end(kStateProperties), it->name,
                          &StateProperty::name) != std::end(kStateProperties)) {
      return false;
    }
  }
  return true;
}
static_assert(StatePropertyNamesAreUnique(),
              "kStateProperties rows must have unique names");
static_assert(std::ranges::all_of(
                  kStateProperties,
                  [](std::string_view name) {
                    return name.starts_with(kStateNamespace);
                  },
                  &StateProperty::name),
              "kStateProperties rows must be in state:");

}  // namespace

//==============================================================================
// CreateStateProperty
//==============================================================================

std::unique_ptr<ViewProperty> CreateStateProperty(Scene* scene,
                                                  std::string_view name) {
  const StateProperty* row =
      std::ranges::find(kStateProperties, name, &StateProperty::name);
  if (row == std::end(kStateProperties)) {
    return nullptr;
  }
  return row->create(scene, name);
}

}  // namespace jpr
