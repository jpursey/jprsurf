// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string_view>

#include "jpr/common/prefixed_name.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// State property names
//==============================================================================

// The name of a property in the state: namespace.
template <StringLiteral Name>
inline constexpr std::string_view kStateName =
    kPrefixedName<kStateNamespace, Name>;

//------------------------------------------------------------------------------
// Polled toggles (see PolledToggleProperty)
//------------------------------------------------------------------------------

// True while any track is soloed.
inline constexpr std::string_view kStateAnyTrackSolo =
    kStateName<"any_track_solo">;

// True while the current project has anything to redo.
inline constexpr std::string_view kStateCanRedo = kStateName<"can_redo">;

// True while the current project has unsaved changes. This is always false if
// "undo/prompt to save" is disabled in REAPER's preferences.
inline constexpr std::string_view kStateProjectDirty =
    kStateName<"project_dirty">;

// True while any media items are selected in the current project.
inline constexpr std::string_view kStateAnyItemSelected =
    kStateName<"any_item_selected">;

// True while any selected track, including the master track, is in the
// automation mode.
inline constexpr std::string_view kStateSelectedAutoTrimRead =
    kStateName<"selected_auto_trim_read">;
inline constexpr std::string_view kStateSelectedAutoRead =
    kStateName<"selected_auto_read">;
inline constexpr std::string_view kStateSelectedAutoTouch =
    kStateName<"selected_auto_touch">;
inline constexpr std::string_view kStateSelectedAutoWrite =
    kStateName<"selected_auto_write">;
inline constexpr std::string_view kStateSelectedAutoLatch =
    kStateName<"selected_auto_latch">;
inline constexpr std::string_view kStateSelectedAutoLatchPreview =
    kStateName<"selected_auto_latch_preview">;

// True while the selected tracks, including the master track, are in more than
// one automation mode. As each track is in exactly one mode, this is also true
// exactly when every lit kStateSelectedAuto* covers only some of the selection.
inline constexpr std::string_view kStateSelectedAutoMixed =
    kStateName<"selected_auto_mixed">;

// The global automation override for the current project (see AutoOverride).
// Writing these sets the override.
//
// True while any override is on. Turning it on restores the last override it
// saw on, or Bypass if there hasn't been one since REAPER started. Turning it
// off removes the override.
inline constexpr std::string_view kStateAutoOverrideActive =
    kStateName<"auto_override_active">;

// True while the override is the mode. Turning one on sets the override to its
// mode, and turning it off sets the override to Bypass.
inline constexpr std::string_view kStateAutoOverrideTrimRead =
    kStateName<"auto_override_trim_read">;
inline constexpr std::string_view kStateAutoOverrideRead =
    kStateName<"auto_override_read">;
inline constexpr std::string_view kStateAutoOverrideTouch =
    kStateName<"auto_override_touch">;
inline constexpr std::string_view kStateAutoOverrideWrite =
    kStateName<"auto_override_write">;
inline constexpr std::string_view kStateAutoOverrideLatch =
    kStateName<"auto_override_latch">;
inline constexpr std::string_view kStateAutoOverrideLatchPreview =
    kStateName<"auto_override_latch_preview">;

// True while the override is Latch or Latch Preview. This is read-only.
inline constexpr std::string_view kStateAutoOverrideAnyLatch =
    kStateName<"auto_override_any_latch">;

// True while the override is Bypass. Turning it on sets Bypass, and turning it
// off removes the override.
inline constexpr std::string_view kStateAutoOverrideBypass =
    kStateName<"auto_override_bypass">;

//------------------------------------------------------------------------------
// Timeline positions (see TimelinePositionProperty)
//------------------------------------------------------------------------------

// The playback position while playing or paused, and otherwise the edit cursor.
inline constexpr std::string_view kStateTimelinePosition =
    kStateName<"timeline_position">;

// The playback position, and the edit cursor position.
inline constexpr std::string_view kStatePlaybackPosition =
    kStateName<"playback_position">;
inline constexpr std::string_view kStateEditPosition =
    kStateName<"edit_position">;

//------------------------------------------------------------------------------
// Rulers (see RulerModeProperty and IsRulerModeProperty, and their secondary
// ruler versions)
//------------------------------------------------------------------------------

// The primary ruler's mode, and whether it is each mode.
inline constexpr std::string_view kStateRulerMode = kStateName<"ruler_mode">;
inline constexpr std::string_view kStateRulerBeats = kStateName<"ruler_beats">;
inline constexpr std::string_view kStateRulerTime = kStateName<"ruler_time">;
inline constexpr std::string_view kStateRulerFrames =
    kStateName<"ruler_frames">;
inline constexpr std::string_view kStateRulerSamples =
    kStateName<"ruler_samples">;

// The secondary ruler's mode, and whether it is each mode.
inline constexpr std::string_view kStateSecondaryRulerMode =
    kStateName<"secondary_ruler_mode">;
inline constexpr std::string_view kStateSecondaryRulerTime =
    kStateName<"secondary_ruler_time">;
inline constexpr std::string_view kStateSecondaryRulerFrames =
    kStateName<"secondary_ruler_frames">;
inline constexpr std::string_view kStateSecondaryRulerSamples =
    kStateName<"secondary_ruler_samples">;

//==============================================================================
// CreateStateProperty
//==============================================================================

// Creates the property with one of the state: names above, or returns null if
// there is none with the name.
std::unique_ptr<ViewProperty> CreateStateProperty(Scene* scene,
                                                  std::string_view name);

}  // namespace jpr
