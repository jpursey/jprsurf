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

#include "absl/memory/memory.h"
#include "absl/strings/str_cat.h"
#include "absl/types/span.h"
#include "gtest/gtest.h"

namespace jpr {
namespace {

// Returns where `route` is in `routes`, which has it.
int IndexOf(absl::Span<FakeRoute* const> routes, const FakeRoute* route) {
  return static_cast<int>(std::find(routes.begin(), routes.end(), route) -
                          routes.begin());
}

}  // namespace

std::unique_ptr<FakeProject> FakeProject::Create() {
  return absl::WrapUnique(new FakeProject(1));
}

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

std::vector<FakeTrack*> FakeProject::AddTracks(int count, FakeTrack* folder) {
  if (folder != nullptr && FindTrack(folder) < 0) {
    ADD_FAILURE() << "AddTracks() was given a folder that isn't a track in the "
                     "project";
    return {};
  }
  const std::string prefix =
      folder != nullptr ? absl::StrCat(folder->name, ".") : "T";
  int number = 0;
  for (FakeTrack* track : tracks_) {
    if (GetParentTrack(track) == folder) {
      ++number;
    }
  }
  std::vector<FakeTrack*> tracks;
  tracks.reserve(count);
  for (int i = 0; i < count; ++i) {
    tracks.push_back(AddTrack(absl::StrCat(prefix, ++number), folder));
  }
  return tracks;
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

void FakeProject::ShowInMixer(FakeTrack* track, bool shown) {
  if (FindTrack(track) < 0) {
    ADD_FAILURE() << "ShowInMixer() was given a track that isn't in the "
                     "project";
    return;
  }
  for (FakeTrack* other : tracks_) {
    for (const FakeTrack* folder = other; folder != nullptr;
         folder = GetParentTrack(folder)) {
      if (folder == track) {
        other->show_in_mixer = shown;
        break;
      }
    }
  }
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

std::vector<FakeTrack*> FakeProject::GetSelectedTracks(
    bool include_master) const {
  std::vector<FakeTrack*> selected;
  if (include_master && master_track_->selected) {
    selected.push_back(master_track_);
  }
  for (FakeTrack* track : tracks_) {
    if (track->selected) {
      selected.push_back(track);
    }
  }
  return selected;
}

FakeTrack* FakeProject::FindTrackByName(std::string_view name) const {
  auto it = std::ranges::find(tracks_, name, &FakeTrack::name);
  return it != tracks_.end() ? *it : nullptr;
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

FakeRoute* FakeProject::GetTrackSendUiRoute(const FakeTrack* track,
                                            int index) const {
  if (index < 0) {
    const absl::Span<FakeRoute* const> receives = GetReceives(track);
    return -1 - index < static_cast<int>(receives.size()) ? receives[-1 - index]
                                                          : nullptr;
  }
  const absl::Span<FakeRoute* const> outputs = GetHardwareOutputs(track);
  const int output_count = static_cast<int>(outputs.size());
  if (index < output_count) {
    return outputs[index];
  }
  const absl::Span<FakeRoute* const> sends = GetSends(track);
  return index - output_count < static_cast<int>(sends.size())
             ? sends[index - output_count]
             : nullptr;
}

int FakeProject::GetTrackSendUiIndex(const FakeRoute* route) const {
  const absl::Span<FakeRoute* const> outputs =
      GetHardwareOutputs(route->source);
  if (route->destination == nullptr) {
    return IndexOf(outputs, route);
  }
  return static_cast<int>(outputs.size()) +
         IndexOf(GetSends(route->source), route);
}

int FakeProject::GetReceiveIndex(const FakeRoute* route) const {
  return IndexOf(GetReceives(route->destination), route);
}

}  // namespace jpr
