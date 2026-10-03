// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/common/testing/fake_project.h"

#include <algorithm>
#include <memory>
#include <string_view>
#include <vector>

#include "gtest/gtest.h"

namespace jpr {

FakeProject::FakeProject(int number) : number_(number) {
  master_track_ = CreateTrack(MakeGuid(), nullptr);
  master_track_->name = "MASTER";
}

//------------------------------------------------------------------------------
// Tracks
//------------------------------------------------------------------------------

FakeTrack* FakeProject::AddTrack(std::string_view name, FakeTrack* parent) {
  if (parent != nullptr && FindTrack(parent) < 0) {
    ADD_FAILURE() << "AddTrack() was given a parent that isn't a track in the "
                     "project";
    return nullptr;
  }
  FakeTrack* track = InsertTrack(MakeGuid(), parent);
  track->name = name;
  return track;
}

void FakeProject::DeleteTrack(FakeTrack* track) {
  const int index = FindTrack(track);
  if (index < 0) {
    ADD_FAILURE() << "DeleteTrack() was given a track that isn't in the "
                     "project";
    return;
  }
  // Every track in the order has a record.
  TrackRecord* record = FindRecord(track);
  for (FakeTrack* other : tracks_) {
    TrackRecord* other_record = FindRecord(other);
    if (other_record->parent == track) {
      other_record->parent = record->parent;
    }
  }
  while (!record->sends.empty()) {
    RemoveRoute(record->sends.back());
  }
  while (!record->receives.empty()) {
    RemoveRoute(record->receives.back());
  }
  while (!record->hardware_outputs.empty()) {
    RemoveRoute(record->hardware_outputs.back());
  }
  record->deleted = true;
  tracks_.erase(tracks_.begin() + index);
}

FakeTrack* FakeProject::RestoreTrack(const FakeTrack* deleted) {
  const TrackRecord* record = FindRecord(deleted);
  if (record == nullptr || !record->deleted) {
    ADD_FAILURE() << "RestoreTrack() was given a track that isn't a deleted "
                     "track in the project";
    return nullptr;
  }
  if (FindTrackByGuid(record->guid) != nullptr) {
    ADD_FAILURE() << "RestoreTrack() was given a deleted track that was "
                     "already restored";
    return nullptr;
  }
  // The folder may have been deleted and restored too, as a new track.
  FakeTrack* parent = record->parent != nullptr
                          ? FindTrackByGuid(GetGuid(record->parent))
                          : nullptr;
  FakeTrack* track = InsertTrack(record->guid, parent);
  *track = *deleted;
  return track;
}

FakeTrack* FakeProject::GetParentTrack(const FakeTrack* track) const {
  const TrackRecord* record = FindRecord(track);
  return record != nullptr ? record->parent : nullptr;
}

const GUID& FakeProject::GetGuid(const FakeTrack* track) const {
  static constexpr GUID kNoGuid = {};
  const TrackRecord* record = FindRecord(track);
  return record != nullptr ? record->guid : kNoGuid;
}

bool FakeProject::HasTrack(const FakeTrack* track) const {
  const TrackRecord* record = FindRecord(track);
  return record != nullptr && !record->deleted;
}

bool FakeProject::HadTrack(const FakeTrack* track) const {
  const TrackRecord* record = FindRecord(track);
  return record != nullptr && record->deleted;
}

bool FakeProject::AnyTrackSolo() const {
  return std::ranges::any_of(
      tracks_, [](const FakeTrack* track) { return track->solo; });
}

GUID FakeProject::MakeGuid() {
  GUID guid = {};
  guid.Data1 = next_guid_++;
  guid.Data2 = static_cast<unsigned short>(number_);
  return guid;
}

FakeProject::TrackRecord* FakeProject::FindRecord(const FakeTrack* track) {
  auto it = records_.find(track);
  return it != records_.end() ? &it->second : nullptr;
}

const FakeProject::TrackRecord* FakeProject::FindRecord(
    const FakeTrack* track) const {
  auto it = records_.find(track);
  return it != records_.end() ? &it->second : nullptr;
}

FakeTrack* FakeProject::FindTrackByGuid(const GUID& guid) const {
  for (const auto& [track, record] : records_) {
    if (!record.deleted && record.guid == guid) {
      return record.track.get();
    }
  }
  return nullptr;
}

FakeTrack* FakeProject::CreateTrack(const GUID& guid, FakeTrack* parent) {
  auto track = std::make_unique<FakeTrack>();
  FakeTrack* created = track.get();
  TrackRecord& record = records_[created];
  record.track = std::move(track);
  record.guid = guid;
  record.parent = parent;
  return created;
}

FakeTrack* FakeProject::InsertTrack(const GUID& guid, FakeTrack* parent) {
  int index = GetTrackCount();
  if (parent != nullptr) {
    index = FindTrack(parent) + 1;
    while (index < GetTrackCount() && IsInFolder(tracks_[index], parent)) {
      ++index;
    }
  }
  FakeTrack* track = CreateTrack(guid, parent);
  tracks_.insert(tracks_.begin() + index, track);
  return track;
}

int FakeProject::FindTrack(const FakeTrack* track) const {
  auto it = std::ranges::find(tracks_, track);
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

//------------------------------------------------------------------------------
// Routes
//------------------------------------------------------------------------------

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

FakeRoute* FakeProject::AddRoute(FakeTrack* source, FakeTrack* destination) {
  FakeRoute* route = routes_
                         .emplace_back(std::make_unique<FakeRoute>(FakeRoute{
                             .source = source, .destination = destination}))
                         .get();
  // AddSend() and AddHardwareOutput() checked the ends are tracks in the
  // project, so each has a record.
  TrackRecord* source_record = FindRecord(source);
  if (destination == nullptr) {
    source_record->hardware_outputs.push_back(route);
  } else {
    TrackRecord* destination_record = FindRecord(destination);
    source_record->sends.push_back(route);
    destination_record->receives.push_back(route);
  }
  return route;
}

void FakeProject::RemoveRoute(FakeRoute* route) {
  // A route's ends always have records, as AddRoute() checked them, and a
  // record is never removed.
  TrackRecord* source_record = FindRecord(route->source);
  if (route->destination == nullptr) {
    std::erase(source_record->hardware_outputs, route);
  } else {
    TrackRecord* destination_record = FindRecord(route->destination);
    std::erase(source_record->sends, route);
    std::erase(destination_record->receives, route);
  }
  std::erase_if(routes_,
                [route](const auto& entry) { return entry.get() == route; });
}

absl::Span<FakeRoute* const> FakeProject::GetSends(
    const FakeTrack* track) const {
  const TrackRecord* record = FindRecord(track);
  if (record == nullptr) {
    return {};
  }
  return record->sends;
}

absl::Span<FakeRoute* const> FakeProject::GetReceives(
    const FakeTrack* track) const {
  const TrackRecord* record = FindRecord(track);
  if (record == nullptr) {
    return {};
  }
  return record->receives;
}

absl::Span<FakeRoute* const> FakeProject::GetHardwareOutputs(
    const FakeTrack* track) const {
  const TrackRecord* record = FindRecord(track);
  if (record == nullptr) {
    return {};
  }
  return record->hardware_outputs;
}

}  // namespace jpr
