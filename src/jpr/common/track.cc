// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track.h"

#include "absl/log/log.h"
#include "jpr/common/track_cache.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

Track::Track(const Guid& guid, MediaTrack* track_id) : guid_(guid) {
  DoRefresh(track_id);
}

void Track::Refresh() { DoRefresh(TrackCache::Get().GetTrackId(guid_)); }

void Track::DoRefresh(MediaTrack* track_id) {
  track_id_ = track_id;
  if (track_id_ == nullptr) {
    name_.clear();
    volume_ = 0.0;
    pan_ = 0.0;
    selected_ = false;
    mute_ = false;
    solo_ = false;
    rec_arm_ = false;
    return;
  }

  int flags = 0;
  const char* name = GetTrackState(track_id_, &flags);
  if (name == nullptr) {
    LOG(ERROR) << "Failed to get name for valid track " << guid_;
    name_.clear();
  } else {
    name_ = name;
  }
  selected_ = (flags & 2) != 0;
  mute_ = (flags & 8) != 0;
  solo_ = (flags & 16) != 0;
  rec_arm_ = (flags & 64) != 0;

  if (!GetTrackUIVolPan(track_id_, &volume_, &pan_)) {
    LOG(ERROR) << "Failed to get volume and pan for valid track " << guid_;
    volume_ = 0.0;
    pan_ = 0.0;
  }
}

void Track::SetName(std::string_view name) {
  if (track_id_ == nullptr) {
    return;
  }
  std::string name_str(name);
  if (!GetSetMediaTrackInfo_String(track_id_, "P_NAME", name_str.data(),
                                   /*setNewValue=*/true)) {
    LOG(ERROR) << "Failed to set name for valid track " << guid_;
    return;
  }
  name_ = std::move(name_str);
}

void Track::SetVolume(double volume) {
  if (track_id_ == nullptr) {
    return;
  }
  if (!GetSetMediaTrackInfo(track_id_, "D_VOL", &volume)) {
    LOG(ERROR) << "Failed to set volume for valid track " << guid_;
    return;
  }
  volume_ = volume;
}

void Track::SetPan(double pan) {
  if (track_id_ == nullptr) {
    return;
  }
  if (!GetSetMediaTrackInfo(track_id_, "D_PAN", &pan)) {
    LOG(ERROR) << "Failed to set pan for valid track " << guid_;
    return;
  }
  pan_ = pan;
}

void Track::SetSelected(bool selected) {
  if (track_id_ == nullptr) {
    return;
  }
  SetTrackSelected(track_id_, selected);
  selected_ = selected;
}

void Track::SetMute(bool mute) {
  if (track_id_ == nullptr) {
    return;
  }
  if (!GetSetMediaTrackInfo(track_id_, "B_MUTE", &mute)) {
    LOG(ERROR) << "Failed to set mute for valid track " << guid_;
    return;
  }
  mute_ = mute;
}

void Track::SetSolo(bool solo) {
  if (track_id_ == nullptr) {
    return;
  }
  // REAPER has the following settings for solo:
  //   0=not soloed,
  //   1=soloed,
  //   2=soloed in place,
  //   5=safe soloed,
  //   6=safe soloed in place
  int solo_value = (solo ? 1 : 0);
  if (!GetSetMediaTrackInfo(track_id_, "I_SOLO", &solo_value)) {
    LOG(ERROR) << "Failed to set solo for valid track " << guid_;
    return;
  }
  solo_ = solo;
}

void Track::SetRecArm(bool rec_arm) {
  if (track_id_ == nullptr) {
    return;
  }
  int rec_arm_value = (rec_arm ? 1 : 0);
  if (!GetSetMediaTrackInfo(track_id_, "I_RECARM", &rec_arm_value)) {
    LOG(ERROR) << "Failed to set record arm for valid track " << guid_;
    return;
  }
  rec_arm_ = rec_arm;
}

}  // namespace jpr
