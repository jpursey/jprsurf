// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/reaper_property.h"

#include <cstddef>
#include <iterator>
#include <utility>

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

struct PolledToggle {
  std::string_view name;
  PolledToggleProperty::ReadFunction read;
};

// Indexed by the index in each name.
constexpr PolledToggle kPolledToggles[] = {
    {kAnyTrackSolo, [] { return AnyTrackSolo(nullptr); }},
    {kCanRedo, [] { return Undo_CanRedo2(nullptr) != nullptr; }},
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

void PolledToggleProperty::UpdateState() {
  bool value = read_();
  if (value != value_) {
    value_ = value;
    NotifyChanged();
  }
}

bool PolledToggleProperty::ReadBool() const { return value_; }

}  // namespace jpr
