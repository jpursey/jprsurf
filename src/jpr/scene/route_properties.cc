// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/route_properties.h"

#include <memory>
#include <string_view>
#include <utility>

#include "absl/strings/str_split.h"

namespace jpr {

namespace {

using Type = ViewProperty::Type;

class RouteVolumeProperty final : public RouteProperty {
 public:
  explicit RouteVolumeProperty(const RouteProperties* route_properties)
      : RouteProperty(RouteProperties::kVolume, Type::kVolume, kReadWriteAccess,
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
      : RouteProperty(RouteProperties::kPan, Type::kPan, kReadWriteAccess,
                      route_properties) {}

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
      : RouteProperty(RouteProperties::kMute, Type::kToggle, kReadWriteAccess,
                      route_properties) {}

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
      : RouteProperty(RouteProperties::kExists, Type::kToggle, kReadAccess,
                      route_properties) {}

 protected:
  bool ReadBool() const override {
    return GetRouteProperties().GetRoute() != nullptr;
  }
};

// Creates a property of type T on the route.
template <typename T>
std::unique_ptr<RouteProperty> Create(const RouteProperties* route_properties) {
  return std::make_unique<T>(route_properties);
}

struct RoutePropertyRow {
  PropertyInfo info;
  std::unique_ptr<RouteProperty> (*create)(
      const RouteProperties* route_properties);
};

// A new route property needs a name in the header, and a row here. The
// static_assert below checks that the names are unique, and in route:.
constexpr RoutePropertyRow kRouteProperties[] = {
    {{RouteProperties::kVolume, Type::kVolume, kReadWriteAccess,
      "The route's volume."},
     Create<RouteVolumeProperty>},
    {{RouteProperties::kPan, Type::kPan, kReadWriteAccess, "The route's pan."},
     Create<RoutePanProperty>},
    {{RouteProperties::kMute, Type::kToggle, kReadWriteAccess,
      "True while the route is muted."},
     Create<RouteMuteProperty>},
    {{RouteProperties::kExists, Type::kToggle, kReadAccess,
      "True if there is a route, as what shows a route may show none."},
     Create<RouteExistsProperty>},
};

constexpr auto kRoutePropertyInfo = GetPropertyInfo(kRouteProperties);
static_assert(AreValidProperties(kRoutePropertyInfo, kRouteNamespace),
              "kRouteProperties rows must have unique names in route:");

constexpr ReferenceInfo kRouteReferences[] = {
    {RouteProperties::kOtherTrack, SubjectKind::kTrack,
     "The track at the other end of the route: the track a send goes to, or a "
     "receive comes from. It refers to nothing if there is no route."},
};

}  // namespace

RouteProperties::RouteProperties(TrackActions* actions, Track* track,
                                 TrackRouteType type, int index)
    : track_(track),
      type_(type),
      index_(index),
      other_track_(kOtherTrack, actions) {
  track_->Subscribe(this);
  OnRouteChanged();
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
  if (track == track_ && type == type_ && index == index_) {
    return;
  }
  if (track != track_) {
    track_->Unsubscribe(this);
    track_ = track;
    track_->Subscribe(this);
  }
  type_ = type;
  index_ = index;
  OnRouteChanged();
}

void RouteProperties::OnTrackChanged(Track* track) {
  // The track may have been removed or restored, which changes its routes.
  OnRouteChanged();
}

void RouteProperties::OnTrackRoutesChanged(Track* track) { OnRouteChanged(); }

void RouteProperties::OnRouteChanged() {
  const TrackRoute* route = GetRoute();
  other_track_.Set(route != nullptr ? route->other_track : nullptr);
  for (auto& [name, property] : properties_) {
    property->NotifyChanged();
  }
}

ViewProperty* RouteProperties::GetProperty(std::string_view name) const {
  // A name with a '.' is a field of the other track.
  const auto [owner, field] = std::pair<std::string_view, std::string_view>(
      absl::StrSplit(name, absl::MaxSplits('.', 1)));
  if (!field.empty()) {
    return owner == kOtherTrack ? other_track_.GetField(field) : nullptr;
  }
  if (auto it = properties_.find(name); it != properties_.end()) {
    return it->second.get();
  }
  const RoutePropertyRow* row = FindRow(kRouteProperties, name);
  if (row == nullptr) {
    return nullptr;
  }
  std::unique_ptr<RouteProperty>& property = properties_[name];
  property = row->create(this);
  return property.get();
}

absl::Span<const PropertyInfo> GetRoutePropertyInfo() {
  return kRoutePropertyInfo;
}

absl::Span<const ReferenceInfo> GetRouteReferenceInfo() {
  return kRouteReferences;
}

}  // namespace jpr
