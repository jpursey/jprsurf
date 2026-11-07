// Copyright (c) 2026 John Pursey
//
// Use of this source code is governed by an MIT-style License that can be found
// in the LICENSE file or at https://opensource.org/licenses/MIT.

#include "jpr/scene/track_properties.h"

#include <algorithm>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "absl/algorithm/container.h"
#include "absl/log/check.h"
#include "absl/types/span.h"
#include "jpr/common/reaper_api.h"

namespace jpr {

namespace {

using Type = ViewProperty::Type;

class TrackNameProperty : public TrackProperty {
 public:
  explicit TrackNameProperty(Track* track)
      : TrackProperty(TrackProperties::kName, Type::kText, kReadWriteAccess,
                      track) {}

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
      : TrackProperty(TrackProperties::kColor, Type::kColor, kReadAccess,
                      track) {}

 protected:
  Color ReadColor() const override { return GetTrack()->GetColor(); }
};

// A property for selected, mute, solo, or record arm. With track actions, a
// write runs the action rather than setting the value.
class TrackBoolViewProperty : public TrackProperty {
 public:
  TrackBoolViewProperty(std::string_view name, TrackBoolProperty property,
                        Track* track, TrackActions* actions)
      : TrackProperty(name, Type::kToggle, kReadWriteAccess, track),
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

// The record modes, in REAPER's order, named for REAPER's "Set track record
// mode to" actions. REAPER's forced input formats (12 to 15), which no action
// sets, aren't among them.
constexpr TrackRecMode kRecModes[] = {
    TrackRecMode::kInput,
    TrackRecMode::kOutputStereo,
    TrackRecMode::kNone,
    TrackRecMode::kOutputStereoCompensated,
    TrackRecMode::kMidiOutput,
    TrackRecMode::kOutputMono,
    TrackRecMode::kOutputMonoCompensated,
    TrackRecMode::kMidiOverdub,
    TrackRecMode::kMidiReplace,
    TrackRecMode::kMidiTouchReplace,
    TrackRecMode::kOutputMultichannel,
    TrackRecMode::kOutputMultichannelCompensated,
    TrackRecMode::kMidiLatchReplace,
};
constexpr std::string_view kRecModeNames[] = {
    "input",
    "output_stereo",
    "none",
    "output_stereo_compensated",
    "midi_output",
    "output_mono",
    "output_mono_compensated",
    "midi_overdub",
    "midi_replace",
    "midi_touch_replace",
    "output_multichannel",
    "output_multichannel_compensated",
    "midi_latch_replace",
};
static_assert(std::size(kRecModes) == std::size(kRecModeNames));

// The monitoring modes, in TrackMonitorMode's order.
constexpr std::string_view kMonitorModeNames[] = {"off", "on", "auto"};

class RecModeValue final : public TrackValue {
 public:
  constexpr RecModeValue() : TrackValue(kRecModeNames) {}

  int Get(const Track& track) const override {
    // A mode with no name, such as a forced input format, reads as input,
    // which it is a form of.
    const auto* it = absl::c_find(kRecModes, track.GetRecMode());
    return it != std::end(kRecModes)
               ? static_cast<int>(it - std::begin(kRecModes))
               : 0;
  }
  void Set(TrackBatch& batch, Track* track, int value,
           TrackGrouping grouping) const override {
    DCHECK(value >= 0 && value <= GetMaxValue());
    batch.SetRecMode(track, kRecModes[value], grouping);
  }
  std::vector<Track*> GetGroup(Track* track) const override {
    return TrackCache::Get().GetRecArmGroup(track);
  }
};

class MonitorModeValue final : public TrackValue {
 public:
  constexpr MonitorModeValue() : TrackValue(kMonitorModeNames) {}

