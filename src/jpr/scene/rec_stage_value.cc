// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/rec_stage_value.h"

#include <vector>

#include "absl/log/check.h"
#include "jpr/common/track_cache.h"

namespace jpr {

int RecStageValue::Get(const Track& track) const {
  if (!track.GetRecArm()) {
    return kOff;
  }
  return track.GetRecMode() == TrackRecMode::kNone ? kMonitor : kRecord;
}

void RecStageValue::Set(TrackBatch& batch, Track* track, int value,
                        TrackGrouping grouping) const {
  DCHECK(value >= 0 && value <= GetMaxValue());
  const Stage stage = static_cast<Stage>(value);
  if (grouping == TrackGrouping::kNone) {
    SetOne(batch, track, stage);
    return;
  }

  // As with the other values, a track that already has the stage is left
  // alone, with its group. The track acted on is fresh, as the surface
  // refreshes a track before acting on it.
  if (Get(*track) == value) {
    return;
  }
  for (Track* grouped : GetGroup(track)) {
    SetOne(batch, grouped, stage);
  }
}

std::vector<Track*> RecStageValue::GetGroup(Track* track) const {
  return TrackCache::Get().GetRecArmGroup(track);
}

void RecStageValue::SetOne(TrackBatch& batch, Track* track, Stage stage) {
  switch (stage) {
    case kOff:
      break;
    case kMonitor:
      batch.SetRecMode(track, TrackRecMode::kNone);
      break;
    case kRecord:
      batch.RestoreRecMode(track);
      break;
  }

  // The mode is set first, so the track is never armed to record what it
  // shouldn't.
  batch.SetRecArm(track, stage != kOff);
}

}  // namespace jpr
