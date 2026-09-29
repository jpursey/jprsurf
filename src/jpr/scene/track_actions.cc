// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_actions.h"

#include <optional>

#include "absl/log/check.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/track_cache.h"

namespace jpr {

namespace {

// Returns the grouping for a UI action: Ctrl ignores grouping and ganging.
TrackGrouping GetUiGrouping() {
  return AreModifiersOn(kModCtrl) ? TrackGrouping::kNone
                                  : TrackGrouping::kGrouped;
}

// Toggles the property of the track within the batch.
void ToggleInBatch(TrackBatch& batch, Track* track, TrackBoolProperty property,
                   TrackGrouping grouping) {
  batch.Set(track, property, !track->Get(property), grouping);
}

}  // namespace

TrackActions::TrackActions(TrackFilter filter) : filter_(filter) {}

//------------------------------------------------------------------------------
// Volume and Pan
//------------------------------------------------------------------------------

void TrackActions::UiSetVolume(Track* track, double volume) {
  track->SetVolume(volume, GetUiGrouping());
}

void TrackActions::UiSetPan(Track* track, double pan) {
  track->SetPan(pan, GetUiGrouping());
}

//------------------------------------------------------------------------------
// Select
//------------------------------------------------------------------------------

void TrackActions::UiSelect(Track* track) {
  if (!track->Exists()) {
    return;
  }
  TrackCache& cache = TrackCache::Get();

  if (Track* anchor_track =
          GetOtherAnchorTrack(TrackBoolProperty::kSelected, track);
      anchor_track != nullptr) {
    SelectRange(track, anchor_track, /*same_parent=*/true);
    return;
  }

  if (AreModifiersOn(kModShift)) {
    SelectRange(track, cache.GetLastTouchedTrack(),
                /*same_parent=*/!AreModifiersOn(kModCtrl));
    return;
  }

  if (AreModifiersOn(kModCtrl)) {
    const bool selected = !track->GetSelected();
    TrackBatch().SetSelected(track, selected);
    if (selected) {
      cache.SetLastTouchedTrack(track);
    }
    return;
  }

  if (track->GetSelected() && CountSelectedTracks(nullptr) <= 1) {
    TrackBatch().SetSelected(track, false);
    return;
  }
  track->SelectOnly();
  cache.SetLastTouchedTrack(track);
}

void TrackActions::SelectRange(Track* track, Track* root, bool same_parent) {
  const std::optional<TrackRange> range =
      TrackRange::Between(root, track, filter_, same_parent);
  if (!range.has_value()) {
    return;
  }

  // This covers *all* tracks in the project, so that tracks which are not on
  // the surface still get unselected. Leaving a track selected that the user
  // cannot see is worse than unselecting one they did not aim at.
  TrackBatch batch;
  for (Track* other : TrackCache::Get().GetTracks()) {
    batch.SetSelected(other, range->Contains(other));
  }
}

//------------------------------------------------------------------------------
// Mute, Solo, and Record Arm
//------------------------------------------------------------------------------

void TrackActions::UiToggle(Track* track, TrackBoolProperty property) {
  DCHECK(property != TrackBoolProperty::kSelected);
  if (!track->Exists()) {
    return;
  }
  TrackCache& cache = TrackCache::Get();

  if (Track* anchor_track = GetOtherAnchorTrack(property, track);
      anchor_track != nullptr) {
    SetRange(track, anchor_track, /*same_parent=*/true, property);
    return;
  }

  // "Every track" means all tracks in the project, not just the ones on the
  // surface: a mute the user can neither see nor clear is a bad state to be
  // able to create.
  if (AreModifiersOn(kModAlt)) {
    const bool only_this_track = AreModifiersOn(kModCtrl);
    const bool set_this_track = only_this_track || AreModifiersOn(kModShift);
    TrackBatch batch;

    // With Ctrl, this track goes straight to its final value rather than being
    // turned off and back on.
    for (Track* other : cache.GetTracks()) {
      batch.Set(other, property, only_this_track && other == track);
    }
    if (set_this_track) {
      // With Shift, this track was turned off above, so setting it with
      // grouping turns its grouped tracks back on too.
      batch.Set(
          track, property, true,
          only_this_track ? TrackGrouping::kNone : TrackGrouping::kGrouped);
      cache.SetLastTouchedTrack(track);
    }
    return;
  }

  if (AreModifiersOn(kModShift)) {
    SetRange(track, cache.GetLastTouchedTrack(),
             /*same_parent=*/!AreModifiersOn(kModCtrl), property);
    return;
  }

  if (AreModifiersOn(kModOpt)) {
    TrackBatch batch;
    ToggleInBatch(batch, track, property, TrackGrouping::kNone);
    if (track->GetSelected()) {
      cache.SetLastTouchedTrack(track);
      for (Track* selected : cache.GetSelectedTracks()) {
        if (selected == track) {
          continue;
        }

        // Selected tracks may not be on the surface, so their cached values
        // may be stale.
        selected->Refresh();
        ToggleInBatch(batch, selected, property, TrackGrouping::kNone);
      }
    }
    return;
  }

  TrackBatch batch;
  ToggleInBatch(batch, track, property, GetUiGrouping());
  cache.SetLastTouchedTrack(track);
}

void TrackActions::SetRange(Track* track, Track* root, bool same_parent,
                            TrackBoolProperty property) {
  const std::optional<TrackRange> range =
      TrackRange::Between(root, track, filter_, same_parent);
  if (!range.has_value()) {
    return;
  }

  // The root is in the filter, but may be scrolled off the surface (the last
  // touched track), so its cached value may be stale.
  root->Refresh();
  const bool value = root->Get(property);
  TrackBatch batch;
  for (Track* other : TrackCache::Get().GetTracks()) {
    if (range->Contains(other)) {
      batch.Set(other, property, value);
    }
  }
}

//------------------------------------------------------------------------------
// Anchors
//------------------------------------------------------------------------------

Track* TrackActions::GetOtherAnchorTrack(TrackBoolProperty property,
                                         const Track* track) {
  // An anchor on a deleted track is not held, so the action behaves as usual
  // until the anchor's view releases it.
  Track* anchor_track = GetAnchor(property).Get();
  if (anchor_track == nullptr || anchor_track == track ||
      !anchor_track->Exists()) {
    return nullptr;
  }
  return anchor_track;
}

}  // namespace jpr