  int Get(const Track& track) const override {
    return static_cast<int>(track.GetMonitorMode());
  }
  void Set(TrackBatch& batch, Track* track, int value,
           TrackGrouping grouping) const override {
    DCHECK(value >= 0 && value <= GetMaxValue());
    batch.SetMonitorMode(track, static_cast<TrackMonitorMode>(value), grouping);
  }
  std::vector<Track*> GetGroup(Track* track) const override {
    // REAPER gangs the monitoring of the selected tracks only, and the track's
    // own selection is fresh, as the surface refreshes a track before acting
    // on it. GetSelectedTracks() never has the master.
    if (!track->GetSelected()) {
      return {track};
    }
    std::vector<Track*> group = TrackCache::Get().GetSelectedTracks();
    if (!absl::c_linear_search(group, track)) {
      group.push_back(track);
    }
    return group;
  }
};

// The one instance of each kind of TrackValue.
template <typename T>
constexpr T kTrackValue{};

// A property for an enumerated value of the track. With track actions, a write
// or a press runs the action rather than setting the value: a write of a value
// is a press that sets it.
class TrackEnumeratedProperty : public TrackProperty {
 public:
  TrackEnumeratedProperty(std::string_view name, const TrackValue& value,
                          Track* track, TrackActions* actions)
      : TrackProperty(name, Type::kEnumerated, kReadWriteAccess, track),
        value_(value),
        actions_(actions) {}

  int GetMaxValue() const override { return value_.GetMaxValue(); }
  std::string GetText() const override {
    return std::string(value_.GetNames()[GetInt()]);
  }
  std::optional<int> FindValue(std::string_view name) const override {
    return FindValueName(value_.GetNames(), name);
  }

 protected:
  int ReadInt() const override { return value_.Get(*GetTrack()); }
  void WriteInt(int value) override {
    if (actions_ != nullptr) {
      WritePress({.min = value, .max = value});
      return;
    }
    TrackBatch batch;
    value_.Set(batch, GetTrack(), value, TrackGrouping::kNone);
  }
  void WritePress(const EnumeratedPress& press) override {
    if (actions_ == nullptr) {
      TrackProperty::WritePress(press);
      return;
    }
    actions_->UiPress(GetTrack(), value_, press, /*anchor=*/nullptr);
  }

 private:
  const TrackValue& value_;
  TrackActions* const actions_;
};

// A property for pan. With track actions, a write runs the action rather than
// setting the value.
class TrackPanProperty : public TrackProperty {
 public:
  TrackPanProperty(std::string_view name, Track* track, TrackActions* actions)
      : TrackProperty(name, Type::kPan, kReadWriteAccess, track),
        actions_(actions) {}

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
      : TrackProperty(name, Type::kVolume, kReadWriteAccess, track),
        actions_(actions) {}

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

// A property for whether the track's volume or pan is touched. Writing true
// holds a touch on the track, which moves with the property to another track.
class TrackTouchedProperty : public TrackProperty {
 public:
  TrackTouchedProperty(std::string_view name, TrackTouchType type, Track* track)
      : TrackProperty(name, Type::kToggle, kReadWriteAccess, track),
        type_(type) {}

 protected:
  bool ReadBool() const override { return GetTrack()->IsTouched(type_); }
  void WriteBool(bool value) override {
    if (value == touch_.IsRegistered()) {
      return;
    }
    touch_ = value ? GetTrack()->RegisterTouch(type_) : TrackTouchHandle();
  }
  void OnTrackSet() override {
    if (touch_.IsRegistered()) {
      touch_ = GetTrack()->RegisterTouch(type_);
    }
  }

 private:
  const TrackTouchType type_;
  TrackTouchHandle touch_;
};

class TrackIsFolderProperty : public TrackProperty {
 public:
  TrackIsFolderProperty(Track* track, TrackFilter filter)
      : TrackProperty(TrackProperties::kTrackIsFolder, Type::kToggle,
                      kReadAccess, track),
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
      : TrackProperty(TrackProperties::kTrackHasParent, Type::kToggle,
                      kReadAccess, track) {}

 protected:
  bool ReadBool() const override {
    return GetTrack()->GetParentTrack() != nullptr;
  }
};

class TrackExistsProperty : public TrackProperty {
 public:
  explicit TrackExistsProperty(Track* track)
      : TrackProperty(TrackProperties::kTrackExists, Type::kToggle, kReadAccess,
                      track) {}

