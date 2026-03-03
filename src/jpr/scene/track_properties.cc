// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_properties.h"

#include "absl/log/check.h"
#include "sdk/reaper_plugin_functions.h"

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

class TrackSelectedProperty : public TrackProperty {
 public:
  explicit TrackSelectedProperty(Track* track)
      : TrackProperty(TrackProperties::kSelected, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetSelected(); }
  void WriteBool(bool value) override { GetTrack()->SetSelected(value); }
};

class TrackMuteProperty : public TrackProperty {
 public:
  explicit TrackMuteProperty(Track* track)
      : TrackProperty(TrackProperties::kMute, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetMute(); }
  void WriteBool(bool value) override { GetTrack()->SetMute(value); }
};

class TrackSoloProperty : public TrackProperty {
 public:
  explicit TrackSoloProperty(Track* track)
      : TrackProperty(TrackProperties::kSolo, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetSolo(); }
  void WriteBool(bool value) override { GetTrack()->SetSolo(value); }
};

class TrackRecArmProperty : public TrackProperty {
 public:
  explicit TrackRecArmProperty(Track* track)
      : TrackProperty(TrackProperties::kRecArm, Type::kToggle, track) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetRecArm(); }
  void WriteBool(bool value) override { GetTrack()->SetRecArm(value); }
};

class TrackPanProperty : public TrackProperty {
 public:
  explicit TrackPanProperty(Track* track)
      : TrackProperty(TrackProperties::kPan, Type::kPan, track) {}

 protected:
  double ReadDouble() const override { return GetTrack()->GetPan(); }
  void WriteDouble(double value) override { GetTrack()->SetPan(value); }
};

class TrackVolumeProperty : public TrackProperty {
 public:
  explicit TrackVolumeProperty(Track* track)
      : TrackProperty(TrackProperties::kVolume, Type::kVolume, track) {}

 protected:
  double ReadDouble() const override { return GetTrack()->GetVolume(); }
  void WriteDouble(double value) override { GetTrack()->SetVolume(value); }
};

}  // namespace

TrackProperties::TrackProperties(Track* track) : track_(track->GetShared()) {}

TrackProperties::~TrackProperties() = default;

void TrackProperties::SetTrack(Track* track) {
  track_ = track->GetShared();
  for (auto& [name, property] : properties_) {
    property->SetTrack(track);
  }
}

ViewProperty* TrackProperties::GetProperty(std::string_view name) {
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }
  if (name == kName) {
    auto& property = properties_[name] =
        std::make_unique<TrackNameProperty>(track_.get());
    return property.get();
  }
  if (name == kSelected) {
    auto& property = properties_[name] =
        std::make_unique<TrackSelectedProperty>(track_.get());
    return property.get();
  }
  if (name == kMute) {
    auto& property = properties_[name] =
        std::make_unique<TrackMuteProperty>(track_.get());
    return property.get();
  }
  if (name == kSolo) {
    auto& property = properties_[name] =
        std::make_unique<TrackSoloProperty>(track_.get());
    return property.get();
  }
  if (name == kRecArm) {
    auto& property = properties_[name] =
        std::make_unique<TrackRecArmProperty>(track_.get());
    return property.get();
  }
  if (name == kPan) {
    auto& property = properties_[name] =
        std::make_unique<TrackPanProperty>(track_.get());
    return property.get();
  }
  if (name == kVolume) {
    auto& property = properties_[name] =
        std::make_unique<TrackVolumeProperty>(track_.get());
    return property.get();
  }
  return nullptr;
}

}  // namespace jpr
