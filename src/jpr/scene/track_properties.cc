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

class TrackColorProperty : public TrackProperty {
 public:
  explicit TrackColorProperty(Track* track)
      : TrackProperty(TrackProperties::kColor, Type::kColor, track) {}

 protected:
  Color ReadColor() const override { return GetTrack()->GetColor(); }
};

class TrackSelectedProperty : public TrackProperty {
 public:
  explicit TrackSelectedProperty(Track* track, bool ui)
      : TrackProperty(TrackProperties::kSelected, Type::kToggle, track),
        ui_(ui) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetSelected(); }
  void WriteBool(bool value) override {
    if (ui_) {
      GetTrack()->UiSelected();
    } else {
      GetTrack()->SetSelected(value);
    }
  }

 private:
  bool ui_;
};

class TrackMuteProperty : public TrackProperty {
 public:
  explicit TrackMuteProperty(Track* track, bool ui)
      : TrackProperty(TrackProperties::kMute, Type::kToggle, track), ui_(ui) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetMute(); }
  void WriteBool(bool value) override {
    if (ui_) {
      GetTrack()->UiMute();
    } else {
      GetTrack()->SetMute(value);
    }
  }

 private:
  bool ui_;
};

class TrackSoloProperty : public TrackProperty {
 public:
  explicit TrackSoloProperty(Track* track, bool ui)
      : TrackProperty(TrackProperties::kSolo, Type::kToggle, track), ui_(ui) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetSolo(); }
  void WriteBool(bool value) override {
    if (ui_) {
      GetTrack()->UiSolo();
    } else {
      GetTrack()->SetSolo(value);
    }
  }

 private:
  bool ui_;
};

class TrackRecArmProperty : public TrackProperty {
 public:
  explicit TrackRecArmProperty(Track* track, bool ui)
      : TrackProperty(TrackProperties::kRecArm, Type::kToggle, track),
        ui_(ui) {}

 protected:
  bool ReadBool() const override { return GetTrack()->GetRecArm(); }
  void WriteBool(bool value) override {
    if (ui_) {
      GetTrack()->UiRecArm();
    } else {
      GetTrack()->SetRecArm(value);
    }
  }

 private:
  bool ui_;
};

class TrackPanProperty : public TrackProperty {
 public:
  explicit TrackPanProperty(Track* track, bool ui)
      : TrackProperty(TrackProperties::kPan, Type::kPan, track), ui_(ui) {}

 protected:
  double ReadDouble() const override { return GetTrack()->GetPan(); }
  void WriteDouble(double value) override {
    if (ui_) {
      GetTrack()->UiPan(value);
    } else {
      GetTrack()->SetPan(value);
    }
  }

 private:
  bool ui_;
};

class TrackVolumeProperty : public TrackProperty {
 public:
  explicit TrackVolumeProperty(Track* track, bool ui)
      : TrackProperty(TrackProperties::kVolume, Type::kVolume, track),
        ui_(ui) {}

 protected:
  double ReadDouble() const override { return GetTrack()->GetVolume(); }
  void WriteDouble(double value) override {
    if (ui_) {
      GetTrack()->UiVolume(value);
    } else {
      GetTrack()->SetVolume(value);
    }
  }

 private:
  bool ui_;
};

}  // namespace

TrackProperties::TrackProperties(Track* track) : track_(track->GetShared()) {
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

ViewProperty* TrackProperties::GetProperty(std::string_view name) {
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
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
  if (name == kSelected) {
    auto& property = properties_[name] =
        std::make_unique<TrackSelectedProperty>(track_.get(), /*ui=*/false);
    return property.get();
  }
  if (name == kMute) {
    auto& property = properties_[name] =
        std::make_unique<TrackMuteProperty>(track_.get(), /*ui=*/false);
    return property.get();
  }
  if (name == kSolo) {
    auto& property = properties_[name] =
        std::make_unique<TrackSoloProperty>(track_.get(), /*ui=*/false);
    return property.get();
  }
  if (name == kRecArm) {
    auto& property = properties_[name] =
        std::make_unique<TrackRecArmProperty>(track_.get(), /*ui=*/false);
    return property.get();
  }
  if (name == kPan) {
    auto& property = properties_[name] =
        std::make_unique<TrackPanProperty>(track_.get(), /*ui=*/false);
    return property.get();
  }
  if (name == kVolume) {
    auto& property = properties_[name] =
        std::make_unique<TrackVolumeProperty>(track_.get(), /*ui=*/false);
    return property.get();
  }
  if (name == kUiSelected) {
    auto& property = properties_[name] =
        std::make_unique<TrackSelectedProperty>(track_.get(), /*ui=*/true);
    return property.get();
  }
  if (name == kUiMute) {
    auto& property = properties_[name] =
        std::make_unique<TrackMuteProperty>(track_.get(), /*ui=*/true);
    return property.get();
  }
  if (name == kUiSolo) {
    auto& property = properties_[name] =
        std::make_unique<TrackSoloProperty>(track_.get(), /*ui=*/true);
    return property.get();
  }
  if (name == kUiRecArm) {
    auto& property = properties_[name] =
        std::make_unique<TrackRecArmProperty>(track_.get(), /*ui=*/true);
    return property.get();
  }
  if (name == kUiPan) {
    auto& property = properties_[name] =
        std::make_unique<TrackPanProperty>(track_.get(), /*ui=*/true);
    return property.get();
  }
  if (name == kUiVolume) {
    auto& property = properties_[name] =
        std::make_unique<TrackVolumeProperty>(track_.get(), /*ui=*/true);
    return property.get();
  }
  return nullptr;
}

}  // namespace jpr
