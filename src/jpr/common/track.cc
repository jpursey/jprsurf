// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track.h"

#include "absl/log/log.h"
#include "jpr/common/track_cache.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

Track::Track(Private, const Guid& guid, MediaTrack* track_id) : guid_(guid) {
  DoRefresh(track_id);
}

void Track::Refresh() { DoRefresh(track_id_); }

void Track::DoRefresh(MediaTrack* track_id) {
  bool changed = (track_id_ != track_id);
  track_id_ = track_id;
  if (track_id == nullptr) {
    name_.clear();
    volume_ = 0.0;
    pan_ = 0.0;
    selected_ = false;
    mute_ = false;
    solo_ = false;
    rec_arm_ = false;
    if (changed) {
      NotifyListeners();
    }
    return;
  }

  int flags = 0;
  const char* name = GetTrackState(track_id_, &flags);
  if (name == nullptr) {
    changed = changed || !name_.empty();
    LOG(ERROR) << "Failed to get name for valid track " << guid_;
    name_.clear();
  } else {
    name_ = name;
  }
  bool selected = (flags & 2) != 0;
  bool mute = (flags & 8) != 0;
  bool solo = (flags & 16) != 0;
  bool rec_arm = (flags & 64) != 0;
  changed = changed || (selected_ != selected) || (mute_ != mute) ||
            (solo_ != solo) || (rec_arm_ != rec_arm);
  selected_ = selected;
  mute_ = mute;
  solo_ = solo;
  rec_arm_ = rec_arm;

  double volume = 0.0;
  double pan = 0.0;
  if (!GetTrackUIVolPan(track_id_, &volume, &pan)) {
    changed = changed || (volume_ != 0.0) || (pan_ != 0.0);
    LOG(ERROR) << "Failed to get volume and pan for valid track " << guid_;
    volume_ = 0.0;
    pan_ = 0.0;
  } else {
    changed = changed || (volume_ != volume) || (pan_ != pan);
    volume_ = volume;
    pan_ = pan;
  }
  if (changed) {
    NotifyListeners();
  }
}

void Track::NotifyListeners() {
  for (TrackListener* listener : listeners_) {
    listener->OnTrackChanged(this);
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
  bool changed = (name_ != name_str);
  name_ = std::move(name_str);
  if (changed) {
    NotifyListeners();
  }
}

void Track::SetVolume(double volume) {
  if (track_id_ == nullptr) {
    return;
  }
  if (!GetSetMediaTrackInfo(track_id_, "D_VOL", &volume)) {
    LOG(ERROR) << "Failed to set volume for valid track " << guid_;
    return;
  }
  bool changed = (volume_ != volume);
  volume_ = volume;
  if (changed) {
    NotifyListeners();
  }
}

void Track::SetPan(double pan) {
  if (track_id_ == nullptr) {
    return;
  }
  if (!GetSetMediaTrackInfo(track_id_, "D_PAN", &pan)) {
    LOG(ERROR) << "Failed to set pan for valid track " << guid_;
    return;
  }
  bool changed = (pan_ != pan);
  pan_ = pan;
  if (changed) {
    NotifyListeners();
  }
}

void Track::SetSelected(bool selected) {
  if (track_id_ == nullptr) {
    return;
  }
  SetTrackSelected(track_id_, selected);
  bool changed = (selected_ != selected);
  selected_ = selected;
  if (changed) {
    NotifyListeners();
  }
}

void Track::SetMute(bool mute) {
  if (track_id_ == nullptr) {
    return;
  }
  if (!GetSetMediaTrackInfo(track_id_, "B_MUTE", &mute)) {
    LOG(ERROR) << "Failed to set mute for valid track " << guid_;
    return;
  }
  bool changed = (mute_ != mute);
  mute_ = mute;
  if (changed) {
    NotifyListeners();
  }
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
  bool changed = (solo_ != solo);
  solo_ = solo;
  if (changed) {
    NotifyListeners();
  }
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
  bool changed = (rec_arm_ != rec_arm);
  rec_arm_ = rec_arm;
  if (changed) {
    NotifyListeners();
  }
}

void Track::Subscribe(TrackListener* listener) { listeners_.insert(listener); }

void Track::Unsubscribe(TrackListener* listener) { listeners_.erase(listener); }

}  // namespace jpr
