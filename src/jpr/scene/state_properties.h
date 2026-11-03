// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <memory>
#include <string_view>

#include "absl/types/span.h"
#include "jpr/scene/catalog.h"
#include "jpr/scene/scene.h"
#include "jpr/scene/view_property.h"

namespace jpr {

//==============================================================================
// State property names
//
// Each is described by its row in kStateProperties (state_properties.cc),
// which the catalog reads (see GetStatePropertyInfo()).
//==============================================================================

// Polled toggles (see PolledToggleProperty).
inline constexpr std::string_view kStateAnyTrackSolo =
    kStateName<"any_track_solo">;
inline constexpr std::string_view kStateCanRedo = kStateName<"can_redo">;
inline constexpr std::string_view kStateProjectDirty =
    kStateName<"project_dirty">;
inline constexpr std::string_view kStateAnyItemSelected =
    kStateName<"any_item_selected">;
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
inline constexpr std::string_view kStateSelectedAutoMixed =
    kStateName<"selected_auto_mixed">;
inline constexpr std::string_view kStateAutoOverrideActive =
    kStateName<"auto_override_active">;
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
inline constexpr std::string_view kStateAutoOverrideAnyLatch =
    kStateName<"auto_override_any_latch">;
inline constexpr std::string_view kStateAutoOverrideBypass =
    kStateName<"auto_override_bypass">;

// Timeline positions (see TimelinePositionProperty).
inline constexpr std::string_view kStateTimelinePosition =
    kStateName<"timeline_position">;
inline constexpr std::string_view kStatePlaybackPosition =
    kStateName<"playback_position">;
inline constexpr std::string_view kStateEditPosition =
    kStateName<"edit_position">;

// Rulers (see RulerModeProperty and IsRulerModeProperty, and their secondary
// ruler versions).
inline constexpr std::string_view kStateRulerMode = kStateName<"ruler_mode">;
inline constexpr std::string_view kStateRulerBeats = kStateName<"ruler_beats">;
inline constexpr std::string_view kStateRulerTime = kStateName<"ruler_time">;
inline constexpr std::string_view kStateRulerFrames =
    kStateName<"ruler_frames">;
inline constexpr std::string_view kStateRulerSamples =
    kStateName<"ruler_samples">;
inline constexpr std::string_view kStateSecondaryRulerMode =
    kStateName<"secondary_ruler_mode">;
inline constexpr std::string_view kStateSecondaryRulerTime =
    kStateName<"secondary_ruler_time">;
inline constexpr std::string_view kStateSecondaryRulerFrames =
    kStateName<"secondary_ruler_frames">;
inline constexpr std::string_view kStateSecondaryRulerSamples =
    kStateName<"secondary_ruler_samples">;

//==============================================================================
// State properties
//==============================================================================

// Creates the property with one of the state: names above, or returns null if
// there is none with the name.
std::unique_ptr<ViewProperty> CreateStateProperty(Scene* scene,
                                                  std::string_view name);

// Returns the info of every state property, in the order of the names above.
absl::Span<const PropertyInfo> GetStatePropertyInfo();

}  // namespace jpr
