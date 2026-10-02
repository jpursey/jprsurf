// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_project.h"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>

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
