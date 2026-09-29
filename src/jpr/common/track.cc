// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/track.h"

#include <algorithm>
#include <optional>

#include "absl/log/log.h"
#include "jpr/common/reaper_api.h"
#include "jpr/common/track_cache.h"
#include "jpr/common/undo.h"

namespace jpr {

namespace {

// These ingroupflags are used for SetTrackUIVolume and SetTrackUIPan, and are
// passed to the REAPER API to control grouping and ganging behavior.
constexpr int kNoGrouping = 1;
constexpr int kNoGanging = 2;

// Categories for GetTrackNumSends and GetSetTrackSendInfo.
constexpr int kReceiveCategory = -1;
constexpr int kSendCategory = 0;
constexpr int kHardwareOutputCategory = 1;

// Undo point name for a TrackBatch that changed more than one kind of property.
constexpr char kChangeTracksUndoName[] = "JPR:Change Tracks";

}  // namespace

struct Track::BoolPropertyInfo {
  // The property's bit in the flags returned by GetTrackState().
  int state_bit;

  // The cached value in Track.
  bool Track::*cached;

  // Sets the property in REAPER. Selection ignores ingroupflags.
  void (*set)(MediaTrack* track_id, bool value, int ingroupflags);

  // The undo point name, or null for selection, which adds no undo point.
  const char* undo_name;
};

const Track::BoolPropertyInfo& Track::GetBoolPropertyInfo(
    TrackBoolProperty property) {
  // Indexed by TrackBoolProperty.
  static constexpr BoolPropertyInfo kBoolPropertyInfo[] = {
      {2, &Track::selected_,
       [](MediaTrack* track_id, bool value, int ingroupflags) {
         SetTrackSelected(track_id, value);
       },
       nullptr},
      {8, &Track::mute_,
       [](MediaTrack* track_id, bool value, int ingroupflags) {
         SetTrackUIMute(track_id, value ? 1 : 0, ingroupflags);
       },
       "JPR:Toggle Mute"},
      {16, &Track::solo_,
       [](MediaTrack* track_id, bool value, int ingroupflags) {
         SetTrackUISolo(track_id, value ? 1 : 0, ingroupflags);
       },
       "JPR:Toggle Solo"},
      {64, &Track::rec_arm_,
       [](MediaTrack* track_id, bool value, int ingroupflags) {
         SetTrackUIRecArm(track_id, value ? 1 : 0, ingroupflags);
       },
       "JPR:Toggle Record Arm"},
  };
  return kBoolPropertyInfo[static_cast<int>(property)];
}

bool Track::Get(TrackBoolProperty property) const {
  return this->*GetBoolPropertyInfo(property).cached;
}

void Track::Set(TrackBoolProperty property, bool value,
                TrackGrouping grouping) {
  if (track_id_ == nullptr) {
    return;
  }
  TrackBatch().Set(this, property, value, grouping);
}

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
    color_ = {0, 0, 0};
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
  for (TrackBoolProperty property : kTrackBoolProperties) {
    const BoolPropertyInfo& info = GetBoolPropertyInfo(property);
    const bool value = (flags & info.state_bit) != 0;
    bool& cached = this->*info.cached;
    changed = changed || (cached != value);
    cached = value;
  }

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

  int raw_color = GetTrackColor(track_id);
  Color color;
  if (raw_color == 0) {
    // No custom color set; use white as the default to match REAPER's
    // uncolored track appearance on the scribble strip.
    color = {255, 255, 255};
  } else {
    // Format is 0x01BBGGRR; mask off the 0x01 flag byte.
    color = {static_cast<uint8_t>(raw_color & 0xFF),
             static_cast<uint8_t>((raw_color >> 8) & 0xFF),
             static_cast<uint8_t>((raw_color >> 16) & 0xFF)};
  }
  changed = changed || (color_ != color);
  color_ = color;

  if (changed) {
    NotifyListeners();
  }
}

