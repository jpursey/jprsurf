// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_properties.h"

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

namespace {

class TrackNameProperty : public TrackProperty {
 public:
  explicit TrackNameProperty(Track* track)
      : TrackProperty(TrackProperties::kName, Type::kText, track) {}

 protected:
  std::string ReadString() const override {
    return std::string(GetTrack()->GetName());
  }
  void WriteString(std::string_view value) override {
    GetTrack()->SetName(value);
  }
};

class TrackColorProperty : public TrackProperty {
 public:
  explicit TrackColorProperty(Track* track)
      : TrackProperty(TrackProperties::kColor, Type::kColor, track) {}

 protected:
  Color ReadColor() const override { return GetTrack()->GetColor(); }
};

// A property for selected, mute, solo, or record arm. With track actions, a
// write runs the action rather than setting the value.
class TrackBoolViewProperty : public TrackProperty {
 public:
  TrackBoolViewProperty(std::string_view name, TrackBoolProperty property,
                        Track* track, TrackActions* actions)
      : TrackProperty(name, Type::kToggle, track),
        property_(property),
        actions_(actions) {}

 protected:
  bool ReadBool() const override { return GetTrack()->Get(property_); }
  void WriteBool(bool value) override {
    if (actions_ == nullptr) {
      GetTrack()->Set(property_, value);
    } else if (property_ == TrackBoolProperty::kSelected) {
      actions_->UiSelect(GetTrack());
    } else {
      actions_->UiToggle(GetTrack(), property_);
    }
  }

 private:
  const TrackBoolProperty property_;
  TrackActions* const actions_;
};

// A property for pan. With track actions, a write runs the action rather than
// setting the value.
class TrackPanProperty : public TrackProperty {
 public:
  TrackPanProperty(std::string_view name, Track* track, TrackActions* actions)
      : TrackProperty(name, Type::kPan, track), actions_(actions) {}

 protected:
  double ReadDouble() const override { return GetTrack()->GetPan(); }
  void WriteDouble(double value) override {
    if (actions_ != nullptr) {
      actions_->UiSetPan(GetTrack(), value);
    } else {
      GetTrack()->SetPan(value);
    }
  }

 private:
  TrackActions* const actions_;
};

// A property for volume. With track actions, a write runs the action rather
// than setting the value.
class TrackVolumeProperty : public TrackProperty {
 public:
  TrackVolumeProperty(std::string_view name, Track* track,
                      TrackActions* actions)
      : TrackProperty(name, Type::kVolume, track), actions_(actions) {}

 protected:
  double ReadDouble() const override { return GetTrack()->GetVolume(); }
  void WriteDouble(double value) override {
    if (actions_ != nullptr) {
      actions_->UiSetVolume(GetTrack(), value);
    } else {
      GetTrack()->SetVolume(value);
    }
  }

 private:
  TrackActions* const actions_;
};

// The names of the TrackBoolViewProperty properties, without and with track
// actions.
struct BoolViewPropertyNames {
  std::string_view name;
  std::string_view ui_name;
  TrackBoolProperty property;
};
constexpr BoolViewPropertyNames kBoolViewPropertyNames[] = {
    {TrackProperties::kSelected, TrackProperties::kUiSelected,
     TrackBoolProperty::kSelected},
    {TrackProperties::kMute, TrackProperties::kUiMute,
     TrackBoolProperty::kMute},
    {TrackProperties::kSolo, TrackProperties::kUiSolo,
     TrackBoolProperty::kSolo},
    {TrackProperties::kRecArm, TrackProperties::kUiRecArm,
     TrackBoolProperty::kRecArm},
};

class TrackIsFolderProperty : public TrackProperty {
 public:
  TrackIsFolderProperty(Track* track, TrackFilter filter)
      : TrackProperty(TrackProperties::kTrackIsFolder, Type::kToggle, track),
        filter_(filter) {}

 protected:
  bool ReadBool() const override {
    // Only children that are on the surface count, since a folder whose
    // children are all hidden cannot be navigated into.
    return GetTrack()->GetChildTrackCount(filter_) > 0;
  }

