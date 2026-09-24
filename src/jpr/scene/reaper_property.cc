// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/reaper_property.h"

#include <cstddef>
#include <iterator>
#include <utility>

#include "jpr/common/automation.h"
#include "jpr/common/track_cache.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

//==============================================================================
// CommandActionProperty
//==============================================================================

void CommandActionProperty::TriggerAction() { Main_OnCommand(command_id_, 0); }

//==============================================================================
// CommandToggleProperty
//==============================================================================

void CommandToggleProperty::UpdateState() {
  bool value = (GetToggleCommandState(command_id_) > 0);
  if (value != value_) {
    value_ = value;
    NotifyChanged();
  }
}

bool CommandToggleProperty::ReadBool() const { return value_; }

void CommandToggleProperty::WriteBool(bool value) {
  if (value != value_) {
    Main_OnCommand(command_id_, 0);
    UpdateState();
  }
}

//==============================================================================
// PolledToggleProperty
//==============================================================================

namespace {

template <AutoMode kMode>
bool HasSelectedAutoMode() {
  return TrackCache::Get().HasSelectedAutoMode(kMode);
}

template <AutoOverride kOverride>
bool IsAutoOverride() {
  return GetAutoOverride() == kOverride;
}

// Sets the override to kOn when turned on, and kOff when turned off.
template <AutoOverride kOn, AutoOverride kOff>
void WriteAutoOverride(bool value) {
  SetAutoOverride(value ? kOn : kOff);
}

struct PolledToggle {
  std::string_view name;
  PolledToggleProperty::ReadFunction read;
  PolledToggleProperty::WriteFunction write = nullptr;
};

// Indexed by the index in each name.
constexpr PolledToggle kPolledToggles[] = {
    {kStateAnyTrackSolo, [] { return AnyTrackSolo(nullptr); }},
    {kStateCanRedo, [] { return Undo_CanRedo2(nullptr) != nullptr; }},
    {kStateProjectDirty, [] { return IsProjectDirty(nullptr) != 0; }},
    {kStateAnyItemSelected,
     [] { return CountSelectedMediaItems(nullptr) > 0; }},
    {kStateSelectedAutoTrimRead, HasSelectedAutoMode<AutoMode::kTrimRead>},
    {kStateSelectedAutoRead, HasSelectedAutoMode<AutoMode::kRead>},
    {kStateSelectedAutoTouch, HasSelectedAutoMode<AutoMode::kTouch>},
    {kStateSelectedAutoWrite, HasSelectedAutoMode<AutoMode::kWrite>},
    {kStateSelectedAutoLatch, HasSelectedAutoMode<AutoMode::kLatch>},
    {kStateSelectedAutoLatchPreview,
     HasSelectedAutoMode<AutoMode::kLatchPreview>},
    {kStateSelectedAutoMixed,
     [] { return TrackCache::Get().HasMixedSelectedAutoModes(); }},
    {kStateAutoOverrideActive,
     [] { return GetAutoOverride() != AutoOverride::kNone; },
     [](bool value) {
       SetAutoOverride(value ? GetLastAutoOverride() : AutoOverride::kNone);
     }},
    {kStateAutoOverrideTrimRead, IsAutoOverride<AutoOverride::kTrimRead>,
     WriteAutoOverride<AutoOverride::kTrimRead, AutoOverride::kBypass>},
    {kStateAutoOverrideRead, IsAutoOverride<AutoOverride::kRead>,
     WriteAutoOverride<AutoOverride::kRead, AutoOverride::kBypass>},
    {kStateAutoOverrideTouch, IsAutoOverride<AutoOverride::kTouch>,
     WriteAutoOverride<AutoOverride::kTouch, AutoOverride::kBypass>},
    {kStateAutoOverrideWrite, IsAutoOverride<AutoOverride::kWrite>,
     WriteAutoOverride<AutoOverride::kWrite, AutoOverride::kBypass>},
    {kStateAutoOverrideLatch, IsAutoOverride<AutoOverride::kLatch>,
     WriteAutoOverride<AutoOverride::kLatch, AutoOverride::kBypass>},
    {kStateAutoOverrideLatchPreview,
     IsAutoOverride<AutoOverride::kLatchPreview>,
     WriteAutoOverride<AutoOverride::kLatchPreview, AutoOverride::kBypass>},
    {kStateAutoOverrideBypass, IsAutoOverride<AutoOverride::kBypass>,
     WriteAutoOverride<AutoOverride::kBypass, AutoOverride::kNone>},
};

// Returns true if every row in kPolledToggles is named kStateName<index> for
// its own index.
template <size_t... kIndices>
constexpr bool PolledToggleNamesMatchIndices(std::index_sequence<kIndices...>) {
  return ((kPolledToggles[kIndices].name == kStateName<kIndices>) && ...);
}
static_assert(PolledToggleNamesMatchIndices(
                  std::make_index_sequence<std::size(kPolledToggles)>()),
              "kPolledToggles rows must be named kStateName<index> in order");

}  // namespace

PolledToggleProperty::ReadFunction PolledToggleProperty::GetReadFunction(
    int index) {
  if (index < 0 || index >= static_cast<int>(std::size(kPolledToggles))) {
    return nullptr;
  }
  return kPolledToggles[index].read;
}

PolledToggleProperty::WriteFunction PolledToggleProperty::GetWriteFunction(
    int index) {
  if (index < 0 || index >= static_cast<int>(std::size(kPolledToggles))) {
    return nullptr;
  }
  return kPolledToggles[index].write;
}

void PolledToggleProperty::UpdateState() {
  bool value = read_();
  if (value != value_) {
    value_ = value;
    NotifyChanged();
  }
}

bool PolledToggleProperty::ReadBool() const { return value_; }

void PolledToggleProperty::WriteBool(bool value) {
  if (write_ == nullptr || value == value_) {
    return;
  }
  write_(value);
  UpdateState();
}

}  // namespace jpr
