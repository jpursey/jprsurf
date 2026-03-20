// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track.h"

#include "absl/log/log.h"
#include "jpr/common/modifiers.h"
#include "jpr/common/track_cache.h"
#include "sdk/reaper_plugin_functions.h"

namespace jpr {

namespace {

// These ingroupflags are used for SetTrackUIVolume and SetTrackUIPan, and are
// passed to the REAPER API to control grouping and ganging behavior.
constexpr int kNoGrouping = 1;
constexpr int kNoGanging = 2;

}  // namespace

Track::Track(Private, const Guid& guid, MediaTrack* track_id) : guid_(guid) {
  DoRefresh(track_id);
}

Track::~Track() {
  // If this track is currently selected, clear the last selected track pointer
  // to avoid leaving a dangling pointer.
  if (TrackCache::Get().GetLastTouchedTrack() == this) {
    TrackCache::Get().SetLastTouchedTrack(nullptr);
  }
}

void Track::Refresh() { DoRefresh(track_id_); }

void Track::DoRefresh(MediaTrack* track_id) {
  bool changed = (track_id_ != track_id);
  track_id_ = track_id;
  if (track_id == nullptr) {
    if (!changed) {
      return;
    }
    if (TrackCache::Get().GetLastTouchedTrack() == this) {
      TrackCache::Get().SetLastTouchedTrack(nullptr);
    }
    name_.clear();
    volume_ = 0.0;
    pan_ = 0.0;
    selected_ = false;
    mute_ = false;
    solo_ = false;
    rec_arm_ = false;
    NotifyListeners();
    return;
  }

  int flags = 0;
  const char* name = GetTrackState(track_id_, &flags);
  if (name == nullptr) {
    changed = changed || !name_.empty();
    LOG(ERROR) << "Failed to get name for valid track " << guid_;
    name_.clear();
  } else {
    changed = changed || (name_ != name);
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

void Track::DoToggleSelected() {
  selected_ = !selected_;
  if (selected_) {
    TrackCache::Get().SetLastTouchedTrack(this);
  }
  NotifyListeners();
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
  if (track_id_ == nullptr || volume_ == volume) {
    return;
  }
  SetTrackUIVolume(track_id_, volume, /*relative=*/false, /*done=*/true,
                   kNoGrouping | kNoGanging);
  volume_ = volume;
  NotifyListeners();
}

void Track::SetPan(double pan) {
  if (track_id_ == nullptr || pan_ == pan) {
    return;
  }
  SetTrackUIPan(track_id_, pan, /*relative=*/false, /*done=*/true,
                kNoGrouping | kNoGanging);
  pan_ = pan;
  NotifyListeners();
}

void Track::SetSelected(bool selected) {
  if (track_id_ == nullptr || selected_ == selected) {
    return;
  }
  SetTrackSelected(track_id_, selected);
  DoToggleSelected();
}

void Track::SetMute(bool mute) {
  if (track_id_ == nullptr || mute_ == mute) {
    return;
  }
  SetTrackUIMute(track_id_, mute ? 1 : 0, kNoGrouping | kNoGanging);
  mute_ = mute;
  NotifyListeners();
}

void Track::SetSolo(bool solo) {
  if (track_id_ == nullptr || solo_ == solo) {
    return;
  }
  SetTrackUISolo(track_id_, solo ? 1 : 0, kNoGrouping | kNoGanging);
  solo_ = solo;
  NotifyListeners();
}

void Track::SetRecArm(bool rec_arm) {
  if (track_id_ == nullptr || rec_arm_ == rec_arm) {
    return;
  }
  SetTrackUIRecArm(track_id_, rec_arm ? 1 : 0, kNoGrouping | kNoGanging);
  rec_arm_ = rec_arm;
  NotifyListeners();
}

void Track::UiVolume(double volume) {
  if (track_id_ == nullptr || volume_ == volume) {
    return;
  }
  // Done should actually be set based on whether the fader is being touched or
  // not, but that is not yet supported.
  SetTrackUIVolume(track_id_, volume, /*relative=*/false, /*done=*/true,
                   /*ingroupflags=*/0);
  volume_ = volume;
  NotifyListeners();
}

void Track::UiPan(double pan) {
  if (track_id_ == nullptr) {
    return;
  }
  // Done should actually be set based on whether the pan knob is being touched
  // or not, but that is not yet supported (it also isn't possible on XTouch).
  SetTrackUIPan(track_id_, pan, /*relative=*/false, /*done=*/true,
                /*ingroupflags=*/0);
  pan_ = pan;
  NotifyListeners();
}

void Track::UiSelected() {
  if (track_id_ == nullptr) {
    return;
  }

  // Shift is used to toggle selection of all tracks between the last selected
  // track and this track.
  Track* last_selected_track = TrackCache::Get().GetLastTouchedTrack();
  if (AreModifiersOn(kModShift) && last_selected_track != nullptr) {
    // We iterate over *all* tracks in the project, and only select tracks which
    // are between the last selected track and this track in the track list.
    int first_index = last_selected_track->GetGlobalIndex();
    int last_index = GetGlobalIndex();
    if (first_index > last_index) {
      std::swap(first_index, last_index);
    }
    auto tracks = TrackCache::Get().GetTracks();
    for (int i = 0; i < tracks.size(); ++i) {
      Track* track = tracks[i];
      bool selected = (i >= first_index && i <= last_index);
      if (selected != IsTrackSelected(track->track_id_)) {
         SetTrackSelected(track->track_id_, selected);
      }
    }
    return;
  }

  // Ctrl is used to toggle selection of individual tracks.
  if (AreModifiersOn(kModCtrl)) {
    SetTrackSelected(track_id_, !selected_);
    DoToggleSelected();
    return;
  }

  // To emulate default REAPER behavior "unselected" a selected track will
  // just unselect every other track and leave the selected track selected.
  if (selected_ && CountSelectedTracks(nullptr) > 1) {
    TrackCache::Get().SetLastTouchedTrack(this);
    SetOnlyTrackSelected(track_id_);
    return;
  }
  if (!selected_) {
    SetOnlyTrackSelected(track_id_);
  } else {
    //  Even though this is not default REAPER behavior, this allows unselecting
    //  the last track.
    SetTrackSelected(track_id_, false);
  }
  DoToggleSelected();
}

void Track::UiMute() {
  if (track_id_ == nullptr) {
    return;
  }
  mute_ = !mute_;
  SetTrackUIMute(track_id_, mute_ ? 1 : 0, /*ingroupflags=*/0);
  NotifyListeners();
}

void Track::UiSolo() {
  if (track_id_ == nullptr) {
    return;
  }
  solo_ = !solo_;
  SetTrackUISolo(track_id_, solo_ ? 1 : 0, /*ingroupflags=*/0);
  NotifyListeners();
}

void Track::UiRecArm() {
  if (track_id_ == nullptr) {
    return;
  }
  rec_arm_ = !rec_arm_;
  SetTrackUIRecArm(track_id_, rec_arm_ ? 1 : 0, /*ingroupflags=*/0);
  NotifyListeners();
}

void Track::Subscribe(TrackListener* listener) { listeners_.insert(listener); }

void Track::Unsubscribe(TrackListener* listener) { listeners_.erase(listener); }

}  // namespace jpr
