// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/route_properties.h"

namespace jpr {

namespace {

class RouteVolumeProperty final : public RouteProperty {
 public:
  explicit RouteVolumeProperty(const RouteProperties* route_properties)
      : RouteProperty(RouteProperties::kVolume, Type::kVolume,
                      route_properties) {}

 protected:
  double ReadDouble() const override {
    const TrackRoute* route = GetRouteProperties().GetRoute();
    return route != nullptr ? route->volume : 0.0;
  }
  void WriteDouble(double value) override {
    const RouteProperties& route = GetRouteProperties();
    route.GetTrack()->SetRouteVolume(route.GetType(), route.GetIndex(), value);
  }
};

class RoutePanProperty final : public RouteProperty {
 public:
  explicit RoutePanProperty(const RouteProperties* route_properties)
      : RouteProperty(RouteProperties::kPan, Type::kPan, route_properties) {}

 protected:
  double ReadDouble() const override {
    const TrackRoute* route = GetRouteProperties().GetRoute();
    return route != nullptr ? route->pan : 0.0;
  }
  void WriteDouble(double value) override {
    const RouteProperties& route = GetRouteProperties();
    route.GetTrack()->SetRoutePan(route.GetType(), route.GetIndex(), value);
  }
};

class RouteMuteProperty final : public RouteProperty {
 public:
  explicit RouteMuteProperty(const RouteProperties* route_properties)
      : RouteProperty(RouteProperties::kMute, Type::kToggle, route_properties) {
  }

 protected:
  bool ReadBool() const override {
    const TrackRoute* route = GetRouteProperties().GetRoute();
    return route != nullptr && route->mute;
  }
  void WriteBool(bool value) override {
    const RouteProperties& route = GetRouteProperties();
    route.GetTrack()->SetRouteMute(route.GetType(), route.GetIndex(), value);
  }
};

class RouteExistsProperty final : public RouteProperty {
 public:
  explicit RouteExistsProperty(const RouteProperties* route_properties)
      : RouteProperty(RouteProperties::kExists, Type::kToggle,
                      route_properties) {}

 protected:
  bool ReadBool() const override {
    return GetRouteProperties().GetRoute() != nullptr;
  }
};

}  // namespace

RouteProperties::RouteProperties(Track* track, TrackRouteType type, int index)
    : track_(track->GetShared()), type_(type), index_(index) {
  track_->Subscribe(this);
}

RouteProperties::~RouteProperties() { track_->Unsubscribe(this); }

const TrackRoute* RouteProperties::GetRoute() const {
  absl::Span<const TrackRoute> routes = track_->GetRoutes(type_);
  if (index_ < 0 || index_ >= static_cast<int>(routes.size())) {
    return nullptr;
  }
  return &routes[index_];
}

void RouteProperties::SetRoute(Track* track, TrackRouteType type, int index) {
  if (track == track_.get() && type == type_ && index == index_) {
    return;
  }
  if (track != track_.get()) {
    track_->Unsubscribe(this);
    track_ = track->GetShared();
    track_->Subscribe(this);
  }
  type_ = type;
  index_ = index;
  NotifyChanged();
}

void RouteProperties::OnTrackChanged(Track* track) {
  // The track may have been removed or restored, which changes its routes.
  NotifyChanged();
}

void RouteProperties::OnTrackRoutesChanged(Track* track) { NotifyChanged(); }

void RouteProperties::NotifyChanged() {
  for (auto& [name, property] : properties_) {
    property->NotifyChanged();
  }
}

ViewProperty* RouteProperties::GetProperty(std::string_view name) const {
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }
  std::unique_ptr<RouteProperty> property;
  if (name == kVolume) {
    property = std::make_unique<RouteVolumeProperty>(this);
  } else if (name == kPan) {
    property = std::make_unique<RoutePanProperty>(this);
  } else if (name == kMute) {
    property = std::make_unique<RouteMuteProperty>(this);
  } else if (name == kExists) {
    property = std::make_unique<RouteExistsProperty>(this);
  } else {
    return nullptr;
  }
  RouteProperty* property_ptr = property.get();
  properties_[name] = std::move(property);
  return property_ptr;
}

}  // namespace jpr
