// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include <string_view>
#include <vector>

#include "jpr/common/track.h"
#include "jpr/scene/track_actions.h"

namespace jpr {

// A track's record stage, which combines its record arm and record mode:
// - kOff: not armed. Setting it disarms the track, and leaves its mode alone,
//   so a track armed again in REAPER honors it.
// - kMonitor: armed, with the record mode kNone, so the track records nothing.
//   Setting it sets kNone (which remembers the mode it replaces, see
//   Track::GetReplacedRecMode()), and arms the track.
// - kRecord: armed, with any other mode. Setting it restores the replaced mode
//   if the mode is kNone, or kInput if there is none (see
//   TrackBatch::RestoreRecMode()), and arms the track.
//
// REAPER groups the rec arm, but not the record mode, so with kGrouped, Set()
// sets each track a grouped rec arm reaches alone (see GetGroup()), so both
// reach the same tracks.
class RecStageValue final : public TrackValue {
 public:
  enum Stage { kOff, kMonitor, kRecord };

  constexpr RecStageValue() : TrackValue(kNames) {}

  int Get(const Track& track) const override;
  void Set(TrackBatch& batch, Track* track, int value,
           TrackGrouping grouping) const override;
  std::vector<Track*> GetGroup(Track* track) const override;

 private:
  static constexpr std::string_view kNames[] = {"off", "monitor", "record"};

  // Sets the stage of the track alone.
  static void SetOne(TrackBatch& batch, Track* track, Stage stage);
};

}  // namespace jpr