 private:
  const TrackFilter filter_;
};

class TrackHasParentProperty : public TrackProperty {
 public:
  explicit TrackHasParentProperty(Track* track)
      : TrackProperty(TrackProperties::kTrackHasParent, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override {
    return GetTrack()->GetParentTrack() != nullptr;
  }
};

class TrackExistsProperty : public TrackProperty {
 public:
  explicit TrackExistsProperty(Track* track)
      : TrackProperty(TrackProperties::kTrackExists, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override { return GetTrack()->Exists(); }
};

class TrackHasRoutesProperty : public TrackProperty {
 public:
  explicit TrackHasRoutesProperty(Track* track)
      : TrackProperty(TrackProperties::kTrackHasRoutes, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override {
    return !GetTrack()->GetSends().empty() ||
           !GetTrack()->GetReceives().empty();
  }
};

}  // namespace

TrackProperties::TrackProperties(TrackActions* actions, Track* track)
    : actions_(actions), track_(track->GetShared()) {
  track_->Subscribe(this);
}

TrackProperties::~TrackProperties() { track_->Unsubscribe(this); }

void TrackProperties::SetTrack(Track* track) {
  if (track == track_.get()) {
    return;
  }
  track_->Unsubscribe(this);
  track_ = track->GetShared();
  track_->Subscribe(this);
  for (auto& [name, property] : properties_) {
    property->SetTrack(track);
  }
}

void TrackProperties::OnTrackChanged(Track* track) {
  for (auto& [name, property] : properties_) {
    property->NotifyChanged();
  }
}

void TrackProperties::OnTrackHierarchyChanged(Track* track) {
  for (auto& [name, property] : properties_) {
    property->NotifyChanged();
  }
}

void TrackProperties::OnTrackRoutesChanged(Track* track) {
  if (has_routes_property_ != nullptr) {
    has_routes_property_->NotifyChanged();
  }
}

void TrackProperties::OnTrackMeterChanged(Track* track, double peak) {
  if (meter_property_ != nullptr) {
    if (meter_property_->peak_ == 0.0 && peak <= 0.0) {
      return;
    }
    meter_property_->peak_ = std::max(peak, 0.0);
    meter_property_->NotifyChanged();
  }
}

ViewProperty* TrackProperties::GetProperty(std::string_view name) const {
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }
  if (name == kMeter) {
    auto meter_property = std::make_unique<TrackMeterProperty>(track_.get());
    meter_property_ = meter_property.get();
    properties_[name] = std::move(meter_property);
    return meter_property_;
  }
  if (name == kName) {
    auto& property = properties_[name] =
        std::make_unique<TrackNameProperty>(track_.get());
    return property.get();
  }
  if (name == kColor) {
    auto& property = properties_[name] =
        std::make_unique<TrackColorProperty>(track_.get());
    return property.get();
  }
  for (const BoolViewPropertyNames& names : kBoolViewPropertyNames) {
    if (name == names.name || name == names.ui_name) {
      auto& property = properties_[name] =
          std::make_unique<TrackBoolViewProperty>(
              name, names.property, track_.get(),
              name == names.ui_name ? actions_ : nullptr);
      return property.get();
    }
  }
  if (name == kPan || name == kUiPan) {
    auto& property = properties_[name] = std::make_unique<TrackPanProperty>(
        name, track_.get(), name == kUiPan ? actions_ : nullptr);
    return property.get();
  }
  if (name == kVolume || name == kUiVolume) {
    auto& property = properties_[name] = std::make_unique<TrackVolumeProperty>(
        name, track_.get(), name == kUiVolume ? actions_ : nullptr);
    return property.get();
  }
  if (name == kTrackIsFolder) {
    auto& property = properties_[name] =
        std::make_unique<TrackIsFolderProperty>(track_.get(),
                                                actions_->GetTrackFilter());
    return property.get();
  }
  if (name == kTrackHasParent) {
    auto& property = properties_[name] =
        std::make_unique<TrackHasParentProperty>(track_.get());
    return property.get();
  }
  if (name == kTrackExists) {
    auto& property = properties_[name] =
        std::make_unique<TrackExistsProperty>(track_.get());
    return property.get();
  }
  if (name == kTrackHasRoutes) {
    auto& property = properties_[name] =
        std::make_unique<TrackHasRoutesProperty>(track_.get());
    has_routes_property_ = property.get();
    return property.get();
  }
  return nullptr;
}

bool TrackProperties::IsWatched() const {
  return absl::c_any_of(
      properties_, [](const auto& entry) { return entry.second->IsWatched(); });
}

}  // namespace jpr