bool Track::UpdateVisibility() {
  if (track_id_ == nullptr) {
    return false;
  }

  // The master track has no B_SHOWINMIXER / B_SHOWINTCP state, and is always
  // included: a control surface has a dedicated master fader, so hiding it the
  // way REAPER's UI does is meaningless here.
  if (this == TrackCache::Get().GetMasterTrack()) {
    return false;
  }

  const bool mcp = GetMediaTrackInfo_Value(track_id_, "B_SHOWINMIXER") != 0.0;
  const bool tcp = GetMediaTrackInfo_Value(track_id_, "B_SHOWINTCP") != 0.0;
  FilterState& mcp_state =
      filter_state_[GetTrackFilterIndex(TrackFilter::kMcp)];
  FilterState& tcp_state =
      filter_state_[GetTrackFilterIndex(TrackFilter::kTcp)];
  if (mcp_state.visible == mcp && tcp_state.visible == tcp) {
    return false;
  }
  mcp_state.visible = mcp;
  tcp_state.visible = tcp;
  return true;
}

bool Track::UpdateRoutes() {
  hardware_output_count_ = GetTrackNumSends(track_id_, kHardwareOutputCategory);
  std::vector<TrackRoute> sends = BuildRoutes(TrackRouteType::kSend);
  std::vector<TrackRoute> receives = BuildRoutes(TrackRouteType::kReceive);
  const bool changed = (sends != sends_ || receives != receives_);
  sends_ = std::move(sends);
  receives_ = std::move(receives);
  return changed;
}

bool Track::OnRemoved() {
  parent_track_ = nullptr;
  for (FilterState& state : filter_state_) {
    state = {};
  }
  const bool routes_changed = !sends_.empty() || !receives_.empty();
  hardware_output_count_ = 0;
  sends_.clear();
  receives_.clear();
  DoRefresh(nullptr);
  return routes_changed;
}

void Track::RefreshMeter() {
  if (track_id_ == nullptr) {
    return;
  }
  double left = Track_GetPeakInfo(track_id_, 0);
  double right = Track_GetPeakInfo(track_id_, 1);
  double peak = std::max(left, right);
  for (TrackListener* listener : listeners_) {
    listener->OnTrackMeterChanged(this, peak);
  }
}

void Track::NotifyListeners() {
  for (TrackListener* listener : listeners_) {
    listener->OnTrackChanged(this);
  }
}

void Track::NotifyHierarchyChanged() {
  for (TrackListener* listener : listeners_) {
    listener->OnTrackHierarchyChanged(this);
  }
}

void Track::NotifyRoutesChanged() {
  for (TrackListener* listener : listeners_) {
    listener->OnTrackRoutesChanged(this);
  }
}

//------------------------------------------------------------------------------
// Name
//------------------------------------------------------------------------------

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

//------------------------------------------------------------------------------
// Volume
//------------------------------------------------------------------------------

void Track::SetVolume(double volume, TrackGrouping grouping) {
  if (track_id_ == nullptr || volume_ == volume) {
    return;
  }
  CSurf_OnVolumeChangeEx(track_id_, volume, /*relative=*/false,
                         /*allowgang=*/grouping == TrackGrouping::kGrouped);
  volume_ = volume;
  NotifyListeners();
}

//------------------------------------------------------------------------------
// Pan
//------------------------------------------------------------------------------

void Track::SetPan(double pan, TrackGrouping grouping) {
  if (track_id_ == nullptr || pan_ == pan) {
    return;
  }
  CSurf_OnPanChangeEx(track_id_, pan, /*relative=*/false,
                      /*allowgang=*/grouping == TrackGrouping::kGrouped);
  pan_ = pan;
  NotifyListeners();
}

//------------------------------------------------------------------------------
// Selected
//------------------------------------------------------------------------------

void Track::SelectOnly() {
  if (track_id_ == nullptr) {
    return;
  }
  SetOnlyTrackSelected(track_id_);
  if (!selected_) {
    selected_ = true;
    NotifyListeners();
  }
}

//------------------------------------------------------------------------------
// Routes
//------------------------------------------------------------------------------

