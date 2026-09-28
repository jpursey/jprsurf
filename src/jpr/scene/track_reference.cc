// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_reference.h"

#include "absl/strings/str_cat.h"
#include "jpr/common/track_cache.h"

namespace jpr {

TrackReference::TrackReference(std::string_view name, TrackActions* actions,
                               const TrackReference* fallback,
                               const TrackReference* follow)
    : ViewReference(name, SubjectKind::kTrack),
      filter_(actions->GetTrackFilter()),
      fallback_(fallback),
      follow_(follow),
      properties_(actions) {}

Track* TrackReference::GetTrack() const {
  Track* track = properties_.GetTrack();
  return track != TrackCache::Get().GetStubTrack() ? track : nullptr;
}

void TrackReference::Set(Track* track) {
  if (track == nullptr || !track->Exists()) {
    track = GetFallbackTrack();
  }
  if (track == nullptr) {
    track = TrackCache::Get().GetStubTrack();
  }
  if (track != properties_.GetTrack()) {
    ChangeVersion();
    properties_.SetTrack(track);
  }
}

ViewProperty* TrackReference::GetField(std::string_view name) const {
  return properties_.GetProperty(absl::StrCat(kTrackNamespace, name));
}

void TrackReference::Update() {
  if (follow_ != nullptr && follow_->GetVersion() != followed_version_) {
    followed_version_ = follow_->GetVersion();

    // Only a track with a place in the filter is on the surface, which leaves
    // out the master track, and tracks that don't exist.
    Track* followed_track = follow_->GetTrack();
    if (followed_track != nullptr &&
        followed_track->GetGlobalIndex(filter_).has_value()) {
      Set(followed_track);
    }
  }

  // A track is only deleted by a track list refresh, but this is as cheap as
  // checking whether there was one.
  Track* track = GetTrack();
  if (track == nullptr || !track->Exists()) {
    Set(nullptr);
  }

  if (!properties_.IsWatched()) {
    return;
  }
  track = properties_.GetTrack();
  track->Refresh();
  if (properties_.IsMeterWatched()) {
    track->RefreshMeter();
  }
}

Track* TrackReference::GetFallbackTrack() const {
  return fallback_ != nullptr ? fallback_->GetTrack() : nullptr;
}

}  // namespace jpr
