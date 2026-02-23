// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#pragma once

#include "sdk/reaper_plugin.h"

namespace jpr {

// This class iterates through the child tracks of a parent track, in order.
//
// If the parent track is null, it iterates through all top-level tracks. After
// construction, the iterator will be at the first child track (or first top
// level track), if there is one. In the case there is no child (or any top
// level tracks), AtEnd() will return true right away, so it must be checked
// before accessing the properties.
class ChildTrackIterator {
 public:
  ChildTrackIterator(MediaTrack* parent_track);
  ChildTrackIterator(const ChildTrackIterator&) = default;
  ChildTrackIterator& operator=(const ChildTrackIterator&) = default;
  ~ChildTrackIterator() = default;

  MediaTrack* GetTrackId() const { return track_id_; }
  int GetIndex() const { return index_; }
  int GetChildIndex() const { return child_index_; }

  bool AtEnd() const { return track_id_ == nullptr; }
  void Next();

 private:
  MediaTrack* track_id_ = nullptr;
  int track_count_ = 0;
  int depth_ = 0;
  int index_ = 0;
  int child_index_ = 0;
};

}  // namespace jpr