 protected:
  bool ReadBool() const override { return GetTrack()->Exists(); }
};

class TrackHasRoutesProperty : public TrackProperty {
 public:
  explicit TrackHasRoutesProperty(Track* track)
      : TrackProperty(TrackProperties::kTrackHasRoutes, Type::kToggle,
                      kReadAccess, track) {}

 protected:
  bool ReadBool() const override {
    return !GetTrack()->GetSends().empty() ||
           !GetTrack()->GetReceives().empty();
  }
};

//------------------------------------------------------------------------------
// Track property table
//------------------------------------------------------------------------------

// Creates a property of type T on the track.
template <typename T>
std::unique_ptr<TrackProperty> Create(std::string_view name, Track* track,
                                      TrackActions* actions) {
  return std::make_unique<T>(track);
}

// Creates a property for selected, mute, solo, or record arm, which runs the
// track action if kUi is true.
template <TrackBoolProperty kProperty, bool kUi>
std::unique_ptr<TrackProperty> CreateBool(std::string_view name, Track* track,
                                          TrackActions* actions) {
  return std::make_unique<TrackBoolViewProperty>(name, kProperty, track,
                                                 kUi ? actions : nullptr);
}

// Creates a pan or volume property of type T, which runs the track action if
// kUi is true.
template <typename T, bool kUi>
std::unique_ptr<TrackProperty> CreateLevel(std::string_view name, Track* track,
                                           TrackActions* actions) {
  return std::make_unique<T>(name, track, kUi ? actions : nullptr);
}

// Creates a property for the enumerated value T, which runs the track action if
// kUi is true.
template <typename T, bool kUi>
std::unique_ptr<TrackProperty> CreateEnumerated(std::string_view name,
                                                Track* track,
                                                TrackActions* actions) {
  return std::make_unique<TrackEnumeratedProperty>(name, kTrackValue<T>, track,
                                                   kUi ? actions : nullptr);
}

template <TrackTouchType kType>
std::unique_ptr<TrackProperty> CreateTouched(std::string_view name,
                                             Track* track,
                                             TrackActions* actions) {
  return std::make_unique<TrackTouchedProperty>(name, kType, track);
}

std::unique_ptr<TrackProperty> CreateIsFolder(std::string_view name,
                                              Track* track,
                                              TrackActions* actions) {
  return std::make_unique<TrackIsFolderProperty>(track,
                                                 actions->GetTrackFilter());
}

struct TrackPropertyRow {
  PropertyInfo info;
  std::unique_ptr<TrackProperty> (*create)(std::string_view name, Track* track,
                                           TrackActions* actions);
};

// A new track property needs a name in the header, and a row here. The
// static_assert below checks that the names are unique, and in track:.
constexpr TrackPropertyRow kTrackProperties[] = {
    {{TrackProperties::kName, Type::kText, kReadWriteAccess,
      "The track's name. Writing it renames the track."},
     Create<TrackNameProperty>},
    {{TrackProperties::kColor, Type::kColor, kReadAccess, "The track's color."},
     Create<TrackColorProperty>},
    {{TrackProperties::kSelected, Type::kToggle, kReadWriteAccess,
      "True while the track is selected. Writing it selects or unselects this "
      "track alone."},
     CreateBool<TrackBoolProperty::kSelected, false>},
    {{TrackProperties::kMute, Type::kToggle, kReadWriteAccess,
      "True while the track is muted. Writing it mutes or unmutes this track "
      "alone."},
     CreateBool<TrackBoolProperty::kMute, false>},
    {{TrackProperties::kSolo, Type::kToggle, kReadWriteAccess,
      "True while the track is soloed. Writing it solos or unsolos this track "
      "alone."},
     CreateBool<TrackBoolProperty::kSolo, false>},
    {{TrackProperties::kRecArm, Type::kToggle, kReadWriteAccess,
      "True while the track is armed for recording. Writing it arms or "
      "disarms this track alone."},
     CreateBool<TrackBoolProperty::kRecArm, false>},
    {{.name = TrackProperties::kRecMode,
      .type = Type::kEnumerated,
      .access = kReadWriteAccess,
      .description =
          "The track's record mode, which decides what it records while armed, "
          "named for REAPER's \"Set track record mode to\" actions: \"none\" "
          "records nothing, and only monitors the input. REAPER's forced input "
          "formats, which no action sets, read as \"input\". Writing it sets "
          "this track's mode alone, but never the master track's, which has "
          "none.",
      .values = kTrackValue<RecModeValue>.GetNames()},
     CreateEnumerated<RecModeValue, false>},
    {{.name = TrackProperties::kMonitorMode,
      .type = Type::kEnumerated,
      .access = kReadWriteAccess,
      .description =
          "The track's record monitoring: \"auto\" is REAPER's tape auto "
          "style. Writing it sets this track's monitoring alone. The master "
          "track's always reads as \"off\", and is never written.",
      .values = kTrackValue<MonitorModeValue>.GetNames()},
     CreateEnumerated<MonitorModeValue, false>},
    {{TrackProperties::kPan, Type::kPan, kReadWriteAccess,
      "The track's pan. Writing it pans this track alone."},
     CreateLevel<TrackPanProperty, false>},
    {{TrackProperties::kVolume, Type::kVolume, kReadWriteAccess,
      "The track's volume. Writing it sets this track's volume alone."},
     CreateLevel<TrackVolumeProperty, false>},
    {{TrackProperties::kMeter, Type::kNormalized, kReadAccess,
      "The track's peak meter level."},
     Create<TrackMeterProperty>},
    {{TrackProperties::kUiSelected, Type::kToggle, kReadWriteAccess,
      "The same as \"track:selected\", but writing it runs the surface's "
      "select action, as clicking the track in REAPER does, with the surface's "
      "modifiers and anchors."},
     CreateBool<TrackBoolProperty::kSelected, true>},
    {{TrackProperties::kUiMute, Type::kToggle, kReadWriteAccess,
      "The same as \"track:mute\", but writing it runs the surface's mute "
      "action, as clicking mute in REAPER does, with the surface's modifiers, "
      "grouping, and anchors."},
     CreateBool<TrackBoolProperty::kMute, true>},
    {{TrackProperties::kUiSolo, Type::kToggle, kReadWriteAccess,
      "The same as \"track:solo\", but writing it runs the surface's solo "
      "action, as clicking solo in REAPER does, with the surface's modifiers, "
      "grouping, and anchors."},
     CreateBool<TrackBoolProperty::kSolo, true>},
    {{TrackProperties::kUiRecArm, Type::kToggle, kReadWriteAccess,
      "The same as \"track:rec_arm\", but writing it runs the surface's record "
      "arm action, as clicking record arm in REAPER does, with the surface's "
      "modifiers, grouping, and anchors."},
     CreateBool<TrackBoolProperty::kRecArm, true>},
    {{.name = TrackProperties::kUiRecMode,
      .type = Type::kEnumerated,
      .access = kReadWriteAccess,
      .description =
          "The same as \"track:rec_mode\", but a press or a write runs the "
          "surface's action for an enumerated value, with the surface's "
          "modifiers and grouping. Its grouping reaches the tracks a grouped "
          "record arm does.",
      .values = kTrackValue<RecModeValue>.GetNames()},
     CreateEnumerated<RecModeValue, true>},
    {{.name = TrackProperties::kUiMonitorMode,
      .type = Type::kEnumerated,
      .access = kReadWriteAccess,
      .description =
          "The same as \"track:monitor_mode\", but a press or a write runs the "
          "surface's action for an enumerated value, with the surface's "
          "modifiers and grouping. Its grouping reaches the selected tracks, "
          "if the track is selected, as REAPER's groups have no monitoring.",
      .values = kTrackValue<MonitorModeValue>.GetNames()},
     CreateEnumerated<MonitorModeValue, true>},
    {{TrackProperties::kUiPan, Type::kPan, kReadWriteAccess,
      "The same as \"track:pan\", but writing it runs the surface's pan "
      "action, as panning in REAPER does, with the surface's modifiers and "
      "grouping."},
     CreateLevel<TrackPanProperty, true>},
    {{TrackProperties::kUiVolume, Type::kVolume, kReadWriteAccess,
      "The same as \"track:volume\", but writing it runs the surface's volume "
      "action, as moving the fader in REAPER does, with the surface's "
      "modifiers and grouping."},
     CreateLevel<TrackVolumeProperty, true>},
    {{TrackProperties::kTrackIsFolder, Type::kToggle, kReadAccess,
      "True if the track has child tracks on the surface."},
     CreateIsFolder},
    {{TrackProperties::kTrackHasParent, Type::kToggle, kReadAccess,
      "True if the track has a parent track, which is every track except the "
      "master track."},
     Create<TrackHasParentProperty>},
    {{TrackProperties::kTrackExists, Type::kToggle, kReadAccess,
      "True if there is a track, as what shows a track may show none."},
     Create<TrackExistsProperty>},
    {{TrackProperties::kTrackHasRoutes, Type::kToggle, kReadAccess,
      "True if the track has any sends or receives."},
     Create<TrackHasRoutesProperty>},
    {{TrackProperties::kVolumeTouched, Type::kToggle, kReadWriteAccess,
      "True while anything touches the track's volume, for REAPER's Touch and "
      "Latch automation. Writing true touches it until false is written, and "
      "the touch moves with the property to another track."},
     CreateTouched<TrackTouchType::kVolume>},
    {{TrackProperties::kPanTouched, Type::kToggle, kReadWriteAccess,
      "True while anything touches the track's pan, for REAPER's Touch and "
      "Latch automation. Writing true touches it until false is written, and "
      "the touch moves with the property to another track."},
     CreateTouched<TrackTouchType::kPan>},
};

constexpr auto kTrackPropertyInfo = GetPropertyInfo(kTrackProperties);
static_assert(AreValidProperties(kTrackPropertyInfo, kTrackNamespace),
              "kTrackProperties rows must have unique names in track:");

}  // namespace

TrackProperties::TrackProperties(TrackActions* actions, Track* track)
    : actions_(actions), track_(track) {
  track_->Subscribe(this);
}

TrackProperties::~TrackProperties() { track_->Unsubscribe(this); }

void TrackProperties::SetTrack(Track* track) {
  if (track == track_) {
    return;
  }
  track_->Unsubscribe(this);
  track_ = track;
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

void TrackProperties::OnTrackTouchChanged(Track* track, TrackTouchType type) {
  if (TrackProperty* property = touched_properties_[static_cast<int>(type)];
      property != nullptr) {
    property->NotifyChanged();
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
  const TrackPropertyRow* row = FindRow(kTrackProperties, name);
  if (row == nullptr) {
    return nullptr;
  }
  std::unique_ptr<TrackProperty>& property = properties_[name];
  property = row->create(name, track_, actions_);

  // The properties notified of more than a change to the track.
  if (name == kMeter) {
    meter_property_ = static_cast<TrackMeterProperty*>(property.get());
  } else if (name == kTrackHasRoutes) {
    has_routes_property_ = property.get();
  } else if (name == kVolumeTouched) {
    touched_properties_[static_cast<int>(TrackTouchType::kVolume)] =
        property.get();
  } else if (name == kPanTouched) {
    touched_properties_[static_cast<int>(TrackTouchType::kPan)] =
        property.get();
  }
  return property.get();
}

bool TrackProperties::IsWatched() const {
  return absl::c_any_of(
      properties_, [](const auto& entry) { return entry.second->IsWatched(); });
}

absl::Span<const PropertyInfo> GetTrackPropertyInfo() {
  return kTrackPropertyInfo;
}

}  // namespace jpr