std::vector<TrackRoute> Track::BuildRoutes(TrackRouteType type) const {
  const bool is_send = (type == TrackRouteType::kSend);
  const int category = is_send ? kSendCategory : kReceiveCategory;
  const char* other_track_param = is_send ? "P_DESTTRACK" : "P_SRCTRACK";
  std::vector<TrackRoute> routes;
  const int count = GetTrackNumSends(track_id_, category);
  routes.reserve(count);
  for (int i = 0; i < count; ++i) {
    auto* other_track_id = static_cast<MediaTrack*>(GetSetTrackSendInfo(
        track_id_, category, i, other_track_param, nullptr));
    Track* other_track = TrackCache::Get().GetTrack(other_track_id);
    if (other_track == nullptr) {
      LOG(ERROR) << "Failed to find " << other_track_param << " for route " << i
                 << " in category " << category;
      other_track = TrackCache::Get().GetStubTrack();
    }
    TrackRoute& route =
        routes.emplace_back(TrackRoute{.other_track = other_track});
    ReadRouteValues(type, i, route);
  }
  return routes;
}

// REAPER's UI route functions do not index routes the same way as
// GetSetTrackSendInfo. In all of them, sends are indexed after the track's
// hardware outputs. Receives use their plain index in the GetTrackReceiveUI*
// functions, but -1 - index in the *TrackSendUI* functions.
int Track::GetTrackSendUiIndex(TrackRouteType type, int index) const {
  return type == TrackRouteType::kSend ? hardware_output_count_ + index
                                       : -1 - index;
}

bool Track::ReadRouteValues(TrackRouteType type, int index,
                            TrackRoute& route) const {
  double volume = 0.0;
  double pan = 0.0;
  bool mute = false;
  if (type == TrackRouteType::kSend) {
    const int ui_index = GetTrackSendUiIndex(type, index);
    if (!GetTrackSendUIVolPan(track_id_, ui_index, &volume, &pan) ||
        !GetTrackSendUIMute(track_id_, ui_index, &mute)) {
      return false;
    }
  } else {
    if (!GetTrackReceiveUIVolPan(track_id_, index, &volume, &pan) ||
        !GetTrackReceiveUIMute(track_id_, index, &mute)) {
      return false;
    }
  }
  route.volume = volume;
  route.pan = pan;
  route.mute = mute;
  return true;
}

void Track::RefreshRoutes() {
  if (track_id_ == nullptr) {
    return;
  }
  bool changed = false;
  for (TrackRouteType type :
       {TrackRouteType::kSend, TrackRouteType::kReceive}) {
    std::vector<TrackRoute>& routes =
        (type == TrackRouteType::kSend ? sends_ : receives_);
    for (int i = 0; i < static_cast<int>(routes.size()); ++i) {
      TrackRoute route = routes[i];
      if (ReadRouteValues(type, i, route) && route != routes[i]) {
        routes[i] = route;
        changed = true;
      }
    }
  }
  if (changed) {
    NotifyRoutesChanged();
  }
}

TrackRoute* Track::GetMutableRoute(TrackRouteType type, int index) {
  std::vector<TrackRoute>& routes =
      (type == TrackRouteType::kSend ? sends_ : receives_);
  if (track_id_ == nullptr || index < 0 ||
      index >= static_cast<int>(routes.size())) {
    return nullptr;
  }
  return &routes[index];
}

void Track::SetRouteVolume(TrackRouteType type, int index, double volume) {
  TrackRoute* route = GetMutableRoute(type, index);
  if (route == nullptr || route->volume == volume) {
    return;
  }
  SetTrackSendUIVol(track_id_, GetTrackSendUiIndex(type, index), volume,
                    /*isend=*/0);
  // REAPER does not create undo points for route changes from a control
  // surface, so create one once the changes stop.
  ContinuousUndo::Get().OnChange(type == TrackRouteType::kSend
                                     ? "JPR: Adjust send volume"
                                     : "JPR: Adjust receive volume",
                                 UNDO_STATE_TRACKCFG);
  route->volume = volume;
  NotifyRoutesChanged();
}

