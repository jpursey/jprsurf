// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_project.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "absl/base/no_destructor.h"
#include "gtest/gtest.h"

namespace jpr {

FakeProject::FakeProject(int number) : number_(number) {
  master_track_.guid = MakeGuid();
  master_track_.name = "MASTER";
}

FakeTrack* FakeProject::AddTrack(std::string_view name, FakeTrack* parent) {
  int index = GetTrackCount();
  if (parent != nullptr) {
    const int parent_index = FindTrack(parent);
    if (parent_index < 0) {
      ADD_FAILURE() << "AddTrack() was given a parent that isn't a track in "
                       "the project";
      parent = nullptr;
    } else {
      index = parent_index + 1;
      while (index < GetTrackCount() &&
             IsInFolder(tracks_[index].get(), parent)) {
        ++index;
      }
    }
  }
  auto track = std::make_unique<FakeTrack>();
  track->guid = MakeGuid();
  track->name = name;
  FakeTrack* added = track.get();
  tracks_.insert(tracks_.begin() + index, std::move(track));
  parents_[added] = parent;
  return added;
}

void FakeProject::DeleteTrack(FakeTrack* track) {
  const int index = FindTrack(track);
  if (index < 0) {
    ADD_FAILURE() << "DeleteTrack() was given a track that isn't in the "
                     "project";
    return;
  }
  FakeTrack* const parent = parents_[track];
  for (auto& [child, child_parent] : parents_) {
    if (child_parent == track) {
      child_parent = parent;
    }
  }
  parents_.erase(track);
  if (auto it = track_routes_.find(track); it != track_routes_.end()) {
    // Copied, as removing each route changes the track's routes.
    const TrackRoutes routes = it->second;
    for (const std::vector<FakeRoute*>* list :
         {&routes.sends, &routes.receives, &routes.hardware_outputs}) {
      for (FakeRoute* route : *list) {
        RemoveRoute(route);
      }
    }
    track_routes_.erase(track);
  }
  deleted_tracks_.insert(track);
  deleted_track_storage_.push_back(std::move(tracks_[index]));
  tracks_.erase(tracks_.begin() + index);
}

FakeTrack* FakeProject::GetParentTrack(const FakeTrack* track) const {
  auto it = parents_.find(track);
  return it != parents_.end() ? it->second : nullptr;
}

bool FakeProject::HasTrack(const FakeTrack* track) const {
  return track == &master_track_ || parents_.contains(track);
}

bool FakeProject::HadTrack(const FakeTrack* track) const {
  return deleted_tracks_.contains(track);
}

FakeRoute* FakeProject::AddSend(FakeTrack* source, FakeTrack* destination) {
  if (FindTrack(source) < 0 || FindTrack(destination) < 0) {
    ADD_FAILURE() << "AddSend() was given a track that isn't in the project, "
                     "or is the master";
    return nullptr;
  }
  if (source == destination) {
    ADD_FAILURE() << "AddSend() was given a track to send to itself";
    return nullptr;
  }
  return AddRoute(source, destination);
}

FakeRoute* FakeProject::AddHardwareOutput(FakeTrack* source) {
  if (!HasTrack(source)) {
    ADD_FAILURE() << "AddHardwareOutput() was given a track that isn't in the "
                     "project";
    return nullptr;
  }
  return AddRoute(source, nullptr);
}

FakeRoute* FakeProject::AddRoute(FakeTrack* source, FakeTrack* destination) {
  FakeRoute* route = routes_
                         .emplace_back(std::make_unique<FakeRoute>(FakeRoute{
                             .source = source, .destination = destination}))
                         .get();
  if (destination == nullptr) {
    track_routes_[source].hardware_outputs.push_back(route);
  } else {
    track_routes_[source].sends.push_back(route);
    track_routes_[destination].receives.push_back(route);
  }
  return route;
}

void FakeProject::DeleteRoute(FakeRoute* route) {
  if (std::ranges::none_of(routes_, [route](const auto& entry) {
        return entry.get() == route;
      })) {
    ADD_FAILURE() << "DeleteRoute() was given a route that isn't in the "
                     "project";
    return;
  }
  RemoveRoute(route);
}

void FakeProject::RemoveRoute(FakeRoute* route) {
  if (route->destination == nullptr) {
    std::erase(track_routes_[route->source].hardware_outputs, route);
  } else {
    std::erase(track_routes_[route->source].sends, route);
    std::erase(track_routes_[route->destination].receives, route);
  }
  std::erase_if(routes_,
                [route](const auto& entry) { return entry.get() == route; });
}

const FakeProject::TrackRoutes& FakeProject::GetTrackRoutes(
    const FakeTrack* track) const {
  static const absl::NoDestructor<TrackRoutes> kNoRoutes;
  auto it = track_routes_.find(track);
  return it != track_routes_.end() ? it->second : *kNoRoutes;
}

GUID FakeProject::MakeGuid() {
  GUID guid = {};
  guid.Data1 = next_guid_++;
  guid.Data2 = static_cast<unsigned short>(number_);
  return guid;
}

int FakeProject::FindTrack(const FakeTrack* track) const {
  auto it = std::ranges::find_if(
      tracks_, [track](const auto& entry) { return entry.get() == track; });
  return it != tracks_.end() ? static_cast<int>(it - tracks_.begin()) : -1;
}

bool FakeProject::IsInFolder(const FakeTrack* track,
                             const FakeTrack* folder) const {
  for (const FakeTrack* parent = GetParentTrack(track); parent != nullptr;
       parent = GetParentTrack(parent)) {
    if (parent == folder) {
      return true;
    }
  }
  return false;
}

}  // namespace jpr
