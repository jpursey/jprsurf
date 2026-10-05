// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "jpr/common/testing/fake_track.h"
#include "jpr/common/track.h"
#include "jpr/common/track_cache.h"

namespace jpr {

// Returns the track cache's track for `track`, or null if the cache doesn't
// have it, as until the cache is refreshed after the track is added.
inline Track* GetCachedTrack(FakeTrack* track) {
  return TrackCache::Get().GetTrack(ToMediaTrack(track));
}

}  // namespace jpr