void Track::SetRoutePan(TrackRouteType type, int index, double pan) {
  TrackRoute* route = GetMutableRoute(type, index);
  if (route == nullptr || route->pan == pan) {
    return;
  }
  SetTrackSendUIPan(track_id_, GetTrackSendUiIndex(type, index), pan,
                    /*isend=*/0);
  // See SetRouteVolume().
  ContinuousUndo::Get().OnChange(type == TrackRouteType::kSend
                                     ? "JPR: Adjust send pan"
                                     : "JPR: Adjust receive pan",
                                 UNDO_STATE_TRACKCFG);
  route->pan = pan;
  NotifyRoutesChanged();
}

void Track::SetRouteMute(TrackRouteType type, int index, bool mute) {
  TrackRoute* route = GetMutableRoute(type, index);
  if (route == nullptr) {
    return;
  }

  // REAPER only provides a way to toggle mute from its UI, so the cached value
  // can't be trusted to decide whether to toggle: it may have changed in
  // REAPER since the routes were last refreshed.
  TrackRoute current = *route;
  ReadRouteValues(type, index, current);
  if (current.mute != mute) {
    // Toggling mute creates its own undo point, which shouldn't include pending
    // volume or pan changes.
    ContinuousUndo::Get().Flush();
    ToggleTrackSendUIMute(track_id_, GetTrackSendUiIndex(type, index));
  }
  if (route->mute == mute) {
    return;
  }
  route->mute = mute;
  NotifyRoutesChanged();
}

//------------------------------------------------------------------------------
// Listeners
//------------------------------------------------------------------------------

void Track::Subscribe(TrackListener* listener) { listeners_.insert(listener); }

void Track::Unsubscribe(TrackListener* listener) { listeners_.erase(listener); }

//==============================================================================
// TrackBatch
//==============================================================================

TrackBatch::TrackBatch() { PreventUIRefresh(1); }

TrackBatch::~TrackBatch() {
  PreventUIRefresh(-1);
  if (undo_name_ != nullptr) {
    Undo_OnStateChangeEx(undo_name_, UNDO_STATE_TRACKCFG, -1);
  }
}

void TrackBatch::Set(Track* track, TrackBoolProperty property, bool value,
                     TrackGrouping grouping) {
  MediaTrack* const track_id = track->track_id_;
  if (track_id == nullptr) {
    return;
  }
  const Track::BoolPropertyInfo& info = Track::GetBoolPropertyInfo(property);

  // The cached value is only kept up to date for tracks the surface shows, so
  // compare against REAPER's.
  int state = 0;
  GetTrackState(track_id, &state);
  if (((state & info.state_bit) != 0) != value) {
    if (info.undo_name != nullptr) {
      if (undo_name_ == nullptr) {
        // This batch creates its own undo point, which shouldn't include
        // pending volume or pan changes.
        ContinuousUndo::Get().Flush();
        undo_name_ = info.undo_name;
      } else if (undo_name_ != info.undo_name) {
        // More than one kind of property changed.
        undo_name_ = kChangeTracksUndoName;
      }
    }
    info.set(
        track_id, value,
        grouping == TrackGrouping::kGrouped ? 0 : kNoGrouping | kNoGanging);
  }

  // REAPER now has the value either way, so bring the cache into line with it.
  bool& cached = track->*info.cached;
  if (cached != value) {
    cached = value;
    track->NotifyListeners();
  }
}

//==============================================================================
// TrackRange
//==============================================================================

std::optional<TrackRange> TrackRange::Between(const Track* from,
                                              const Track* to,
                                              TrackFilter filter,
                                              bool same_parent) {
  if (from == nullptr || to == nullptr) {
    return std::nullopt;
  }
  const std::optional<int> from_index = from->GetGlobalIndex(filter);
  const std::optional<int> to_index = to->GetGlobalIndex(filter);
  if (!from_index.has_value() || !to_index.has_value()) {
    return std::nullopt;
  }
  return TrackRange(filter, std::min(*from_index, *to_index),
                    std::max(*from_index, *to_index),
                    same_parent ? from->GetParentTrack() : nullptr);
}

bool TrackRange::Contains(const Track* track) const {
  const std::optional<int> index = track->GetGlobalIndex(filter_);
  if (!index.has_value() || *index < first_index_ || *index > last_index_) {
    return false;
  }
  return parent_track_ == nullptr || track->GetParentTrack() == parent_track_;
}

}  // namespace jpr
