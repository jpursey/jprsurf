// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "child_track_iterator.h"

#include "absl/log/log.h"
#include "reaper_plugin_functions.h"

namespace jpr {

ChildTrackIterator::ChildTrackIterator(MediaTrack* parent_track_id) {
  if (parent_track_id != nullptr) {
    // If the parent track has children, this will be 1, otherwise it will be
    // less than one. We set this to be one less, so that we won't find any
    // tracks if there are no children of the parent, otherwise we will
    // iterate until the folder depth goes below zero.
    depth_ = static_cast<int>(
                 GetMediaTrackInfo_Value(parent_track_id, "I_FOLDERDEPTH")) -
             1;
    if (depth_ < 0) {
      return;
    }

    // The track number is 1-based, and we want the track index immediately
    // following the parent track, so getting the track number of the parent
    // track is this index (parent track index plus one).
    index_ = static_cast<int>(
        GetMediaTrackInfo_Value(parent_track_id, "IP_TRACKNUMBER"));
  }

  // Immediately advance to the first child. It may not exist if the parent
  // track is the last track, or there are no tracks (and parent_track_id is
  // null).
  track_id_ = GetTrack(nullptr, index_);
  if (track_id_ == nullptr) {
    return;
  }

  // Update the depth based on folder depth change value.
  depth_ +=
      static_cast<int>(GetMediaTrackInfo_Value(track_id_, "I_FOLDERDEPTH"));
}

void ChildTrackIterator::Next() {
  if (track_id_ == nullptr) {
    return;
  }

  for (++index_; depth_ >= 0; ++index_) {
    track_id_ = GetTrack(nullptr, index_);
    if (track_id_ == nullptr) {
      break;
    }

    // Update the depth based on folder depth change value.
    bool found_child = (depth_ == 0);
    depth_ +=
        static_cast<int>(GetMediaTrackInfo_Value(track_id_, "I_FOLDERDEPTH"));

    // Found the next child.
    if (found_child) {
      ++child_index_;
      return;
    }
  }

  // No child found!
  track_id_ = nullptr;
}

}  // namespace jpr
